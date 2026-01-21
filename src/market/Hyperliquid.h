#pragma once

#include <string>
#include <vector>

namespace tradeboy::market {

bool fetch_all_mids_raw(std::string& out_json);

bool parse_mid_price(const std::string& all_mids_json, const std::string& coin, double& out_price);

bool fetch_info_raw(const std::string& request_json, std::string& out_json);

bool fetch_user_role_raw(const std::string& user_address_0x, std::string& out_json);

bool fetch_spot_clearinghouse_state_raw(const std::string& user_address_0x, std::string& out_json);

bool parse_usdc_deposit_address(const std::string& user_role_json, std::string& out_addr);

bool parse_spot_usdc_balance(const std::string& spot_state_json, double& out_usdc);

} // namespace tradeboy::market
