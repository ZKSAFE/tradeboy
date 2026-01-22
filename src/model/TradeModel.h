#pragma once

#include <pthread.h>
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

struct WalletSnapshot {
    std::string wallet_address;
    std::string private_key;
};

struct TradeModel {
    TradeModel();
    ~TradeModel();

    TradeModel(const TradeModel&) = delete;
    TradeModel& operator=(const TradeModel&) = delete;
    TradeModel(TradeModel&&) = delete;
    TradeModel& operator=(TradeModel&&) = delete;

    mutable pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

    TradeModelSnapshot snapshot() const;
    WalletSnapshot wallet_snapshot() const;

    void set_spot_rows(std::vector<SpotRow> rows);
    void set_spot_row_idx(int idx);

    void set_wallet(const std::string& wallet_address, const std::string& private_key);

    void update_mid_prices_from_allmids_json(const std::string& all_mids_json);

private:
    int spot_row_idx_ = 0;

    std::vector<SpotRow> spot_rows_;

    std::string wallet_address_;
    std::string private_key_;
};

} // namespace tradeboy::model
