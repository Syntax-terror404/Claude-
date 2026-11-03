#pragma once
// Source abstraction: anything that yields sequenced feed messages. The synthetic
// generator and the Bloomberg adapter implement the same interface, so the book,
// handler and analytics never know where data came from.
#include <optional>
#include <string>
#include <vector>

#include "lob/feed.hpp"

namespace lob {

class MarketDataSource {
public:
    virtual ~MarketDataSource() = default;
    virtual std::string name() const = 0;
    // Next message in sequence order, or nullopt at end of stream.
    virtual std::optional<Msg> next() = 0;
};

class SyntheticSource final : public MarketDataSource {
public:
    SyntheticSource(std::size_t n, unsigned seed) : feed_(generate_feed(n, seed)) {}
    std::string name() const override { return "synthetic"; }
    std::optional<Msg> next() override {
        if (i_ >= feed_.size()) return std::nullopt;
        return feed_[i_++];
    }
    const std::vector<Msg>& all() const { return feed_; }

private:
    std::vector<Msg> feed_;
    std::size_t i_ = 0;
};

}  // namespace lob
