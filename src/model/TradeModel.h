#pragma once

#include <mutex>
#include <string>
#include <vector>

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
    int spot_row_idx = 0;

    std::vector<SpotRow> spot_rows;
};

struct TradeModel {
    mutable std::mutex mu;

    TradeModelSnapshot snapshot() const;

    void set_spot_rows(std::vector<SpotRow> rows);
    void set_spot_row_idx(int idx);

    void update_mid_prices_from_allmids_json(const std::string& all_mids_json);

private:
    int spot_row_idx_ = 0;

    std::vector<SpotRow> spot_rows_;
};

} // namespace tradeboy::model
