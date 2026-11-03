#pragma once
// Bloomberg BLPAPI market-data source. Compiled only with -DLOB_WITH_BLPAPI=ON and
// the BLPAPI C++ SDK from Bloomberg's own distribution. NOT verified against a live
// Terminal from the development container: expect to debug on first run in the lab.
#include <deque>
#include <memory>
#include <string>

#include "lob/bbg_translate.hpp"
#include "lob/source.hpp"

namespace lob {

struct BlpapiConfig {
    std::string host = "localhost";
    unsigned short port = 8194;       // Desktop API (logged-in Terminal)
    std::string security = "IBM US Equity";
    double tick_size = 0.01;
    int event_timeout_ms = 5000;
};

class BlpapiSource final : public MarketDataSource {
public:
    explicit BlpapiSource(BlpapiConfig cfg);
    ~BlpapiSource() override;
    std::string name() const override { return "blpapi:" + cfg_.security; }
    // Blocks up to event_timeout_ms for the next update; nullopt on timeout/session end.
    std::optional<Msg> next() override;

private:
    struct Impl;
    BlpapiConfig cfg_;
    L1Translator tr_;
    std::deque<Msg> q_;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lob
