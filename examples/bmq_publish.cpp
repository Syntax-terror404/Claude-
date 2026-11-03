// Publishes the deterministic synthetic feed to a BlazingMQ queue as 40-byte wire packets.
// Usage: bmq_publish <queue-uri> <messages> [seed] [broker-uri] [messages-per-event]
// Built only with -DLOB_WITH_BMQ=ON.
#include <bmqa_messageevent.h>
#include <bmqa_messageeventbuilder.h>
#include <bmqa_messageiterator.h>
#include <bmqa_session.h>
#include <bmqt_sessioneventtype.h>
#include <bmqt_correlationid.h>
#include <bmqt_queueflags.h>
#include <bmqt_sessionoptions.h>
#include <bmqt_uri.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "lob/source.hpp"
#include "lob/wire.hpp"

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s <queue-uri> <messages> [seed] [broker] [per-event]\n", argv[0]); return 2; }
    const std::string uri = argv[1];
    const std::size_t n = std::strtoull(argv[2], nullptr, 10);
    const unsigned seed = argc > 3 ? unsigned(std::strtoul(argv[3], nullptr, 10)) : 42;
    const std::string broker = argc > 4 ? argv[4] : "tcp://localhost:30114";
    const std::size_t per_event = argc > 5 ? std::strtoull(argv[5], nullptr, 10) : 100;

    BloombergLP::bmqt::SessionOptions opts;
    opts.setBrokerUri(broker.c_str());
    BloombergLP::bmqa::Session session(opts);
    if (session.start() != 0) { std::fprintf(stderr, "session start failed (is the broker up on %s?)\n", broker.c_str()); return 1; }

    BloombergLP::bmqa::QueueId qid;
    auto st = session.openQueueSync(&qid, BloombergLP::bmqt::Uri(uri.c_str()),
                                    BloombergLP::bmqt::QueueFlags::e_WRITE | BloombergLP::bmqt::QueueFlags::e_ACK);
    if (!st) { std::fprintf(stderr, "open queue failed: %d\n", int(st.result())); session.stop(); return 1; }

    lob::SyntheticSource src(n, seed);
    BloombergLP::bmqa::MessageEventBuilder builder;
    session.loadMessageEventBuilder(&builder);

    std::size_t posted = 0, acked = 0, failed_acks = 0, in_event = 0;
    // Handles ack events until `done()` or a real timeout. Session events other than TIMEOUT are skipped.
    auto pump = [&](double timeout_s, auto&& done) {
        while (!done()) {
            auto ev = session.nextEvent(BloombergLP::bsls::TimeInterval(timeout_s));
            if (ev.isSessionEvent()) {
                if (ev.sessionEvent().type() == BloombergLP::bmqt::SessionEventType::e_TIMEOUT) return false;
                continue;
            }
            if (!ev.isMessageEvent()) continue;
            auto me = ev.messageEvent();
            if (me.type() != BloombergLP::bmqt::MessageEventType::e_ACK) continue;
            auto it = me.messageIterator();
            while (it.nextMessage()) { if (it.message().ackStatus() == 0) ++acked; else ++failed_acks; }
        }
        return true;
    };
    auto flush = [&]() {
        if (!in_event) return;
        if (session.post(builder.messageEvent()) != 0) { std::fprintf(stderr, "post failed\n"); std::exit(1); }
        builder.reset();
        in_event = 0;
    };
    const std::size_t window = 500;  // max unacked messages; the broker's queue limit is 1000 in this test domain
    for (const lob::Msg& m : src.all()) {
        if (in_event == 0 && posted - (acked + failed_acks) >= window)
            pump(5.0, [&] { return posted - (acked + failed_acks) < window / 2; });  // backpressure
        auto pkt = lob::wire::encode_msg(m);
        auto& msg = builder.startMessage();
        msg.setDataRef(reinterpret_cast<const char*>(pkt.data()), pkt.size());
        msg.setCorrelationId(BloombergLP::bmqt::CorrelationId(int(posted + 1)));  // required when ACKs are on
        if (int rc = builder.packMessage(qid); rc != 0) { std::fprintf(stderr, "pack failed rc=%d at message %zu\n", rc, posted); return 1; }
        ++posted;
        if (++in_event >= per_event) { flush(); }
    }
    flush();
    pump(10.0, [&] { return acked + failed_acks >= posted; });

    std::printf("published=%zu acked=%zu failed_acks=%zu queue=%s seed=%u\n", posted, acked, failed_acks, uri.c_str(), seed);
    session.closeQueueSync(&qid);
    session.stop();
    return (acked == posted && failed_acks == 0) ? 0 : 1;
}
