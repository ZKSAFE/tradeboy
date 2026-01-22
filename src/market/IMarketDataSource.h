#pragma once

#include <string>

#include "Hyperliquid.h"

namespace tradeboy::market {

struct IMarketDataSource {
    virtual ~IMarketDataSource() = default;

    virtual bool fetch_all_mids_raw(std::string& out_json) = 0;
    virtual void set_user_address(const std::string& /*user_address_0x*/) {}
    virtual bool fetch_user_webdata_raw(std::string& /*out_json*/) { return false; }
    virtual bool fetch_spot_clearinghouse_state_raw(std::string& /*out_json*/) { return false; }
    virtual bool fetch_perp_clearinghouse_state_raw(std::string& /*out_json*/) { return false; }
};

} // namespace tradeboy::market
