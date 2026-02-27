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

bool exchange_withdraw3(const std::string& wallet_address_0x,
                        const std::string& private_key_hex,
                        const std::string& destination_addr_0x,
                        const std::string& amount_str,
                        unsigned long long nonce_ms,
                        bool is_mainnet,
                        std::string& out_resp,
                        std::string& out_err);

bool exchange_spot_market_order(const std::string& wallet_address_0x,
                                const std::string& private_key_hex,
                                const std::string& display_sym,
                                bool is_buy,
                                double input_amount,
                                double mid_px,
                                const std::string& spot_meta_json,
                                double slippage,
                                bool is_mainnet,
                                std::string& out_resp,
                                std::string& out_err);

bool exchange_perp_market_order(const std::string& wallet_address_0x,
                                const std::string& private_key_hex,
                                const std::string& display_sym,
                                bool is_buy,
                                bool reduce_only,
                                double input_amount,
                                double leverage,
                                double mid_px,
                                const std::string& perp_meta_json,
                                double slippage,
                                bool is_mainnet,
                                std::string& out_resp,
                                std::string& out_err);

bool exchange_perp_update_leverage(const std::string& wallet_address_0x,
                                   const std::string& private_key_hex,
                                   const std::string& display_sym,
                                   double leverage,
                                   bool is_cross,
                                   const std::string& perp_meta_json,
                                   bool is_mainnet,
                                   std::string& out_resp,
                                   std::string& out_err);

} // namespace tradeboy::market
