// Consumes wire packets from a BlazingMQ queue into the feed handler + order book, then checks
// the resulting book against a golden book built from the same deterministic feed.
// Usage: bmq_consume <queue-uri> <expected-messages> [seed] [broker-uri] [idle-timeout-s]
// Built only with -DLOB_WITH_BMQ=ON.
#include <bdlbb_blob.h>
#include <bdlbb_blobutil.h>
#include <bdlbb_pooledblobbufferfactory.h>
#include <bmqa_messageevent.h>
#include <bmqa_messageiterator.h>
#include <bmqa_session.h>
#include <bmqt_sessioneventtype.h>
#include <bmqt_queueflags.h>
#include <bmqt_sessionoptions.h>
#include <bmqt_uri.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "lob/source.hpp"
#include "lob/wire.hpp"

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s <queue-uri> <expected> [seed] [broker] [idle-s]\n", argv[0]); return 2; }
    const std::string uri = argv[1];
    const std::size_t expected = std::strtoull(argv[2], nullptr, 10);
    const unsigned seed = argc > 3 ? unsigned(std::strtoul(argv[3], nullptr, 10)) : 42;
    const std::string broker = argc > 4 ? argv[4] : "tcp://localhost:30114";
    const double idle_s = argc > 5 ? std::atof(argv[5]) : 10.0;

    BloombergLP::bmqt::SessionOptions opts;
    opts.setBrokerUri(broker.c_str());
    BloombergLP::bmqa::Session session(opts);
    if (session.start() != 0) { std::fprintf(stderr, "session start failed\n"); return 1; }
    BloombergLP::bmqa::QueueId qid;
    auto st = session.openQueueSync(&qid, BloombergLP::bmqt::Uri(uri.c_str()), BloombergLP::bmqt::QueueFlags::e_READ);
    if (!st) { std::fprintf(stderr, "open queue failed: %d\n", int(st.result())); session.stop(); return 1; }

    // Reference: the same deterministic feed, applied in order, is the golden book.
    lob::SyntheticSource src(expected, seed);
    const auto& feed = src.all();
    lob::OrderBook golden;
    for (auto& m : feed) { if (m.kind == lob::Msg::Kind::Add) golden.submit(m.order); else golden.cancel(m.id); }

    lob::OrderBook book;
    lob::FeedHandler fh(book, [&](std::uint64_t s, lob::Msg& out) { out = feed[s - 1]; return true; });
    BloombergLP::bdlbb::PooledBlobBufferFactory factory(256);
    std::size_t received = 0, bad = 0;

    while (fh.next_expected() <= expected) {
        auto ev = session.nextEvent(BloombergLP::bsls::TimeInterval(idle_s));
        if (ev.isSessionEvent()) {
            if (ev.sessionEvent().type() == BloombergLP::bmqt::SessionEventType::e_TIMEOUT) {
                std::fprintf(stderr, "idle timeout (%.0fs); stopping\n", idle_s);
                break;
            }
            continue;  // connection/queue status events are not end-of-stream
        }
        if (!ev.isMessageEvent()) continue;
        auto me = ev.messageEvent();
        if (me.type() != BloombergLP::bmqt::MessageEventType::e_PUSH) continue;
        auto it = me.messageIterator();
        while (it.nextMessage()) {
            const auto& m = it.message();
            BloombergLP::bdlbb::Blob blob(&factory);
            m.getData(&blob);
            std::uint8_t buf[256];
            const std::size_t len = std::size_t(blob.length());
            lob::wire::Decoded d;
            if (len > sizeof buf) { ++bad; }
            else {
                BloombergLP::bdlbb::BlobUtil::copy(reinterpret_cast<char*>(buf), blob, 0, int(len));
                if (!lob::wire::decode(buf, len, d)) ++bad;
                else if (d.kind != lob::wire::Kind::Heartbeat) fh.on_packet(d.msg);
            }
            ++received;
            session.confirmMessage(m);
        }
    }

    const bool applied_all = fh.next_expected() == expected + 1 && fh.stats().applied == expected;
    const bool same = book.resting_orders() == golden.resting_orders();
    lob::Price a, b;
    const bool bid_same = book.best_bid(a) == golden.best_bid(b) && (!book.best_bid(a) || a == b);
    const bool ask_same = book.best_ask(a) == golden.best_ask(b) && (!book.best_ask(a) || a == b);
    std::printf("received=%zu bad=%zu applied=%llu dup_dropped=%llu retransmits=%llu resting=%zu/%zu bid_same=%d ask_same=%d\n",
                received, bad, (unsigned long long)fh.stats().applied, (unsigned long long)fh.stats().duplicates,
                (unsigned long long)fh.stats().retransmits, book.resting_orders(), golden.resting_orders(),
                int(bid_same), int(ask_same));
    const bool ok = applied_all && same && bid_same && ask_same && bad == 0;
    std::printf("%s\n", ok ? "MATCH: book rebuilt from the queue equals the golden book" : "MISMATCH");
    session.closeQueueSync(&qid);
    session.stop();
    return ok ? 0 : 1;
}
