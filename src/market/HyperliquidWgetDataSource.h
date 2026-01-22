#pragma once

#include "IMarketDataSource.h"

namespace tradeboy::market {

struct HyperliquidWgetDataSource final : public IMarketDataSource {
    bool fetch_all_mids_raw(std::string& out_json) override;
    void set_user_address(const std::string& user_address_0x) override;
    bool fetch_user_webdata_raw(std::string& out_json) override;
    bool fetch_spot_clearinghouse_state_raw(std::string& out_json) override;

private:
    std::string user_address_0x_;
};

} // namespace tradeboy::market
