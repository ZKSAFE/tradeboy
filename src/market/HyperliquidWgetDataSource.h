#pragma once

#include "IMarketDataSource.h"

namespace tradeboy::market {

struct HyperliquidWgetDataSource final : public IMarketDataSource {
    bool fetch_all_mids_raw(std::string& out_json) override;
    bool fetch_candle_snapshot_raw(const CandleReq& req, std::string& out_json) override;
};

} // namespace tradeboy::market
