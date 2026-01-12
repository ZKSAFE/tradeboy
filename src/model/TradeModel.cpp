#include "TradeModel.h"

#include <algorithm>

#include "../market/Hyperliquid.h"

extern void log_to_file(const char* fmt, ...);

namespace tradeboy::model {

TradeModelSnapshot TradeModel::snapshot() const {
    std::lock_guard<std::mutex> lock(mu);
    TradeModelSnapshot s;
    s.tf_idx = tf_idx_;
    s.spot_row_idx = spot_row_idx_;
    s.spot_rows = spot_rows_;
    s.kline_data = kline_data_;
    return s;
}

void TradeModel::set_spot_rows(std::vector<SpotRow> rows) {
    std::lock_guard<std::mutex> lock(mu);
    spot_rows_ = std::move(rows);
    if (spot_row_idx_ < 0) spot_row_idx_ = 0;
    if (!spot_rows_.empty() && spot_row_idx_ >= (int)spot_rows_.size()) spot_row_idx_ = (int)spot_rows_.size() - 1;
}

void TradeModel::set_spot_row_idx(int idx) {
    std::lock_guard<std::mutex> lock(mu);
    if (spot_rows_.empty()) {
        spot_row_idx_ = 0;
        return;
    }
    spot_row_idx_ = std::max(0, std::min((int)spot_rows_.size() - 1, idx));
}

void TradeModel::set_tf_idx(int idx) {
    std::lock_guard<std::mutex> lock(mu);
    tf_idx_ = idx;
}

void TradeModel::update_mid_prices_from_allmids_json(const std::string& all_mids_json) {
    std::lock_guard<std::mutex> lock(mu);
    int updated = 0;
    for (auto& r : spot_rows_) {
        double p = 0.0;
        if (tradeboy::market::parse_mid_price(all_mids_json, r.sym, p)) {
            r.prev_price = r.price;
            r.price = p;
            updated++;
        }
    }
    log_to_file("[Model] allMids updated=%d json_len=%d\n", updated, (int)all_mids_json.size());
}

void TradeModel::set_kline_data(std::vector<tradeboy::spot::OHLC> v) {
    std::lock_guard<std::mutex> lock(mu);
    kline_data_ = std::move(v);
}

void TradeModel::regenerate_kline_dummy(unsigned int seed_hint) {
    std::lock_guard<std::mutex> lock(mu);
    kline_data_.clear();

    int candle_count = 60;
    kline_data_.reserve(candle_count);

    unsigned int seed = seed_hint;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) & 0xFFFFu;
    };

    float cur = 68000.0f;
    if (spot_rows_.size() > (size_t)spot_row_idx_) {
        cur = (float)spot_rows_[(size_t)spot_row_idx_].price;
    }

    for (int i = 0; i < candle_count; i++) {
        float o = cur;
        float volatility = cur * 0.005f;
        float hi = o + (float)(rnd() % 100) / 100.0f * volatility;
        float lo = o - (float)(rnd() % 100) / 100.0f * volatility;
        float c = lo + (float)(rnd() % 100) / 100.0f * (hi - lo);

        if (c > hi) hi = c;
        if (c < lo) lo = c;
        if (o > hi) hi = o;
        if (o < lo) lo = o;

        cur = c;
        kline_data_.push_back({o, hi, lo, c});
    }
}

} // namespace tradeboy::model
