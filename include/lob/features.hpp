#pragma once
// Feature recorder for the research stage. Replays an in-order message stream through a
// book and samples causal features; forward-mid labels are attached afterwards.
//
// Anti-leakage contract (enforced by tests/test_features.cpp):
//   * a feature row at sample i depends only on messages with seq <= row.seq
//   * the label is the ONLY forward-looking column and is clearly named label_*
#include <cmath>
#include <cstdio>
#include <deque>
#include <limits>
#include <string>
#include <vector>

#include "lob/feed.hpp"

namespace lob {

struct FeatureConfig {
    std::size_t sample_every = 10;   // messages between samples
    std::size_t horizon = 50;        // label horizon, in SAMPLES (not messages)
    std::size_t flow_window = 200;   // messages in the signed-trade-flow window
    std::size_t depth_levels = 5;
};

struct FeatureRow {
    std::uint64_t seq{};
    double mid{}, spread{}, imb1{}, imb5{}, microprice_dev{}, flow{}, bid_depth{}, ask_depth{};
    double label_dmid = std::numeric_limits<double>::quiet_NaN();  // mid[i+h]-mid[i], ticks
};

inline std::vector<FeatureRow> record_features(const std::vector<Msg>& in_order, const FeatureConfig& cfg) {
    OrderBook book;
    std::vector<FeatureRow> rows;
    std::deque<std::pair<std::uint64_t, double>> flow;  // (seq, signed qty)
    double flow_sum = 0;
    std::vector<Trade> trades;

    for (const Msg& m : in_order) {
        trades.clear();
        if (m.kind == Msg::Kind::Add) book.submit(m.order, &trades); else book.cancel(m.id);
        for (const Trade& t : trades) {
            double s = (t.taker_side == Side::Buy ? 1.0 : -1.0) * double(t.qty);
            flow.emplace_back(m.seq, s);
            flow_sum += s;
        }
        while (!flow.empty() && flow.front().first + cfg.flow_window <= m.seq) {
            flow_sum -= flow.front().second;
            flow.pop_front();
        }
        if (m.seq % cfg.sample_every != 0) continue;

        double mid; Price spread;
        if (!Analytics::mid_spread(book, mid, spread)) continue;
        auto bids = book.depth(Side::Buy, cfg.depth_levels);
        auto asks = book.depth(Side::Sell, cfg.depth_levels);
        double bq1 = double(bids[0].qty), aq1 = double(asks[0].qty);
        double bd = 0, ad = 0;
        for (auto& l : bids) bd += double(l.qty);
        for (auto& l : asks) ad += double(l.qty);

        FeatureRow r;
        r.seq = m.seq;
        r.mid = mid;
        r.spread = double(spread);
        r.imb1 = (bq1 - aq1) / (bq1 + aq1);
        r.imb5 = (bd - ad) / (bd + ad);
        double micro = (double(bids[0].price) * aq1 + double(asks[0].price) * bq1) / (bq1 + aq1);
        r.microprice_dev = micro - mid;
        r.flow = flow_sum;
        r.bid_depth = bd;
        r.ask_depth = ad;
        rows.push_back(r);
    }
    for (std::size_t i = 0; i + cfg.horizon < rows.size(); ++i)
        rows[i].label_dmid = rows[i + cfg.horizon].mid - rows[i].mid;
    return rows;
}

// Writes only labelled rows (the unlabeled tail would be silently wrong to train on).
inline std::size_t write_csv(const std::vector<FeatureRow>& rows, const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return 0;
    std::fprintf(f, "seq,mid,spread,imb1,imb5,microprice_dev,flow,bid_depth,ask_depth,label_dmid\n");
    std::size_t n = 0;
    for (const auto& r : rows) {
        if (std::isnan(r.label_dmid)) continue;
        std::fprintf(f, "%llu,%.4f,%.1f,%.6f,%.6f,%.6f,%.1f,%.0f,%.0f,%.4f\n",
                     (unsigned long long)r.seq, r.mid, r.spread, r.imb1, r.imb5, r.microprice_dev,
                     r.flow, r.bid_depth, r.ask_depth, r.label_dmid);
        ++n;
    }
    std::fclose(f);
    return n;
}

}  // namespace lob
