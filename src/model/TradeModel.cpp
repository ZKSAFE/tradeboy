#include "TradeModel.h"

#include <algorithm>

#include "../market/Hyperliquid.h"

extern void log_to_file(const char* fmt, ...);
extern void log_str(const char* s);

namespace tradeboy::model {

TradeModel::TradeModel() {
    log_str("[Model] ctor\n");
    int rc = pthread_mutex_init(&mu, nullptr);
    if (rc != 0) {
        log_to_file("[Model] pthread_mutex_init failed rc=%d\n", rc);
    }
}

TradeModel::~TradeModel() {
    pthread_mutex_destroy(&mu);
}

TradeModelSnapshot TradeModel::snapshot() const {
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) {
        log_to_file("[Model] snapshot mutex_lock failed rc=%d\n", rc);
        return TradeModelSnapshot();
    }
    TradeModelSnapshot s;
    s.spot_row_idx = spot_row_idx_;
    s.spot_rows = spot_rows_;
    pthread_mutex_unlock(&mu);
    return s;
}

void TradeModel::set_spot_rows(std::vector<SpotRow> rows) {
    log_str("[Model] set_spot_rows enter\n");
    log_str("[Model] set_spot_rows about to lock\n");
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) {
        log_str("[Model] set_spot_rows mutex_lock failed\n");
        return;
    }
    log_str("[Model] set_spot_rows locked\n");
    // NOTE: On RG34XX we've seen SIGSEGV in std::vector move-assignment here.
    // Use copy assignment as a conservative workaround.
    spot_rows_.swap(rows);
    log_str("[Model] set_spot_rows swapped\n");
    if (spot_row_idx_ < 0) spot_row_idx_ = 0;
    if (!spot_rows_.empty() && spot_row_idx_ >= (int)spot_rows_.size()) spot_row_idx_ = (int)spot_rows_.size() - 1;
    log_str("[Model] set_spot_rows exit\n");
    pthread_mutex_unlock(&mu);
    log_str("[Model] set_spot_rows unlocked\n");
}

void TradeModel::set_spot_row_idx(int idx) {
    (void)idx;
    log_str("[Model] set_spot_row_idx enter\n");
    log_str("[Model] set_spot_row_idx about to lock\n");
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) {
        log_str("[Model] set_spot_row_idx mutex_lock failed\n");
        return;
    }
    log_str("[Model] set_spot_row_idx locked\n");
    if (spot_rows_.empty()) {
        spot_row_idx_ = 0;
        log_str("[Model] set_spot_row_idx exit empty\n");
        pthread_mutex_unlock(&mu);
        return;
    }
    spot_row_idx_ = std::max(0, std::min((int)spot_rows_.size() - 1, idx));
    log_str("[Model] set_spot_row_idx exit\n");
    pthread_mutex_unlock(&mu);
}

void TradeModel::update_mid_prices_from_allmids_json(const std::string& all_mids_json) {
    int rc = pthread_mutex_lock(&mu);
    if (rc != 0) {
        log_to_file("[Model] allMids mutex_lock failed rc=%d\n", rc);
        return;
    }
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
    pthread_mutex_unlock(&mu);
}

} // namespace tradeboy::model
