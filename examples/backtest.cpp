// Usage: backtest [messages] [latency] [seed]
#include <cstdio>
#include <cstdlib>

#include "lob/backtest.hpp"
#include "lob/source.hpp"

static void show(const char* name, const lob::BacktestResult& r) {
    std::printf("%-16s net=%+10.1f gross=%+10.1f fees=%8.1f fills=%6llu (maker %llu) vol=%7llu "
                "maxpos=%4lld finalpos=%4lld maxDD=%8.1f sharpe/sample=%+.3f\n",
                name, r.net_pnl, r.gross_pnl, r.fees, (unsigned long long)r.fills,
                (unsigned long long)r.maker_fills, (unsigned long long)r.volume,
                (long long)r.max_abs_pos, (long long)r.final_pos, r.max_drawdown, r.sharpe_per_sample);
}

int main(int argc, char** argv) {
    std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1000000;
    lob::BacktestConfig cfg;
    if (argc > 2) cfg.latency = std::strtoull(argv[2], nullptr, 10);
    unsigned seed = argc > 3 ? unsigned(std::strtoul(argv[3], nullptr, 10)) : 42;
    lob::SyntheticSource src(n, seed);
    std::printf("source=%s messages=%zu latency=%llu msgs taker_fee=%.2f maker_fee=%.2f (ticks/share)\n",
                src.name().c_str(), n, (unsigned long long)cfg.latency, cfg.taker_fee, cfg.maker_fee);
    lob::MarketMaker mm;
    show("market-maker", lob::Engine(cfg).run(src.all(), mm));
    lob::ImbalanceTaker it;
    show("imbalance-taker", lob::Engine(cfg).run(src.all(), it));
}
