#include "lob/order_book.hpp"

#include <algorithm>

namespace lob {

bool OrderBook::crosses(Side taker, Price limit, Price level_price, OrderType t) {
    if (t == OrderType::Market) return true;
    return taker == Side::Buy ? level_price <= limit : level_price >= limit;
}

template <class Opp>
std::uint64_t OrderBook::available(const Opp& opp, const Order& o) {
    std::uint64_t sum = 0;
    for (const auto& [price, level] : opp) {
        if (!crosses(o.side, o.price, price, o.type)) break;
        sum += level.total;
        if (sum >= o.qty) break;
    }
    return sum;
}

template <class Opp>
Qty OrderBook::match(Opp& opp, const Order& o, Qty qty, std::vector<Trade>* trades) {
    Qty filled = 0;
    while (qty > 0 && !opp.empty()) {
        auto level_it = opp.begin();  // best price on the opposite side
        if (!crosses(o.side, o.price, level_it->first, o.type)) break;
        Level& level = level_it->second;
        while (qty > 0 && !level.queue.empty()) {
            Resting& maker = level.queue.front();  // oldest order first: time priority
            const Qty fill = std::min(qty, maker.qty);
            if (trades) trades->push_back({o.id, maker.id, level_it->first, fill, o.side});
            maker.qty -= fill;
            level.total -= fill;
            qty -= fill;
            filled += fill;
            if (maker.qty == 0) {
                index_.erase(maker.id);
                level.queue.pop_front();
            }
        }
        if (level.queue.empty()) opp.erase(level_it);
    }
    return filled;
}

template <class Book>
void OrderBook::rest(Book& book, const Order& o, Qty qty) {
    Level& level = book[o.price];
    level.queue.push_back({o.id, qty});
    level.total += qty;
    index_[o.id] = {o.side, o.price, std::prev(level.queue.end())};
}

template <class Book>
void OrderBook::erase(Book& book, const Locator& loc) {
    auto level_it = book.find(loc.price);
    Level& level = level_it->second;
    level.total -= loc.it->qty;
    level.queue.erase(loc.it);
    if (level.queue.empty()) book.erase(level_it);
}

template <class Opp, class Own>
Result OrderBook::process(Opp& opp, Own& own, const Order& o, std::vector<Trade>* trades) {
    if (o.tif == TimeInForce::FOK && available(opp, o) < o.qty)
        return {Status::Rejected, RejectReason::FokUnfillable, 0};

    const Qty filled = match(opp, o, o.qty, trades);
    const Qty left = o.qty - filled;
    if (left == 0) return {Status::Filled, RejectReason::None, filled};

    const bool can_rest = o.type == OrderType::Limit && o.tif == TimeInForce::GTC;
    if (can_rest) {
        rest(own, o, left);
        return {Status::Resting, RejectReason::None, filled};
    }
    return {filled ? Status::PartiallyFilledCancelled : Status::Cancelled,
            RejectReason::None, filled};
}

Result OrderBook::submit(const Order& o, std::vector<Trade>* trades) {
    if (o.qty == 0) return {Status::Rejected, RejectReason::ZeroQty, 0};
    if (o.type == OrderType::Limit && o.price <= 0)
        return {Status::Rejected, RejectReason::BadPrice, 0};
    if (index_.count(o.id)) return {Status::Rejected, RejectReason::DuplicateId, 0};
    return o.side == Side::Buy ? process(asks_, bids_, o, trades)
                               : process(bids_, asks_, o, trades);
}

bool OrderBook::cancel(OrderId id) {
    auto it = index_.find(id);
    if (it == index_.end()) return false;
    if (it->second.side == Side::Buy) erase(bids_, it->second);
    else erase(asks_, it->second);
    index_.erase(it);
    return true;
}

Result OrderBook::modify(OrderId id, Price new_price, Qty new_qty,
                         std::vector<Trade>* trades) {
    auto it = index_.find(id);
    if (it == index_.end()) return {Status::Rejected, RejectReason::UnknownId, 0};
    if (new_qty == 0) return {Status::Rejected, RejectReason::ZeroQty, 0};
    if (new_price <= 0) return {Status::Rejected, RejectReason::BadPrice, 0};

    const Locator loc = it->second;
    if (new_price == loc.price && new_qty <= loc.it->qty) {
        // In-place reduction: keeps time priority.
        auto adjust = [&](auto& book) {
            book.find(loc.price)->second.total -= loc.it->qty - new_qty;
        };
        if (loc.side == Side::Buy) adjust(bids_); else adjust(asks_);
        loc.it->qty = new_qty;
        return {Status::Resting, RejectReason::None, 0};
    }
    cancel(id);
    return submit({id, loc.side, OrderType::Limit, new_price, new_qty, TimeInForce::GTC},
                  trades);
}

bool OrderBook::best_bid(Price& p) const {
    if (bids_.empty()) return false;
    p = bids_.begin()->first;
    return true;
}

bool OrderBook::best_ask(Price& p) const {
    if (asks_.empty()) return false;
    p = asks_.begin()->first;
    return true;
}

std::vector<LevelInfo> OrderBook::depth(Side side, std::size_t levels) const {
    std::vector<LevelInfo> out;
    auto collect = [&](const auto& book) {
        for (const auto& [price, level] : book) {
            if (out.size() == levels) break;
            out.push_back({price, level.total, level.queue.size()});
        }
    };
    if (side == Side::Buy) collect(bids_); else collect(asks_);
    return out;
}

}  // namespace lob
