#pragma once
// Event-driven backtester. Replays an in-order feed through the order book. Strategy orders
// enter the SAME book (ids >= kOwnBase), so price-time priority and queue position are modelled
// by the matching engine itself, not by a fill heuristic.
//
// Realism and its limits (state these when presenting results):
//   + order latency: actions take effect `latency` feed messages after the decision
//   + queue position: resting orders sit behind earlier orders at their price
//   + fees/rebates per share, position limit, cancel races (fill can win against a late cancel)
//   - replayed feed does not react to our orders (no market impact, no adverse reaction)
//   - feed is synthetic or an L1 reconstruction, so fill realism is only as good as that data
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <unordered_map>
#include <vector>

#include "lob/feed.hpp"

namespace lob {

constexpr OrderId kOwnBase = OrderId(1) << 60;
inline bool is_own(OrderId id) { return id >= kOwnBase; }

struct BacktestConfig {
    std::uint64_t latency = 10;      // feed messages between decision and effect (>= 1)
    double taker_fee = 0.3;          // ticks per share paid when we take liquidity
    double maker_fee = 0.0;          // ticks per share when we make (negative = rebate)
    std::int64_t max_pos = 500;      // absolute position limit, shares
    std::size_t sample_every = 1000; // equity sampling interval, messages
};

struct Fill {
    std::uint64_t seq;
    OrderId id;
    Side side;
    Price price;
    Qty qty;
    bool maker;
};

class Engine;

class Gateway {
public:
    OrderId send(Side side, Price price, Qty qty, TimeInForce tif);
    void cancel(OrderId id);
    bool is_open(OrderId id) const;
private:
    friend class Engine;
    explicit Gateway(Engine& e) : e_(e) {}
    Engine& e_;
};

struct View {
    const OrderBook& book;
    std::uint64_t seq;
    bool valid;      // two-sided book
    Price bid, ask;
    double mid;
    std::int64_t pos;
    double equity;   // ticks, after fees
    double imbalance1() const {
        auto b = book.depth(Side::Buy, 1), a = book.depth(Side::Sell, 1);
        if (b.empty() || a.empty()) return 0.0;
        double bq = double(b[0].qty), aq = double(a[0].qty);
        return (bq - aq) / (bq + aq);
    }
};

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual void on_event(const View& v, Gateway& gw) = 0;
};

struct BacktestResult {
    double net_pnl = 0, gross_pnl = 0, fees = 0;  // ticks
    std::int64_t final_pos = 0, max_abs_pos = 0;
    std::uint64_t fills = 0, maker_fills = 0, taker_fills = 0, rejects = 0, volume = 0;
    double max_drawdown = 0;     // ticks, on sampled equity
    double sharpe_per_sample = 0; // mean/std of sampled equity increments (NOT annualised)
    std::vector<double> equity;  // sampled
    std::vector<Fill> trace;
};

class Engine {
public:
    explicit Engine(BacktestConfig c = {}) : cfg_(c) { if (cfg_.latency < 1) cfg_.latency = 1; }

    BacktestResult run(const std::vector<Msg>& feed, Strategy& s, bool keep_trace = false) {
        OrderBook book;
        book_ = &book;
        *this = reset(cfg_, &book);
        Gateway gw(*this);
        keep_trace_ = keep_trace;
        std::vector<Trade> trades;
        for (const Msg& m : feed) {
            seq_ = m.seq;
            while (!pending_.empty() && pending_.front().act <= seq_) {
                activate(pending_.front());
                pending_.pop_front();
            }
            trades.clear();
            if (m.kind == Msg::Kind::Add) book.submit(m.order, &trades); else book.cancel(m.id);
            for (const Trade& t : trades) account(t);
            refresh_mid();
            View v{book, seq_, valid_, bid_, ask_, mid_, pos_, equity(), };
            s.on_event(v, gw);
            if (m.seq % cfg_.sample_every == 0) sample();
        }
        res_.final_pos = pos_;
        res_.fees = fees_;
        res_.net_pnl = equity();
        res_.gross_pnl = res_.net_pnl + fees_;
        finish_stats();
        return std::move(res_);
    }

private:
    friend class Gateway;
    struct Action { std::uint64_t act; bool cancel; Order o; OrderId id; };

