#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "lob/features.hpp"
#include "lob/source.hpp"

#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); std::exit(1); } } while (0)
using namespace lob;

int main() {
    SyntheticSource src(30000, 5);
    FeatureConfig cfg;
    auto full = record_features(src.all(), cfg);
    CHECK(full.size() > 500);

    // Labels: exactly mid[i+h]-mid[i]; the last h rows are unlabelled.
    for (std::size_t i = 0; i < full.size(); ++i) {
        if (i + cfg.horizon < full.size())
            CHECK(full[i].label_dmid == full[i + cfg.horizon].mid - full[i].mid);
        else
            CHECK(std::isnan(full[i].label_dmid));
    }

    // No lookahead: features computed on a truncated stream equal the full-run features
    // for every row inside the prefix (only the label column may differ).
    for (std::size_t cut : {5000u, 12345u, 20000u}) {
        std::vector<Msg> prefix(src.all().begin(), src.all().begin() + cut);
        auto part = record_features(prefix, cfg);
        CHECK(!part.empty() && part.size() <= full.size());
        for (std::size_t i = 0; i < part.size(); ++i) {
            CHECK(part[i].seq == full[i].seq && part[i].mid == full[i].mid);
            CHECK(part[i].spread == full[i].spread && part[i].imb1 == full[i].imb1);
            CHECK(part[i].imb5 == full[i].imb5 && part[i].flow == full[i].flow);
            CHECK(part[i].microprice_dev == full[i].microprice_dev);
            CHECK(part[i].bid_depth == full[i].bid_depth && part[i].ask_depth == full[i].ask_depth);
        }
    }

    // Sanity ranges.
    for (auto& r : full) {
        CHECK(r.spread > 0 && r.imb1 >= -1 && r.imb1 <= 1 && r.imb5 >= -1 && r.imb5 <= 1);
        CHECK(std::fabs(r.microprice_dev) <= r.spread / 2 + 1e-9);
    }

    std::size_t n = write_csv(full, "/tmp/features_test.csv");
    CHECK(n == full.size() - cfg.horizon);
    std::printf("features tests passed (%zu rows)\n", full.size());
}
