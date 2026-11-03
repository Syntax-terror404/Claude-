// Usage: dump_depth <messages> <snapshots> <out.csv> [half_width_ticks] [seed]
// Order-book depth around the mid over time, for a heatmap: one row per (snapshot, price offset).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

#include "lob/source.hpp"

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: %s <messages> <snapshots> <out.csv> [half_width] [seed]\n", argv[0]); return 2; }
    const std::size_t n = std::strtoull(argv[1], nullptr, 10), snaps = std::strtoull(argv[2], nullptr, 10);
    const int hw = argc > 4 ? std::atoi(argv[4]) : 10;
    const unsigned seed = argc > 5 ? unsigned(std::strtoul(argv[5], nullptr, 10)) : 42;
    lob::SyntheticSource src(n, seed);
    lob::OrderBook book;
    std::FILE* f = std::fopen(argv[3], "w");
    if (!f) { std::perror("open"); return 1; }
    std::fprintf(f, "seq,offset,side,qty\n");
    const std::size_t every = n / snaps;
    for (const lob::Msg& m : src.all()) {
        if (m.kind == lob::Msg::Kind::Add) book.submit(m.order); else book.cancel(m.id);
        if (m.seq % every != 0) continue;
        lob::Price b, a;
        if (!book.best_bid(b) || !book.best_ask(a)) continue;
        const lob::Price mid = (a + b) / 2;
        std::map<lob::Price, std::uint64_t> bids, asks;
        for (auto& l : book.depth(lob::Side::Buy, 2000)) bids[l.price] = l.qty;
        for (auto& l : book.depth(lob::Side::Sell, 2000)) asks[l.price] = l.qty;
        for (int o = -hw; o <= hw; ++o) {
            const lob::Price p = mid + o;
            auto ib = bids.find(p); auto ia = asks.find(p);
            std::uint64_t q = ib != bids.end() ? ib->second : (ia != asks.end() ? ia->second : 0);
            std::fprintf(f, "%llu,%d,%s,%llu\n", (unsigned long long)m.seq, o, ia != asks.end() ? "ask" : "bid", (unsigned long long)q);
        }
    }
    std::fclose(f);
}