    static Engine reset(BacktestConfig c, OrderBook* b) { Engine e(c); e.book_ = b; return e; }

    double equity() const { return cash_ + double(pos_) * mid_ - fees_; }
    void refresh_mid() {
        Price b, a;
        valid_ = book_->best_bid(b) && book_->best_ask(a);
        if (valid_) { bid_ = b; ask_ = a; mid_ = 0.5 * (double(b) + double(a)); }
    }
    void sample() {
        res_.equity.push_back(equity());
    }
    void finish_stats() {
        const auto& e = res_.equity;
        double peak = -1e300, dd = 0;
        for (double x : e) { peak = std::max(peak, x); dd = std::max(dd, peak - x); }
        res_.max_drawdown = dd;
        if (e.size() > 2) {
            std::vector<double> d;
            for (std::size_t i = 1; i < e.size(); ++i) d.push_back(e[i] - e[i - 1]);
            double m = 0; for (double x : d) m += x; m /= double(d.size());
            double v = 0; for (double x : d) v += (x - m) * (x - m); v /= double(d.size());
            res_.sharpe_per_sample = v > 0 ? m / std::sqrt(v) : 0.0;
        }
    }

    OrderId submit_own(Side side, Price price, Qty qty, TimeInForce tif) {
        OrderId id = next_own_++;
        pending_.push_back(Action{seq_ + cfg_.latency, false, Order{id, side, OrderType::Limit, price, qty, tif}, id});
        return id;
    }
    void cancel_own(OrderId id) { pending_.push_back(Action{seq_ + cfg_.latency, true, Order{}, id}); }

    void activate(const Action& a) {
        if (a.cancel) {
            if (book_->cancel(a.id)) drop_open(a.id);
            return;
        }
        const Order& o = a.o;
        std::int64_t worst = o.side == Side::Buy ? pos_ + open_buy_ + o.qty : -(pos_ - open_sell_ - std::int64_t(o.qty));
        if (worst > cfg_.max_pos) { ++res_.rejects; return; }
        std::vector<Trade> trades;
        Result r = book_->submit(o, &trades);
        if (r.status == Status::Rejected) { ++res_.rejects; return; }
        for (const Trade& t : trades) account(t);
        if (r.status == Status::Resting) {
            Qty rem = o.qty - r.filled;
            open_[o.id] = {o.side, rem};
            (o.side == Side::Buy ? open_buy_ : open_sell_) += rem;
        }
    }
    void drop_open(OrderId id) {
        auto it = open_.find(id);
        if (it == open_.end()) return;
        (it->second.first == Side::Buy ? open_buy_ : open_sell_) -= it->second.second;
        open_.erase(it);
    }

    void account(const Trade& t) {
        bool mk = is_own(t.maker_id), tk = is_own(t.taker_id);
        if (mk == tk) return;  // not ours (self-cross is out of scope)
        Fill f;
        f.seq = seq_;
        f.price = t.price;
        f.qty = t.qty;
        f.maker = mk;
        if (mk) {
            f.id = t.maker_id;
            f.side = t.taker_side == Side::Buy ? Side::Sell : Side::Buy;
            auto it = open_.find(t.maker_id);
            if (it != open_.end()) {
                it->second.second -= t.qty;
                (it->second.first == Side::Buy ? open_buy_ : open_sell_) -= t.qty;
                if (it->second.second == 0) open_.erase(it);
            }
        } else {
            f.id = t.taker_id;
            f.side = t.taker_side;
        }
        double sgn = f.side == Side::Buy ? 1.0 : -1.0;
        pos_ += std::int64_t(sgn) * std::int64_t(f.qty);
        cash_ -= sgn * double(f.price) * double(f.qty);
        fees_ += double(f.qty) * (mk ? cfg_.maker_fee : cfg_.taker_fee);
        ++res_.fills; (mk ? res_.maker_fills : res_.taker_fills)++;
        res_.volume += f.qty;
        res_.max_abs_pos = std::max<std::int64_t>(res_.max_abs_pos, pos_ < 0 ? -pos_ : pos_);
        if (keep_trace_) res_.trace.push_back(f);
    }

