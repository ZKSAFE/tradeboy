#pragma once

#include <string>
#include <vector>

namespace tradeboy::market {

bool fetch_all_mids_raw(std::string& out_json);

bool parse_mid_price(const std::string& all_mids_json, const std::string& coin, double& out_price);

} // namespace tradeboy::market
