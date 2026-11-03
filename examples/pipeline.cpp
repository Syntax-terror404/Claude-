#include <chrono>
#include <cstdio>

#include "lob/feed.hpp"

using namespace lob;

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1000000;
    auto feed = generate_feed(n, 42);
    auto wire = impair(feed, {});
    OrderBook book;
    Analytics an(1000);
    FeedHandler h(book, [&](std::uint64_t s, Msg& m) { m = feed[s - 1]; return true; }, &an);

    auto t0 = std::chrono::steady_clock::now();
    std::FILE* csv = argc > 2 ? std::fopen(argv[2], "w") : nullptr;
    if (csv) std::fprintf(csv, "seq,mid,spread,imbalance5,vwap1000,resting\n");
    std::size_t k = 0;
    for (const auto& m : wire) {
        h.on_packet(m);
        double mm; Price sp;
        if (csv && ++k % 1000 == 0 && Analytics::mid_spread(book, mm, sp))
            std::fprintf(csv, "%llu,%.1f,%lld,%.4f,%.2f,%zu\n", (unsigned long long)(h.next_expected() - 1),
                         mm, (long long)sp, Analytics::imbalance(book), an.vwap(), book.resting_orders());
    }
    if (csv) std::fclose(csv);
    h.heartbeat(feed.back().seq);
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    const auto& st = h.stats();
    double mid; Price spread;
    std::printf("sent %zu  received %zu (loss/dup/reorder injected)\n", feed.size(), wire.size());
    std::printf("applied %llu  dup-dropped %llu  reordered-buffered %llu  retransmits %llu\n",
                (unsigned long long)st.applied, (unsigned long long)st.duplicates,
                (unsigned long long)st.buffered, (unsigned long long)st.retransmits);
    std::printf("throughput %.2f M msgs/s\n", wire.size() / s / 1e6);
    if (Analytics::mid_spread(book, mid, spread))
        std::printf("mid %.1f  spread %lld  imbalance(5) %+.3f  vwap(1000) %.2f\n", mid,
                    (long long)spread, Analytics::imbalance(book), an.vwap());
}
