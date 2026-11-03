#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "lob/bbg_translate.hpp"

#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
using namespace lob;
static const double NaN = std::numeric_limits<double>::quiet_NaN();

static void apply_msgs(OrderBook& b, const std::vector<Msg>& ms, std::vector<Trade>* t = nullptr) {
    for (auto& m : ms) {
        if (m.kind == Msg::Kind::Add) b.submit(m.order, t); else b.cancel(m.id);
    }
}

int main() {
    L1Translator tr(0.01);
    OrderBook book;
    std::vector<Msg> out;

    tr.on_quote(100.00, 500, 100.05, 300, out);  // initial two-sided quote
    CHECK(out.size() == 2);
    apply_msgs(book, out);
    Price p;
    CHECK(book.best_bid(p) && p == 10000);
    CHECK(book.best_ask(p) && p == 10005);

    out.clear();
    tr.on_quote(100.01, NaN, NaN, NaN, out);  // partial update: only bid price changed
    CHECK(out.size() == 2 && out[0].kind == Msg::Kind::Cancel && out[1].kind == Msg::Kind::Add);
    CHECK(out[1].order.qty == 500);  // kept last valid bid size
    apply_msgs(book, out);
    CHECK(book.best_bid(p) && p == 10001);
    CHECK(book.depth(Side::Buy, 5).size() == 1);  // old quote really removed

    out.clear();
    tr.on_quote(100.06, NaN, NaN, NaN, out);  // would cross the ask: dropped, book untouched
    CHECK(out.empty() && tr.dropped() == 1);

    out.clear();
    std::vector<Trade> t;
    tr.on_trade(100.05, 100, out);  // at the ask => buy aggressor
    CHECK(out.size() == 1 && out[0].order.side == Side::Buy && out[0].order.tif == TimeInForce::IOC);
    apply_msgs(book, out, &t);
    CHECK(t.size() == 1 && t[0].qty == 100 && t[0].price == 10005);
    CHECK(book.depth(Side::Sell, 1)[0].qty == 200);

    out.clear();
    tr.on_trade(-1, 10, out);  // invalid trade
    CHECK(out.empty());

    // sequence numbers are gapless and increasing across all emitted messages
    CHECK(tr.last_seq() == 2 + 2 + 1);
    std::printf("bbg translator tests passed\n");
}
