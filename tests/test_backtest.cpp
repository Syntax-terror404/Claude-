#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

#include "lob/backtest.hpp"
#include "lob/source.hpp"

#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
using namespace lob;

struct Script : Strategy {
    std::function<void(const View&, Gateway&)> f;
    explicit Script(std::function<void(const View&, Gateway&)> fn) : f(std::move(fn)) {}
    void on_event(const View& v, Gateway& gw) override { f(v, gw); }
};
static Msg add(std::uint64_t seq, OrderId id, Side s, Price p, Qty q, TimeInForce t = TimeInForce::GTC) {
    Msg m; m.seq = seq; m.order = Order{id, s, OrderType::Limit, p, q, t}; return m;
}

static void test_accounting() {
    // buy 10 @102 (taker), later sell 10 @103 (taker): +10 ticks gross, 2*10*0.3 = 6 fees
    std::vector<Msg> feed = {add(1, 1, Side::Sell, 102, 10), add(2, 2, Side::Buy, 100, 10),
                             add(3, 3, Side::Buy, 103, 10), add(4, 4, Side::Buy, 50, 1), add(5, 5, Side::Buy, 50, 1),
                             add(6, 6, Side::Buy, 50, 1), add(7, 7, Side::Buy, 50, 1)};
    Script s([](const View& v, Gateway& gw) {
        if (v.seq == 2) gw.send(Side::Buy, 102, 10, TimeInForce::IOC);
        if (v.seq == 4) gw.send(Side::Sell, 103, 10, TimeInForce::IOC);
    });
    BacktestConfig c; c.latency = 1; c.taker_fee = 0.3; c.sample_every = 1;
    Engine e(c);
    // feed order 3 (bid 103) would cross ask 102 before we buy: make the buy happen first
    std::swap(feed[2], feed[3]);  // seq stamps stay with the messages, so re-stamp
    for (std::size_t i = 0; i < feed.size(); ++i) feed[i].seq = i + 1;
    auto r = e.run(feed, s, true);
    CHECK(r.final_pos == 0);
    CHECK(r.taker_fills == 2 && r.maker_fills == 0);
    CHECK(std::fabs(r.gross_pnl - 10.0) < 1e-9);
    CHECK(std::fabs(r.fees - 6.0) < 1e-9);
    CHECK(std::fabs(r.net_pnl - 4.0) < 1e-9);
    CHECK(r.volume == 20);
}

static void test_latency() {
    std::vector<Msg> feed = {add(1, 1, Side::Sell, 102, 100), add(2, 2, Side::Buy, 100, 100),
                             add(3, 3, Side::Buy, 10, 1), add(4, 4, Side::Buy, 10, 1),
                             add(5, 5, Side::Buy, 10, 1), add(6, 6, Side::Buy, 10, 1)};
    Script s([](const View& v, Gateway& gw) { if (v.seq == 2) gw.send(Side::Buy, 102, 5, TimeInForce::IOC); });
    BacktestConfig c; c.latency = 3;
    auto r = Engine(c).run(feed, s, true);
    CHECK(r.trace.size() == 1 && r.trace[0].seq == 5);  // decided at 2, effective before msg 5
}

static void test_queue_position_and_maker_fee() {
    // our bid joins behind feed order 1 at the same price: must not fill until it is exhausted
    std::vector<Msg> feed = {add(1, 1, Side::Buy, 100, 5), add(2, 2, Side::Sell, 105, 50),
                             add(3, 3, Side::Sell, 100, 5, TimeInForce::IOC),   // eats order 1 only
                             add(4, 4, Side::Sell, 100, 3, TimeInForce::IOC)};  // reaches us
    Script s([](const View& v, Gateway& gw) { if (v.seq == 2) gw.send(Side::Buy, 100, 5, TimeInForce::GTC); });
    BacktestConfig c; c.latency = 1; c.maker_fee = -0.1;
    // our order arrives before msg 3, i.e. it is BEHIND order 1
    auto r = Engine(c).run(feed, s, true);
    CHECK(r.trace.size() == 1 && r.trace[0].seq == 4 && r.trace[0].qty == 3 && r.trace[0].maker);
    CHECK(r.final_pos == 3 && r.maker_fills == 1);
    CHECK(std::fabs(r.fees - (-0.3)) < 1e-9);  // rebate
}

static void test_position_limit() {
    SyntheticSource src(20000, 9);
    Script s([](const View& v, Gateway& gw) {
        if (v.valid && v.seq % 7 == 0) gw.send(Side::Buy, v.ask + 5, 50, TimeInForce::IOC);
    });
    BacktestConfig c; c.max_pos = 120;
    auto r = Engine(c).run(src.all(), s);
    CHECK(r.max_abs_pos <= 120);
    CHECK(r.rejects > 0);
}

static void test_determinism_and_prefix() {
    SyntheticSource src(30000, 4);
    BacktestConfig c; c.sample_every = 500;
    MarketMaker mm1, mm2, mm3;
    auto a = Engine(c).run(src.all(), mm1);
    auto b = Engine(c).run(src.all(), mm2);
    CHECK(a.equity == b.equity && a.fills == b.fills);  // bit-for-bit repeatable
    std::vector<Msg> prefix(src.all().begin(), src.all().begin() + 12000);
    auto p = Engine(c).run(prefix, mm3);
    CHECK(!p.equity.empty() && p.equity.size() <= a.equity.size());
    for (std::size_t i = 0; i < p.equity.size(); ++i) CHECK(p.equity[i] == a.equity[i]);  // no lookahead
}

static void test_strategies_run() {
    SyntheticSource src(100000, 2);
    MarketMaker mm;
    auto a = Engine().run(src.all(), mm);
    CHECK(a.fills > 0 && a.maker_fills > 0 && a.max_abs_pos <= 500);
    ImbalanceTaker it;
    auto b = Engine().run(src.all(), it);
    CHECK(b.taker_fills > 0);
    CHECK(std::fabs(b.gross_pnl - b.net_pnl - b.fees) < 1e-6);
}

int main() {
    test_accounting();
    test_latency();
    test_queue_position_and_maker_fee();
    test_position_limit();
    test_determinism_and_prefix();
    test_strategies_run();
    std::printf("backtest tests passed\n");
}
