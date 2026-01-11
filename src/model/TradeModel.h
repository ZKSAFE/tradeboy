#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "../spot/KLineChart.h"

namespace tradeboy::model {

struct SpotRow {
    std::string sym;
    double price = 0.0;
    double prev_price = 0.0;
    double balance = 0.0;
    double entry_price = 0.0;

    SpotRow() = default;
    SpotRow(std::string sym, double price, double prev_price, double balance, double entry_price)
        : sym(std::move(sym)), price(price), prev_price(prev_price), balance(balance), entry_price(entry_price) {}
};

struct TradeModelSnapshot {
    int tf_idx = 0;
    int spot_row_idx = 0;

    std::vector<SpotRow> spot_rows;
    std::vector<tradeboy::spot::OHLC> kline_data;
};

struct TradeModel {
    mutable std::mutex mu;

    TradeModelSnapshot snapshot() const;

    void set_spot_rows(std::vector<SpotRow> rows);
    void set_spot_row_idx(int idx);
    void set_tf_idx(int idx);

    void update_mid_prices_from_allmids_json(const std::string& all_mids_json);
    void set_kline_data(std::vector<tradeboy::spot::OHLC> v);

    // Legacy fallback: generate dummy kline data.
    void regenerate_kline_dummy(unsigned int seed_hint);

private:
    int tf_idx_ = 0;
    int spot_row_idx_ = 0;

    std::vector<SpotRow> spot_rows_;
    std::vector<tradeboy::spot::OHLC> kline_data_;
};

} // namespace tradeboy::model
