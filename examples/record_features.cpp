// Usage: record_features <messages> <out.csv> [horizon_samples] [sample_every]
#include <cstdio>
#include <cstdlib>

#include "lob/features.hpp"
#include "lob/source.hpp"

int main(int argc, char** argv) {
    std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1000000;
    const char* out = argc > 2 ? argv[2] : "features.csv";
    lob::FeatureConfig cfg;
    if (argc > 3) cfg.horizon = std::strtoull(argv[3], nullptr, 10);
    if (argc > 4) cfg.sample_every = std::strtoull(argv[4], nullptr, 10);
    lob::SyntheticSource src(n, 42);
    auto rows = lob::record_features(src.all(), cfg);
    std::size_t w = lob::write_csv(rows, out);
    std::printf("source=%s messages=%zu sampled=%zu labelled_written=%zu -> %s\n",
                src.name().c_str(), n, rows.size(), w, out);
}
