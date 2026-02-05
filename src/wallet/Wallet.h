#pragma once

#include <string>

namespace tradeboy::wallet {

struct WalletConfig {
    std::string arb_rpc_url;
    std::string wallet_address; // 0x...
    std::string private_key;    // 0x...
    std::string regular_font_path;      // font file path
    std::string bolditalic_font_path;   // font file path
};

bool load_or_create_config(const std::string& path, WalletConfig& out_cfg, bool& out_created, std::string& out_err);

} // namespace tradeboy::wallet
