#pragma once

#include <string>
#include <vector>

#include "../spot/KLineChart.h"

namespace tradeboy::market {

struct CandleReq {
    std::string coin;
    std::string interval;
    long long startTimeMs = 0;
    long long endTimeMs = 0;
};

bool fetch_all_mids_raw(std::string& out_json);
bool fetch_candle_snapshot_raw(const CandleReq& req, std::string& out_json);

bool parse_mid_price(const std::string& all_mids_json, const std::string& coin, double& out_price);
std::vector<tradeboy::spot::OHLC> parse_candle_snapshot(const std::string& candle_json);

} // namespace tradeboy::market
