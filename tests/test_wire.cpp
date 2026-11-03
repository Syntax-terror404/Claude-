#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

#include "lob/source.hpp"
#include "lob/transport.hpp"

#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
using namespace lob;

static bool same(const Msg& a, const Msg& b) {
    if (a.kind != b.kind || a.seq != b.seq) return false;
    if (a.kind == Msg::Kind::Cancel) return a.id == b.id;
    return a.order.id == b.order.id && a.order.side == b.order.side && a.order.type == b.order.type &&
           a.order.price == b.order.price && a.order.qty == b.order.qty && a.order.tif == b.order.tif;
}

static void test_roundtrip_and_corruption() {
    SyntheticSource src(5000, 3);
    std::mt19937_64 rng(11);
    for (const Msg& m : src.all()) {
        auto p = wire::encode_msg(m);
        wire::Decoded d;
        CHECK(wire::decode(p.data(), p.size(), d));
        CHECK(same(m, d.msg));
        // any single flipped bit must be rejected by CRC/field validation
        auto q = p;
        q[rng() % q.size()] ^= std::uint8_t(1u << (rng() % 8));
        CHECK(!wire::decode(q.data(), q.size(), d));
        CHECK(!wire::decode(p.data(), p.size() - 1, d));  // truncated
    }
    auto hb = wire::encode_heartbeat(777);
    wire::Decoded d;
    CHECK(wire::decode(hb.data(), hb.size(), d) && d.kind == wire::Kind::Heartbeat && d.last_seq == 777);
}

// Full path over real loopback UDP: encode -> impaired send -> recv/decode -> handler -> book.
static void test_udp_pipeline(unsigned seed, double loss) {
    const std::size_t n = 20000;
    SyntheticSource src(n, seed);
    const auto& feed = src.all();

    OrderBook golden;
    for (auto& m : feed) {
        if (m.kind == Msg::Kind::Add) golden.submit(m.order); else golden.cancel(m.id);
    }

    ChannelConfig cc; cc.loss = loss; cc.seed = seed + 100;
    auto wirepkts = impair(feed, cc);

    UdpSocket rx, tx;
    auto port = rx.bind("127.0.0.1", 0);
    tx.connect("127.0.0.1", port);

    OrderBook book;
    Analytics an;
    FeedHandler fh(book, [&](std::uint64_t s, Msg& out) { out = feed[s - 1]; return true; }, &an);

    std::uint8_t buf[256];
    std::uint64_t corrupted = 0;
    auto drain = [&]() {
        while (std::size_t len = rx.recv(buf, sizeof buf, 0)) {
            wire::Decoded d;
            if (!wire::decode(buf, len, d)) { ++corrupted; continue; }
            if (d.kind == wire::Kind::Heartbeat) fh.heartbeat(d.last_seq);
            else fh.on_packet(d.msg);
        }
    };
    std::size_t sent = 0;
    for (auto& m : wirepkts) {
        CHECK(tx.send(wire::encode_msg(m)));
        if (++sent % 64 == 0) drain();  // keep the socket buffer from overflowing
    }
    // garbage datagrams must be rejected, not crash or poison the book
    const std::uint8_t junk[7] = {1, 2, 3, 4, 5, 6, 7};
    CHECK(tx.send_raw(junk, sizeof junk));
    CHECK(tx.send(wire::encode_heartbeat(n)));  // tail-loss detection
    for (int i = 0; i < 50; ++i) { rx.recv(buf, 0, 1); drain(); }
    drain();

    CHECK(corrupted == 1);
    CHECK(fh.next_expected() == n + 1);
    CHECK(fh.stats().applied == n);
    CHECK(book.resting_orders() == golden.resting_orders());
    Price a, b, c, e;
    CHECK(book.best_bid(a) == golden.best_bid(b) && (a == b || !book.best_bid(a)));
    CHECK(book.best_ask(c) == golden.best_ask(e) && c == e);
    CHECK(book.depth(Side::Buy, 10).size() == golden.depth(Side::Buy, 10).size());
}

int main() {
    test_roundtrip_and_corruption();
    for (unsigned s = 1; s <= 5; ++s) test_udp_pipeline(s, 0.01 * s);
    std::printf("wire tests passed\n");
}
