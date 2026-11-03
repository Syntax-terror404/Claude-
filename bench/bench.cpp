// Synthetic order-flow benchmark. Op streams are generated up front so only the
// book is on the clock. Reports throughput (untimed-per-op pass) and latency
// percentiles (per-op timed pass; includes ~20ns of clock overhead).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>

#include "lob/order_book.hpp"

using namespace lob;
using Clock = std::chrono::steady_clock;

struct Op { bool is_cancel; Order order; OrderId cancel_id; };

static std::vector<Op> generate(std::size_t n, unsigned seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> noise(0.0, 12.0);
    std::vector<Op> ops;
    ops.reserve(n);
    Price mid = 100000;  // ticks
    OrderId next = 1;
    for (std::size_t i = 0; i < n; ++i) {
        if (rng() % 200 == 0) mid += (rng() & 1) ? 1 : -1;  // slow random walk
        unsigned d = rng() % 100;
        if (d < 30 && next > 1) {
            ops.push_back({true, {}, 1 + rng() % (next - 1)});
        } else {
            Order o;
            o.id = next++;
            o.side = rng() & 1 ? Side::Buy : Side::Sell;
            o.qty = 1 + rng() % 100;
            if (d >= 95) { o.type = OrderType::Market; o.tif = TimeInForce::IOC; }
            else {
                Price off = static_cast<Price>(std::abs(noise(rng))) - 2;  // some orders cross
                o.price = o.side == Side::Buy ? mid - off : mid + off;
                o.tif = d >= 90 ? TimeInForce::IOC : TimeInForce::GTC;
            }
            ops.push_back({false, o, 0});
        }
    }
    return ops;
}

static void apply(OrderBook& b, const Op& op, std::vector<Trade>& trades) {
    if (op.is_cancel) b.cancel(op.cancel_id);
    else b.submit(op.order, &trades);
}

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 2'000'000;
    const auto ops = generate(n, 42);

    {  // throughput
        OrderBook b;
        std::vector<Trade> trades;
        trades.reserve(1 << 16);
        const auto t0 = Clock::now();
        std::size_t total_trades = 0;
        for (const auto& op : ops) {
            apply(b, op, trades);
            total_trades += trades.size();
            trades.clear();
        }
        const double s = std::chrono::duration<double>(Clock::now() - t0).count();
        std::printf("ops:        %zu\ntrades:     %zu\nresting:    %zu\nthroughput: %.2f M ops/s\n",
                    n, total_trades, b.resting_orders(), n / s / 1e6);
    }
    {  // latency
        OrderBook b;
        std::vector<Trade> trades;
        std::vector<std::uint32_t> lat;
        lat.reserve(n);
        for (const auto& op : ops) {
            const auto t0 = Clock::now();
            apply(b, op, trades);
            lat.push_back(static_cast<std::uint32_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count()));
            trades.clear();
        }
        std::sort(lat.begin(), lat.end());
        auto pct = [&](double p) { return lat[static_cast<std::size_t>(p * (lat.size() - 1))]; };
        std::printf("latency ns: p50=%u  p90=%u  p99=%u  p99.9=%u  max=%u\n", pct(.5), pct(.9),
                    pct(.99), pct(.999), lat.back());
    }
}
