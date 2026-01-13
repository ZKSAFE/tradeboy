#pragma once

#include <string>

#include "Hyperliquid.h"

namespace tradeboy::market {

struct IMarketDataSource {
    virtual ~IMarketDataSource() = default;

    virtual bool fetch_all_mids_raw(std::string& out_json) = 0;
};

} // namespace tradeboy::market
