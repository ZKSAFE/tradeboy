#pragma once

#include <string>

namespace tradeboy::arb {

struct WalletOnchainData {
    bool rpc_ok = false;
    std::string eth_balance;  // formatted
    std::string usdc_balance; // formatted
    std::string gas;          // "GAS: ..."

    long double gas_price_wei = 0.0L;
};

bool fetch_wallet_data(const std::string& rpc_url,
                       const std::string& wallet_address_0x,
                       WalletOnchainData& out,
                       std::string& out_err);

} // namespace tradeboy::arb
