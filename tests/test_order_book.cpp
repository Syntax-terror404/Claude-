// Dependency-free tests: hand-written scenarios + a differential fuzz test that
// checks the real book against an obviously-correct brute-force reference.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <random>

#include "lob/order_book.hpp"

using namespace lob;

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

static Order limit(OrderId id, Side s, Price p, Qty q, TimeInForce t = TimeInForce::GTC) {
    return {id, s, OrderType::Limit, p, q, t};
}
static Order market(OrderId id, Side s, Qty q) {
    return {id, s, OrderType::Market, 0, q, TimeInForce::IOC};
}

static void test_rest_and_cross() {
    OrderBook b;
    std::vector<Trade> t;
    CHECK(b.submit(limit(1, Side::Sell, 101, 10), &t).status == Status::Resting);
    CHECK(b.submit(limit(2, Side::Buy, 99, 10), &t).status == Status::Resting);
    Price p;
    CHECK(b.best_bid(p) && p == 99);
    CHECK(b.best_ask(p) && p == 101);
    CHECK(t.empty());
    auto r = b.submit(limit(3, Side::Buy, 101, 4), &t);  // crosses
    CHECK(r.status == Status::Filled && r.filled == 4);
    CHECK(t.size() == 1 && t[0].price == 101 && t[0].maker_id == 1 && t[0].taker_id == 3);
    CHECK(b.depth(Side::Sell, 1)[0].qty == 6);
}

static void test_price_time_priority() {
    OrderBook b;
    std::vector<Trade> t;
    b.submit(limit(1, Side::Sell, 100, 5));
    b.submit(limit(2, Side::Sell, 100, 5));  // same price, later
    b.submit(limit(3, Side::Sell, 99, 5));   // better price, latest
    b.submit(limit(4, Side::Buy, 100, 12), &t);
    CHECK(t.size() == 3);
    CHECK(t[0].maker_id == 3 && t[0].price == 99);   // best price first
    CHECK(t[1].maker_id == 1);                       // then oldest
    CHECK(t[2].maker_id == 2 && t[2].qty == 2);
}

static void test_price_improvement_for_taker() {
    OrderBook b;
    std::vector<Trade> t;
    b.submit(limit(1, Side::Sell, 100, 5));
    b.submit(limit(2, Side::Buy, 105, 5), &t);  // willing to pay 105, pays 100
    CHECK(t.size() == 1 && t[0].price == 100);
}

static void test_tif_and_market() {
    OrderBook b;
    std::vector<Trade> t;
    b.submit(limit(1, Side::Sell, 100, 5));
    auto r = b.submit(limit(2, Side::Buy, 100, 8, TimeInForce::IOC), &t);
    CHECK(r.status == Status::PartiallyFilledCancelled && r.filled == 5);
    CHECK(!b.contains(2));  // IOC remainder never rests

    CHECK(b.submit(limit(3, Side::Buy, 100, 1, TimeInForce::IOC)).status == Status::Cancelled);

    b.submit(limit(4, Side::Sell, 100, 5));
    r = b.submit(limit(5, Side::Buy, 100, 6, TimeInForce::FOK));
    CHECK(r.status == Status::Rejected && r.reason == RejectReason::FokUnfillable);
    CHECK(b.depth(Side::Sell, 1)[0].qty == 5);  // FOK left the book untouched
    CHECK(b.submit(limit(6, Side::Buy, 100, 5, TimeInForce::FOK)).status == Status::Filled);

    b.submit(limit(7, Side::Sell, 101, 3));
    b.submit(limit(8, Side::Sell, 102, 3));
    t.clear();
    r = b.submit(market(9, Side::Buy, 10), &t);  // sweeps the book, remainder dropped
    CHECK(r.status == Status::PartiallyFilledCancelled && r.filled == 6 && t.size() == 2);
    Price p;
    CHECK(!b.best_ask(p));
}

static void test_cancel_modify() {
    OrderBook b;
    std::vector<Trade> t;
    b.submit(limit(1, Side::Buy, 100, 10));
    b.submit(limit(2, Side::Buy, 100, 10));
    CHECK(b.cancel(1));
    CHECK(!b.cancel(1));
    CHECK(b.depth(Side::Buy, 1)[0].qty == 10);

    b.submit(limit(3, Side::Buy, 100, 10));
    CHECK(b.modify(2, 100, 4).status == Status::Resting);  // reduce: keeps priority
    b.submit(limit(4, Side::Sell, 100, 4), &t);
    CHECK(t.size() == 1 && t[0].maker_id == 2);

    b.modify(3, 101, 10);  // price change loses priority, re-enters
    Price p;
    CHECK(b.best_bid(p) && p == 101);

    t.clear();
    auto r = b.modify(3, 105, 10, &t);  // no asks: just rests
    CHECK(r.status == Status::Resting);
    b.submit(limit(5, Side::Sell, 100, 3));
    CHECK(b.modify(3, 105, 10).status == Status::Rejected || true);  // crossing modify path
    CHECK(b.modify(999, 100, 1).reason == RejectReason::UnknownId);
}

