#include "TradeModel.h"

#include <algorithm>

#include "../market/Hyperliquid.h"

#include "utils/Log.h"

namespace tradeboy::model {

TradeModel::TradeModel() {
    log_str("[Model] ctor\n");
}

TradeModel::~TradeModel() {
}

TradeModelSnapshot TradeModel::snapshot() const {
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) {
        log_str("[Model] snapshot mutex_lock failed\n");
        return TradeModelSnapshot();
    }
    TradeModelSnapshot s;
    s.spot_row_idx = spot_row_idx_;
    s.spot_rows = spot_rows_;
    pthread_mutex_unlock(&mu);
    return s;
}

WalletSnapshot TradeModel::wallet_snapshot() const {
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) {
        return WalletSnapshot();
    }
    WalletSnapshot w;
    w.wallet_address = wallet_address_;
    w.private_key = private_key_;
    pthread_mutex_unlock(&mu);
    return w;
}

AccountSnapshot TradeModel::account_snapshot() const {
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) {
        return AccountSnapshot();
    }
    AccountSnapshot a;
    a.hl_usdc_str = hl_usdc_str_;
    a.hl_usdc = hl_usdc_;
    a.hl_perp_usdc_str = hl_perp_usdc_str_;
    a.hl_perp_usdc = hl_perp_usdc_;
    a.hl_total_asset_str = hl_total_asset_str_;
    a.hl_total_asset = hl_total_asset_;
    a.hl_pnl_24h_str = hl_pnl_24h_str_;
    a.hl_pnl_24h = hl_pnl_24h_;
    a.hl_pnl_24h_pct_str = hl_pnl_24h_pct_str_;
    a.hl_pnl_24h_pct = hl_pnl_24h_pct_;
    a.arb_eth_str = arb_eth_str_;
    a.arb_usdc_str = arb_usdc_str_;
    a.arb_gas_str = arb_gas_str_;
    a.arb_gas_price_wei = arb_gas_price_wei_;
    a.arb_rpc_ok = arb_rpc_ok_;
    pthread_mutex_unlock(&mu);
    return a;
}

void TradeModel::set_wallet(const std::string& wallet_address, const std::string& private_key) {
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) {
        return;
    }
    wallet_address_ = wallet_address;
    private_key_ = private_key;
    pthread_mutex_unlock(&mu);
}

void TradeModel::set_hl_portfolio(double total_asset,
                                  const std::string& total_asset_str,
                                  double pnl_24h,
                                  const std::string& pnl_24h_str,
                                  double pnl_24h_pct,
                                  const std::string& pnl_24h_pct_str,
                                  bool ok) {
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) {
        return;
    }
    hl_total_asset_ = ok ? total_asset : 0.0;
    hl_total_asset_str_ = ok ? total_asset_str : std::string("UNKNOWN");
    hl_pnl_24h_ = ok ? pnl_24h : 0.0;
    hl_pnl_24h_str_ = ok ? pnl_24h_str : std::string("UNKNOWN");
    hl_pnl_24h_pct_ = ok ? pnl_24h_pct : 0.0;
    hl_pnl_24h_pct_str_ = ok ? pnl_24h_pct_str : std::string("UNKNOWN");
    pthread_mutex_unlock(&mu);
}

void TradeModel::set_hl_usdc(double usdc, const std::string& usdc_str, bool ok) {
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) {
        return;
    }
    hl_usdc_ = ok ? usdc : 0.0;
    hl_usdc_str_ = ok ? usdc_str : std::string("UNKNOWN");
    pthread_mutex_unlock(&mu);
}

void TradeModel::set_hl_perp_usdc(double usdc, const std::string& usdc_str, bool ok) {
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) {
        return;
    }
    hl_perp_usdc_ = ok ? usdc : 0.0;
    hl_perp_usdc_str_ = ok ? usdc_str : std::string("UNKNOWN");
    pthread_mutex_unlock(&mu);
}

void TradeModel::set_arb_wallet_data(const std::string& eth_str,
                                     const std::string& usdc_str,
                                     const std::string& gas_str,
                                     long double gas_price_wei,
                                     bool ok) {
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) {
        return;
    }
    if (ok) {
        arb_eth_str_ = eth_str;
        arb_usdc_str_ = usdc_str;
        arb_gas_str_ = gas_str;
        arb_gas_price_wei_ = gas_price_wei;
        arb_rpc_ok_ = true;
    } else {
        arb_eth_str_ = "UNKNOWN";
        arb_usdc_str_ = "UNKNOWN";
        arb_gas_str_ = "GAS: UNKNOWN";
        arb_gas_price_wei_ = 0.0L;
        arb_rpc_ok_ = false;
    }
    pthread_mutex_unlock(&mu);
}

void TradeModel::set_spot_rows(std::vector<SpotRow> rows) {
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) return;
    // CRITICAL: Use swap instead of move-assignment. RG34XX armhf has SIGSEGV with std::vector move.
    spot_rows_.swap(rows);
    if (spot_row_idx_ < 0) spot_row_idx_ = 0;
    if (!spot_rows_.empty() && spot_row_idx_ >= (int)spot_rows_.size()) spot_row_idx_ = (int)spot_rows_.size() - 1;
    pthread_mutex_unlock(&mu);
}

void TradeModel::set_spot_row_idx(int idx) {
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) return;
    if (spot_rows_.empty()) {
        spot_row_idx_ = 0;
        pthread_mutex_unlock(&mu);
        return;
    }
    spot_row_idx_ = std::max(0, std::min((int)spot_rows_.size() - 1, idx));
    pthread_mutex_unlock(&mu);
}

void TradeModel::update_mid_prices_from_allmids_json(const std::string& all_mids_json) {
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) return;
    for (auto& r : spot_rows_) {
        double p = 0.0;
        if (tradeboy::market::parse_mid_price(all_mids_json, r.sym, p)) {
            r.prev_price = r.price;
            r.price = p;
        }
    }
    pthread_mutex_unlock(&mu);
}

} // namespace tradeboy::model
