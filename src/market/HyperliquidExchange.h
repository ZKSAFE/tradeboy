#pragma once

#include <string>

namespace tradeboy::market {

bool exchange_usd_class_transfer(const std::string& wallet_address_0x,
                                const std::string& private_key_hex,
                                bool to_perp,
                                const std::string& amount_str,
                                unsigned long long nonce_ms,
                                bool is_mainnet,
                                std::string& out_resp,
                                std::string& out_err);

} // namespace tradeboy::market
