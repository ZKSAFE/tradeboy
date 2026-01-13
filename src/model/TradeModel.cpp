#include "TradeModel.h"

#include <algorithm>

#include "../market/Hyperliquid.h"

extern void log_to_file(const char* fmt, ...);

namespace tradeboy::model {

TradeModelSnapshot TradeModel::snapshot() const {
    std::lock_guard<std::mutex> lock(mu);
    TradeModelSnapshot s;
    s.spot_row_idx = spot_row_idx_;
    s.spot_rows = spot_rows_;
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

} // namespace tradeboy::model
