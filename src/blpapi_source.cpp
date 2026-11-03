#include "blpapi_source.hpp"

#include <blpapi_event.h>
#include <blpapi_message.h>
#include <blpapi_session.h>
#include <blpapi_sessionoptions.h>
#include <blpapi_subscriptionlist.h>

#include <cmath>
#include <limits>
#include <stdexcept>

namespace lob {

using namespace BloombergLP;

struct BlpapiSource::Impl {
    blpapi::SessionOptions opts;
    std::unique_ptr<blpapi::Session> session;
};

BlpapiSource::BlpapiSource(BlpapiConfig cfg)
    : cfg_(std::move(cfg)), tr_(cfg_.tick_size), impl_(new Impl) {
    impl_->opts.setServerHost(cfg_.host.c_str());
    impl_->opts.setServerPort(cfg_.port);
    impl_->session.reset(new blpapi::Session(impl_->opts));
    if (!impl_->session->start()) throw std::runtime_error("blpapi: session start failed (is the Terminal logged in?)");
    if (!impl_->session->openService("//blp/mktdata")) throw std::runtime_error("blpapi: cannot open //blp/mktdata");

    blpapi::SubscriptionList subs;
    subs.add(cfg_.security.c_str(), "BID,ASK,BID_SIZE,ASK_SIZE,LAST_PRICE,SIZE_LAST_TRADE",
             "", blpapi::CorrelationId(1));
    impl_->session->subscribe(subs);
}

BlpapiSource::~BlpapiSource() { if (impl_ && impl_->session) impl_->session->stop(); }

static double get(const blpapi::Message& m, const char* f) {
    return m.hasElement(f) ? m.getElementAsFloat64(f) : std::numeric_limits<double>::quiet_NaN();
}

std::optional<Msg> BlpapiSource::next() {
    while (q_.empty()) {
        blpapi::Event ev = impl_->session->nextEvent(cfg_.event_timeout_ms);
        if (ev.eventType() == blpapi::Event::TIMEOUT) return std::nullopt;
        if (ev.eventType() == blpapi::Event::SESSION_STATUS) {
            blpapi::MessageIterator it(ev);
            while (it.next())
                if (std::string(it.message().messageType().string()) == "SessionTerminated") return std::nullopt;
            continue;
        }
        if (ev.eventType() != blpapi::Event::SUBSCRIPTION_DATA) continue;
        blpapi::MessageIterator it(ev);
        while (it.next()) {
            const blpapi::Message& m = it.message();
            std::vector<Msg> out;
            // Quote fields and trade fields arrive in separate updates; the translator
            // keeps the last valid value of each, so partial updates are fine.
            double bid = get(m, "BID"), ask = get(m, "ASK");
            double bsz = get(m, "BID_SIZE"), asz = get(m, "ASK_SIZE");
            if (std::isfinite(bid) || std::isfinite(ask) || std::isfinite(bsz) || std::isfinite(asz))
                tr_.on_quote(bid, bsz, ask, asz, out);
            double lp = get(m, "LAST_PRICE"), ls = get(m, "SIZE_LAST_TRADE");
            if (std::isfinite(lp) && std::isfinite(ls)) tr_.on_trade(lp, ls, out);
            for (auto& x : out) q_.push_back(x);
        }
    }
    Msg m = q_.front();
    q_.pop_front();
    return m;
}

}  // namespace lob
