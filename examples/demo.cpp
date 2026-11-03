#include <cstdio>

#include "lob/order_book.hpp"

using namespace lob;

static void print_book(const OrderBook& b) {
    auto asks = b.depth(Side::Sell, 5);
    auto bids = b.depth(Side::Buy, 5);
    std::printf("  %-10s %8s %6s\n", "price", "qty", "orders");
    for (auto it = asks.rbegin(); it != asks.rend(); ++it)
        std::printf("  ASK %-6lld %8llu %6zu\n", (long long)it->price, (unsigned long long)it->qty, it->orders);
    std::printf("  ----------------------------\n");
    for (const auto& l : bids)
        std::printf("  BID %-6lld %8llu %6zu\n", (long long)l.price, (unsigned long long)l.qty, l.orders);
    std::printf("\n");
}

int main() {
    OrderBook b;
    std::vector<Trade> trades;
    OrderId id = 1;
    for (Price p : {101, 102, 103}) b.submit({id++, Side::Sell, OrderType::Limit, p, 50, TimeInForce::GTC});
    for (Price p : {100, 99, 98}) b.submit({id++, Side::Buy, OrderType::Limit, p, 50, TimeInForce::GTC});
    std::printf("Initial book\n");
    print_book(b);

    std::printf("Buy 120 @ 102 (limit, GTC) sweeps 101 and 102, rests the remainder\n");
    auto r = b.submit({id++, Side::Buy, OrderType::Limit, 102, 120, TimeInForce::GTC}, &trades);
    for (const auto& t : trades)
        std::printf("  TRADE %u @ %lld (maker #%llu)\n", t.qty, (long long)t.price, (unsigned long long)t.maker_id);
    std::printf("  filled=%u\n\n", r.filled);
    print_book(b);
}
