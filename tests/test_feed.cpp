// End-to-end property: despite loss, duplication and reordering, the handler's
// book must be IDENTICAL to a book fed the clean in-order stream.
#include <cstdio>

#include "lob/feed.hpp"

using namespace lob;

static bool same(const OrderBook& a, const OrderBook& b) {
    for (Side s : {Side::Buy, Side::Sell}) {
        auto x = a.depth(s, 1000000), y = b.depth(s, 1000000);
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (x[i].price != y[i].price || x[i].qty != y[i].qty || x[i].orders != y[i].orders)
                return false;
    }
    return a.resting_orders() == b.resting_orders();
}

int main() {
    int failures = 0;
    for (unsigned seed = 1; seed <= 10; ++seed) {
        auto feed = generate_feed(50000, seed);
        OrderBook golden;
        for (const auto& m : feed) {
            if (m.kind == Msg::Kind::Cancel) golden.cancel(m.id); else golden.submit(m.order);
        }
        ChannelConfig cc;
        cc.seed = seed * 31;
        cc.loss = 0.01 * seed;  // up to 10% loss
        auto wire = impair(feed, cc);

        OrderBook book;
        FeedHandler h(book, [&](std::uint64_t s, Msg& out) { out = feed[s - 1]; return true; });
        for (const auto& m : wire) h.on_packet(m);
        h.heartbeat(feed.back().seq);

        bool ok = same(golden, book) && h.next_expected() == feed.size() + 1 &&
                  h.stats().applied == feed.size();
        if (!ok) { std::printf("FAIL seed %u\n", seed); ++failures; }
    }
    // Unrecoverable gap must stall, never silently skip.
    {
        auto feed = generate_feed(100, 1);
        OrderBook b;
        FeedHandler h(b, [](std::uint64_t, Msg&) { return false; });
        h.on_packet(feed[1]);  // seq 2, seq 1 missing
        h.heartbeat(2);
        if (h.next_expected() != 1) { std::printf("FAIL stall\n"); ++failures; }
    }
    if (failures) return 1;
    std::printf("feed tests passed\n");
}