    BacktestConfig cfg_;
    OrderBook* book_ = nullptr;
    std::uint64_t seq_ = 0;
    OrderId next_own_ = kOwnBase;
    std::deque<Action> pending_;
    std::unordered_map<OrderId, std::pair<Side, Qty>> open_;
    std::int64_t open_buy_ = 0, open_sell_ = 0, pos_ = 0;
    double cash_ = 0, fees_ = 0, mid_ = 0;
    bool valid_ = false, keep_trace_ = false;
    Price bid_ = 0, ask_ = 0;
    BacktestResult res_;
};

inline OrderId Gateway::send(Side s, Price p, Qty q, TimeInForce t) { return e_.submit_own(s, p, q, t); }
inline void Gateway::cancel(OrderId id) { e_.cancel_own(id); }
inline bool Gateway::is_open(OrderId id) const { return e_.open_.count(id) != 0; }

// ---------------------------------------------------------------- example strategies

// Quotes both sides around mid, skews by inventory, refreshes every `refresh` messages.
class MarketMaker final : public Strategy {
public:
    struct Params {
        double half_spread = 2.0;   // ticks each side of (skewed) mid
        Qty size = 10;
        double skew = 0.02;         // ticks of quote shift per share of inventory
        std::uint64_t refresh = 50;
    };
    MarketMaker() : MarketMaker(Params{}) {}
    explicit MarketMaker(Params p) : p_(p) {}
    void on_event(const View& v, Gateway& gw) override {
        if (!v.valid || v.seq % p_.refresh != 0) return;
        if (bid_ && gw.is_open(bid_)) gw.cancel(bid_);
        if (ask_ && gw.is_open(ask_)) gw.cancel(ask_);
        double c = v.mid - p_.skew * double(v.pos);
        Price b = Price(std::floor(c - p_.half_spread)), a = Price(std::ceil(c + p_.half_spread));
        if (b < 1) b = 1;
        if (b >= a) a = b + 1;
        bid_ = gw.send(Side::Buy, b, p_.size, TimeInForce::GTC);
        ask_ = gw.send(Side::Sell, a, p_.size, TimeInForce::GTC);
    }
private:
    Params p_;
    OrderId bid_ = 0, ask_ = 0;
};

// Crosses the spread when top-of-book imbalance is extreme, exits after `hold` messages.
class ImbalanceTaker final : public Strategy {
public:
    struct Params { double threshold = 0.6; Qty size = 20; std::uint64_t hold = 200; Price max_spread = 3; };
    ImbalanceTaker() : ImbalanceTaker(Params{}) {}
    explicit ImbalanceTaker(Params p) : p_(p) {}
    void on_event(const View& v, Gateway& gw) override {
        if (!v.valid) return;
        if (state_ == 1 && v.seq >= exit_at_) {  // exit regardless of book quality
            if (v.pos == 0) { state_ = 0; return; }  // entry never filled
            Qty q = Qty(v.pos < 0 ? -v.pos : v.pos);
            if (v.pos > 0) gw.send(Side::Sell, 1, q, TimeInForce::IOC);
            else gw.send(Side::Buy, std::numeric_limits<Price>::max() / 4, q, TimeInForce::IOC);
            state_ = 2; exit_sent_ = v.seq;
            return;
        }
        if (state_ == 2 && v.seq >= exit_sent_ + 50) state_ = 0;  // wait for exit to land
        if (state_ != 0 || v.pos != 0 || v.ask - v.bid > p_.max_spread) return;
        double imb = v.imbalance1();
        if (std::fabs(imb) < p_.threshold) return;
        side_ = imb > 0 ? Side::Buy : Side::Sell;
        if (side_ == Side::Buy) gw.send(Side::Buy, v.ask + 2, p_.size, TimeInForce::IOC);
        else gw.send(Side::Sell, std::max<Price>(1, v.bid - 2), p_.size, TimeInForce::IOC);
        state_ = 1; exit_at_ = v.seq + p_.hold;
    }
private:
    Params p_;
    int state_ = 0;  // 0 flat, 1 holding, 2 exiting
    Side side_ = Side::Buy;
    std::uint64_t exit_at_ = 0, exit_sent_ = 0;
};

}  // namespace lob
