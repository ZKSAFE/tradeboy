#pragma once

#include "IMarketDataSource.h"

namespace tradeboy::market {

struct HyperliquidWgetDataSource final : public IMarketDataSource {
    bool fetch_all_mids_raw(std::string& out_json) override;
};

} // namespace tradeboy::market
