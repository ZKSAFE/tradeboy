#include "HyperliquidWgetDataSource.h"

#include "Hyperliquid.h"

namespace tradeboy::market {

bool HyperliquidWgetDataSource::fetch_all_mids_raw(std::string& out_json) {
    return tradeboy::market::fetch_all_mids_raw(out_json);
}

void HyperliquidWgetDataSource::set_user_address(const std::string& user_address_0x) {
    user_address_0x_ = user_address_0x;
}

bool HyperliquidWgetDataSource::fetch_user_webdata_raw(std::string& out_json) {
    if (user_address_0x_.empty()) return false;
    return tradeboy::market::fetch_spot_clearinghouse_state_raw(user_address_0x_, out_json);
}

bool HyperliquidWgetDataSource::fetch_spot_clearinghouse_state_raw(std::string& out_json) {
    if (user_address_0x_.empty()) return false;
    return tradeboy::market::fetch_spot_clearinghouse_state_raw(user_address_0x_, out_json);
}

} // namespace tradeboy::market
