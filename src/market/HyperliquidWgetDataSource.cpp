#include "HyperliquidWgetDataSource.h"

#include "Hyperliquid.h"

namespace tradeboy::market {

bool HyperliquidWgetDataSource::fetch_all_mids_raw(std::string& out_json) {
    return tradeboy::market::fetch_all_mids_raw(out_json);
}

bool HyperliquidWgetDataSource::fetch_candle_snapshot_raw(const CandleReq& req, std::string& out_json) {
    return tradeboy::market::fetch_candle_snapshot_raw(req, out_json);
}

} // namespace tradeboy::market
