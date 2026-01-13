#include "HyperliquidWgetDataSource.h"

#include "Hyperliquid.h"

namespace tradeboy::market {

bool HyperliquidWgetDataSource::fetch_all_mids_raw(std::string& out_json) {
    return tradeboy::market::fetch_all_mids_raw(out_json);
}

} // namespace tradeboy::market
