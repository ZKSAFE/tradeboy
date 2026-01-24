/**
 * @file TradeModel.h
 * @brief Central state management for TradeBoy application.
 * 
 * ARCHITECTURE CRITICAL - DO NOT MODIFY WITHOUT UNDERSTANDING:
 * 1. Uses pthread_mutex_t instead of std::mutex (RG34XX ABI compatibility)
 * 2. Uses swap() instead of move-assignment for std::vector (SIGSEGV workaround)
 * 3. All public methods are thread-safe via mutex
 * 4. Snapshot pattern: UI reads snapshots, data layer writes via setters
 */
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

struct AccountSnapshot {
    std::string hl_usdc_str;
    double hl_usdc = 0.0;

    std::string hl_perp_usdc_str;
    double hl_perp_usdc = 0.0;

    std::string hl_total_asset_str;
    double hl_total_asset = 0.0;
    std::string hl_pnl_24h_str;
    double hl_pnl_24h = 0.0;
    std::string hl_pnl_24h_pct_str;
    double hl_pnl_24h_pct = 0.0;

    std::string arb_eth_str;
    std::string arb_usdc_str;
    std::string arb_gas_str;
    long double arb_gas_price_wei = 0.0L;
    bool arb_rpc_ok = false;
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
    AccountSnapshot account_snapshot() const;

    void set_spot_rows(std::vector<SpotRow> rows);
    void set_spot_row_idx(int idx);

    void set_wallet(const std::string& wallet_address, const std::string& private_key);

    void set_hl_usdc(double usdc, const std::string& usdc_str, bool ok);
    void set_hl_perp_usdc(double usdc, const std::string& usdc_str, bool ok);
    void set_hl_portfolio(double total_asset,
                          const std::string& total_asset_str,
                          double pnl_24h,
                          const std::string& pnl_24h_str,
                          double pnl_24h_pct,
                          const std::string& pnl_24h_pct_str,
                          bool ok);
    void set_arb_wallet_data(const std::string& eth_str,
                             const std::string& usdc_str,
                             const std::string& gas_str,
                             long double gas_price_wei,
                             bool ok);

    void update_mid_prices_from_allmids_json(const std::string& all_mids_json);

private:
    int spot_row_idx_ = 0;

    std::vector<SpotRow> spot_rows_;

    std::string wallet_address_;
    std::string private_key_;

    std::string hl_usdc_str_;
    double hl_usdc_ = 0.0;

    std::string hl_perp_usdc_str_;
    double hl_perp_usdc_ = 0.0;

    std::string hl_total_asset_str_;
    double hl_total_asset_ = 0.0;
    std::string hl_pnl_24h_str_;
    double hl_pnl_24h_ = 0.0;
    std::string hl_pnl_24h_pct_str_;
    double hl_pnl_24h_pct_ = 0.0;

    std::string arb_eth_str_;
    std::string arb_usdc_str_;
    std::string arb_gas_str_;
    long double arb_gas_price_wei_ = 0.0L;
    bool arb_rpc_ok_ = false;
};

} // namespace tradeboy::model