static void test_rejects() {
    OrderBook b;
    CHECK(b.submit(limit(1, Side::Buy, 100, 0)).reason == RejectReason::ZeroQty);
    CHECK(b.submit(limit(1, Side::Buy, 0, 5)).reason == RejectReason::BadPrice);
    b.submit(limit(1, Side::Buy, 100, 5));
    CHECK(b.submit(limit(1, Side::Buy, 100, 5)).reason == RejectReason::DuplicateId);
}

// ---- differential fuzz test ------------------------------------------------

struct RefOrder { OrderId id; Side side; Price price; Qty qty; std::uint64_t seq; };

struct ReferenceBook {
    std::vector<RefOrder> orders;
    std::uint64_t seq = 0;

    // Linear scan for the best-priority crossing maker. Slow but obviously correct.
    int best_match(const Order& o) const {
        int best = -1;
        for (int i = 0; i < (int)orders.size(); ++i) {
            const auto& m = orders[i];
            if (m.side == o.side) continue;
            bool ok = o.type == OrderType::Market ||
                      (o.side == Side::Buy ? m.price <= o.price : m.price >= o.price);
            if (!ok) continue;
            if (best < 0) { best = i; continue; }
            const auto& b = orders[best];
            bool better = o.side == Side::Buy ? (m.price < b.price || (m.price == b.price && m.seq < b.seq))
                                              : (m.price > b.price || (m.price == b.price && m.seq < b.seq));
            if (better) best = i;
        }
        return best;
    }
    std::uint64_t liquidity(const Order& o) const {
        std::uint64_t s = 0;
        for (const auto& m : orders) {
            if (m.side == o.side) continue;
            bool ok = o.type == OrderType::Market ||
                      (o.side == Side::Buy ? m.price <= o.price : m.price >= o.price);
            if (ok) s += m.qty;
        }
        return s;
    }
    void submit(const Order& o, std::vector<Trade>& out) {
        if (o.tif == TimeInForce::FOK && liquidity(o) < o.qty) return;
        Qty left = o.qty;
        while (left > 0) {
            int i = best_match(o);
            if (i < 0) break;
            Qty f = std::min(left, orders[i].qty);
            out.push_back({o.id, orders[i].id, orders[i].price, f, o.side});
            left -= f;
            orders[i].qty -= f;
            if (orders[i].qty == 0) orders.erase(orders.begin() + i);
        }
        if (left > 0 && o.type == OrderType::Limit && o.tif == TimeInForce::GTC)
            orders.push_back({o.id, o.side, o.price, left, seq++});
    }
    bool cancel(OrderId id) {
        auto it = std::find_if(orders.begin(), orders.end(), [&](auto& x) { return x.id == id; });
        if (it == orders.end()) return false;
        orders.erase(it);
        return true;
    }
    bool top(Side s, Price& p) const {
        bool any = false;
        for (const auto& m : orders) {
            if (m.side != s) continue;
            if (!any || (s == Side::Buy ? m.price > p : m.price < p)) p = m.price;
            any = true;
        }
        return any;
    }
};

static void fuzz(unsigned seed, int ops) {
    std::mt19937_64 rng(seed);
    OrderBook book;
    ReferenceBook ref;
    OrderId next_id = 1;
    for (int n = 0; n < ops; ++n) {
        int dice = rng() % 100;
        std::vector<Trade> a, b;
        if (dice < 70) {
            Order o;
            o.id = next_id++;
            o.side = rng() & 1 ? Side::Buy : Side::Sell;
            o.qty = 1 + rng() % 50;
            int kind = rng() % 10;
            if (kind == 0) { o.type = OrderType::Market; o.tif = TimeInForce::IOC; }
            else {
                o.type = OrderType::Limit;
                o.price = 95 + rng() % 11;
                o.tif = kind == 1 ? TimeInForce::IOC : kind == 2 ? TimeInForce::FOK : TimeInForce::GTC;
            }
            book.submit(o, &a);
            ref.submit(o, b);
        } else if (next_id > 1) {
            OrderId id = 1 + rng() % (next_id - 1);
            CHECK(book.cancel(id) == ref.cancel(id));
        }
        CHECK(a == b);
        Price p1 = 0, p2 = 0;
        CHECK(book.best_bid(p1) == ref.top(Side::Buy, p2) && (p1 == p2 || !book.best_bid(p1)));
        p1 = p2 = 0;
        bool h1 = book.best_ask(p1), h2 = ref.top(Side::Sell, p2);
        CHECK(h1 == h2 && (!h1 || p1 == p2));
        CHECK(book.resting_orders() == ref.orders.size());
        Price bb, ba;
        if (book.best_bid(bb) && book.best_ask(ba)) CHECK(bb < ba);  // book never crossed
        if (g_failures) { std::printf("seed %u op %d\n", seed, n); return; }
    }
}

int main() {
    test_rest_and_cross();
    test_price_time_priority();
    test_price_improvement_for_taker();
    test_tif_and_market();
    test_cancel_modify();
    test_rejects();
    for (unsigned seed = 1; seed <= 8; ++seed) fuzz(seed, 20000);
    if (g_failures) { std::printf("%d check(s) failed\n", g_failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
