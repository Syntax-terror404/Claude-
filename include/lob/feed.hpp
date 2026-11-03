#pragma once
// Market-data pipeline: sequenced feed -> gap/dup/reorder handling -> order book -> analytics.
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <random>
#include <vector>

#include "lob/order_book.hpp"

namespace lob {

struct Msg {
    enum class Kind : std::uint8_t { Add, Cancel } kind{Kind::Add};
    std::uint64_t seq{0};
    Order order{};  // Add
    OrderId id{0};  // Cancel
};

// Deterministic synthetic feed. Replace with a blpapi adapter to read real data.
inline std::vector<Msg> generate_feed(std::size_t n, unsigned seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> noise(0.0, 10.0);
    std::vector<Msg> out;
    out.reserve(n);
    Price mid = 10000;
    OrderId next = 1;
    for (std::size_t i = 0; i < n; ++i) {
        if (rng() % 200 == 0) mid += (rng() & 1) ? 1 : -1;
        Msg m;
        m.seq = i + 1;
        unsigned d = rng() % 100;
        if (d < 30 && next > 1) {
            m.kind = Msg::Kind::Cancel;
            m.id = 1 + rng() % (next - 1);
        } else {
            Order o;
            o.id = next++;
            o.side = rng() & 1 ? Side::Buy : Side::Sell;
            o.qty = 1 + rng() % 100;
            if (d >= 95) { o.type = OrderType::Market; o.tif = TimeInForce::IOC; }
            else {
                Price off = static_cast<Price>(std::abs(noise(rng))) - 2;
                o.price = o.side == Side::Buy ? mid - off : mid + off;
                if (o.price < 1) o.price = 1;
            }
            m.order = o;
        }
        out.push_back(m);
    }
    return out;
}

struct ChannelConfig {
    double loss = 0.02, dup = 0.01;
    std::size_t reorder_window = 6;
    unsigned seed = 7;
};

// Simulates an unreliable multicast channel: drops, duplicates, local reordering.
inline std::vector<Msg> impair(const std::vector<Msg>& feed, const ChannelConfig& c) {
    std::mt19937_64 rng(c.seed);
    std::uniform_real_distribution<double> u(0, 1);
    std::vector<Msg> out;
    for (const auto& m : feed) {
        if (u(rng) < c.loss) continue;
        out.push_back(m);
        if (u(rng) < c.dup) out.push_back(m);
    }
    for (std::size_t i = 0; i + 1 < out.size(); ++i) {
        std::size_t j = i + rng() % std::min(c.reorder_window, out.size() - i);
        std::swap(out[i], out[j]);
    }
    return out;
}

struct FeedStats {
    std::uint64_t applied = 0, duplicates = 0, buffered = 0, gaps_recovered = 0, retransmits = 0;
};

// Rolling analytics over the last `window` trades plus top-of-book snapshot.
class Analytics {
public:
    explicit Analytics(std::size_t window = 1000) : window_(window) {}
    void on_trade(const Trade& t) {
        trades_.push_back(t);
        pv_ += double(t.price) * t.qty;
        v_ += t.qty;
        if (trades_.size() > window_) {
            const Trade& o = trades_.front();
            pv_ -= double(o.price) * o.qty;
            v_ -= o.qty;
            trades_.pop_front();
        }
    }
    double vwap() const { return v_ ? pv_ / double(v_) : 0.0; }
    static bool mid_spread(const OrderBook& b, double& mid, Price& spread) {
        Price bid, ask;
        if (!b.best_bid(bid) || !b.best_ask(ask)) return false;
        mid = (bid + ask) / 2.0;
        spread = ask - bid;
        return true;
    }
    // (bid - ask) / (bid + ask) volume over top `levels`; >0 = buy pressure.
    static double imbalance(const OrderBook& b, std::size_t levels = 5) {
        double bq = 0, aq = 0;
        for (auto& l : b.depth(Side::Buy, levels)) bq += double(l.qty);
        for (auto& l : b.depth(Side::Sell, levels)) aq += double(l.qty);
        return bq + aq > 0 ? (bq - aq) / (bq + aq) : 0.0;
    }
private:
    std::size_t window_;
    std::deque<Trade> trades_;
    double pv_ = 0, v_ = 0;
};

class FeedHandler {
public:
    // Returns the original message for `seq` (a replay/retransmission service).
    using Retransmit = std::function<bool(std::uint64_t, Msg&)>;

    FeedHandler(OrderBook& book, Retransmit rt, Analytics* an = nullptr, std::size_t tolerance = 16)
        : book_(book), rt_(std::move(rt)), an_(an), tol_(tolerance) {}

    void on_packet(const Msg& m) {
        if (m.seq < next_ || pending_.count(m.seq)) { ++st_.duplicates; return; }
        if (m.seq == next_) { apply(m); ++next_; drain(); return; }
        pending_.emplace(m.seq, m);
        ++st_.buffered;
        if (pending_.size() > tol_) recover_until(pending_.begin()->first);
    }
    // Heartbeat carrying the sender's last seq: lets us detect tail loss.
    void heartbeat(std::uint64_t last_seq) { recover_until(last_seq + 1); }

    const FeedStats& stats() const { return st_; }
    std::uint64_t next_expected() const { return next_; }

private:
    void apply(const Msg& m) {
        ++st_.applied;
        if (m.kind == Msg::Kind::Cancel) { book_.cancel(m.id); return; }
        trades_.clear();
        book_.submit(m.order, &trades_);
        if (an_) for (const auto& t : trades_) an_->on_trade(t);
    }
    void drain() {
        for (auto it = pending_.find(next_); it != pending_.end(); it = pending_.find(next_)) {
            apply(it->second);
            pending_.erase(it);
            ++next_;
        }
    }
    void recover_until(std::uint64_t upto) {  // fill [next_, upto) from the replay service
        while (next_ < upto) {
            auto it = pending_.find(next_);
            if (it != pending_.end()) { apply(it->second); pending_.erase(it); }
            else {
                Msg m;
                ++st_.retransmits;
                if (!rt_ || !rt_(next_, m)) break;  // unrecoverable: stop, do not skip silently
                ++st_.gaps_recovered;
                apply(m);
            }
            ++next_;
        }
        drain();
    }

    OrderBook& book_;
    Retransmit rt_;
    Analytics* an_;
    std::size_t tol_;
    std::uint64_t next_ = 1;
    std::map<std::uint64_t, Msg> pending_;
    std::vector<Trade> trades_;
    FeedStats st_;
};

}  // namespace lob
