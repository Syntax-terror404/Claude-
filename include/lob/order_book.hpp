#pragma once
// Limit order book with strict price-time priority.
//
// Complexity (n = resting orders, p = distinct price levels):
//   submit (no match)  O(log p)      cancel  O(log p)
//   each fill          O(1) amortised (plus O(log p) when a level empties)
//   best bid / ask     O(1)
#include <cstdint>
#include <list>
#include <map>
#include <unordered_map>
#include <vector>

namespace lob {

using OrderId = std::uint64_t;
using Price   = std::int64_t;  // integer ticks; never use floating point for prices
using Qty     = std::uint32_t;

enum class Side : std::uint8_t { Buy, Sell };
enum class OrderType : std::uint8_t { Limit, Market };
enum class TimeInForce : std::uint8_t {
    GTC,  // good-till-cancel: unfilled remainder rests on the book
    IOC,  // immediate-or-cancel: unfilled remainder is dropped
    FOK   // fill-or-kill: execute completely or not at all
};

struct Order {
    OrderId id{};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    Price price{0};  // ignored for market orders
    Qty qty{0};
    TimeInForce tif{TimeInForce::GTC};
};

struct Trade {
    OrderId taker_id;
    OrderId maker_id;
    Price price;  // always the resting (maker) order's price
    Qty qty;
    Side taker_side;
    bool operator==(const Trade& o) const {
        return taker_id == o.taker_id && maker_id == o.maker_id && price == o.price &&
               qty == o.qty && taker_side == o.taker_side;
    }
};

enum class Status : std::uint8_t {
    Resting,                  // some or all of the order is on the book
    Filled,                   // fully executed
    PartiallyFilledCancelled, // IOC / market: partly executed, remainder dropped
    Cancelled,                // IOC / market: nothing executed
    Rejected
};

enum class RejectReason : std::uint8_t {
    None, ZeroQty, BadPrice, DuplicateId, UnknownId, FokUnfillable
};

struct Result {
    Status status{Status::Rejected};
    RejectReason reason{RejectReason::None};
    Qty filled{0};
};

struct LevelInfo {
    Price price;
    std::uint64_t qty;
    std::size_t orders;
};

class OrderBook {
public:
    // Matches `o` against the book. Executions are appended to `trades` if non-null.
    Result submit(const Order& o, std::vector<Trade>* trades = nullptr);

    // Removes a resting order. Returns false if the id is not resting.
    bool cancel(OrderId id);

    // Cancel/replace. Reducing qty at the same price keeps queue priority;
    // any other change loses priority and re-enters as a GTC limit (may match).
    Result modify(OrderId id, Price new_price, Qty new_qty,
                  std::vector<Trade>* trades = nullptr);

    bool best_bid(Price& p) const;
    bool best_ask(Price& p) const;
    std::vector<LevelInfo> depth(Side side, std::size_t levels) const;
    std::size_t resting_orders() const { return index_.size(); }
    bool contains(OrderId id) const { return index_.count(id) != 0; }

private:
    struct Resting {
        OrderId id;
        Qty qty;
    };
    struct Level {
        std::list<Resting> queue;  // front = oldest = first to trade
        std::uint64_t total{0};
    };
    using Bids = std::map<Price, Level, std::greater<Price>>;
    using Asks = std::map<Price, Level, std::less<Price>>;
    struct Locator {
        Side side;
        Price price;
        std::list<Resting>::iterator it;
    };

    static bool crosses(Side taker, Price limit, Price level_price, OrderType t);
    template <class Opp>
    static std::uint64_t available(const Opp& opp, const Order& o);
    template <class Opp>
    Qty match(Opp& opp, const Order& o, Qty qty, std::vector<Trade>* trades);
    template <class Book>
    void rest(Book& book, const Order& o, Qty qty);
    template <class Book>
    void erase(Book& book, const Locator& loc);
    template <class Opp, class Own>
    Result process(Opp& opp, Own& own, const Order& o, std::vector<Trade>* trades);

    Bids bids_;
    Asks asks_;
    std::unordered_map<OrderId, Locator> index_;
};

}  // namespace lob
