#pragma once
// Translates Bloomberg-style L1 ticks (best bid/ask + trades) into sequenced feed
// messages. Pure C++ (no blpapi dependency) so it is unit-tested everywhere.
//
// L1 data has no order identity, so each side's quote is modelled as ONE synthetic
// resting order that is cancelled and replaced whenever the quote changes. A trade
// tick becomes an IOC limit order at the trade price, which consumes synthetic
// liquidity. Locked/crossed/invalid quotes are dropped (counted in dropped()).
// This is an L1 reconstruction, NOT a full depth book - say so when presenting it.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "lob/feed.hpp"

namespace lob {

class L1Translator {
public:
    explicit L1Translator(double tick_size = 0.01) : tick_(tick_size) {}

    // Any of the fields may be NaN/<=0 if absent in this update; the last valid
    // value for that field is kept (Bloomberg sends only changed fields).
    void on_quote(double bid, double bid_sz, double ask, double ask_sz, std::vector<Msg>& out) {
        const double ob = bid_, obs = bid_sz_, oa = ask_, oas = ask_sz_;
        // Evaluate both updates unconditionally (no short-circuit): each caches its own field.
        const bool bp = upd(bid_, bid), bs = upd(bid_sz_, bid_sz);
        const bool ap = upd(ask_, ask), as = upd(ask_sz_, ask_sz);
        const bool bid_changed = bp || bs, ask_changed = ap || as;
        if (!ok(bid_, bid_sz_) || !ok(ask_, ask_sz_)) return;
        if (to_ticks(bid_) >= to_ticks(ask_)) {  // locked/crossed: reject AND keep prior state
            bid_ = ob; bid_sz_ = obs; ask_ = oa; ask_sz_ = oas;
            ++dropped_;
            return;
        }
        if (bid_changed) requote(Side::Buy, bid_, bid_sz_, bid_id_, out);
        if (ask_changed) requote(Side::Sell, ask_, ask_sz_, ask_id_, out);
    }

    void on_trade(double price, double size, std::vector<Msg>& out) {
        if (!ok(price, size)) { ++dropped_; return; }
        if (!(last_mid_valid())) return;  // no book yet: nothing to trade against
        // Aggressor inferred from price vs mid; ties are treated as buys.
        Msg m;
        m.seq = ++seq_;
        m.order.id = next_id_++;
        m.order.type = OrderType::Limit;
        m.order.tif = TimeInForce::IOC;
        m.order.price = to_ticks(price);
        m.order.qty = to_qty(size);
        m.order.side = double(price) >= 0.5 * (bid_ + ask_) ? Side::Buy : Side::Sell;
        out.push_back(m);
    }

    Price to_ticks(double px) const { return Price(std::llround(px / tick_)); }
    std::uint64_t dropped() const { return dropped_; }
    std::uint64_t last_seq() const { return seq_; }

private:
    static bool ok(double a, double b) { return std::isfinite(a) && std::isfinite(b) && a > 0 && b > 0; }
    static bool upd(double& cur, double v) {
        if (!std::isfinite(v) || v <= 0 || v == cur) return false;
        cur = v;
        return true;
    }
    bool last_mid_valid() const { return bid_ > 0 && ask_ > 0 && bid_id_ && ask_id_; }
    Qty to_qty(double s) const { return Qty(std::max(1LL, std::llround(s))); }

    void requote(Side side, double px, double sz, OrderId& live, std::vector<Msg>& out) {
        if (live) {
            Msg c; c.kind = Msg::Kind::Cancel; c.seq = ++seq_; c.id = live;
            out.push_back(c);
        }
        Msg a;
        a.seq = ++seq_;
        a.order.id = live = next_id_++;
        a.order.side = side;
        a.order.type = OrderType::Limit;
        a.order.tif = TimeInForce::GTC;
        a.order.price = to_ticks(px);
        a.order.qty = to_qty(sz);
        out.push_back(a);
    }

    double tick_;
    double bid_ = 0, bid_sz_ = 0, ask_ = 0, ask_sz_ = 0;
    OrderId bid_id_ = 0, ask_id_ = 0, next_id_ = 1;
    std::uint64_t seq_ = 0, dropped_ = 0;
};

}  // namespace lob
