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

bool send_usdc_transfer_test(const std::string& rpc_url,
                             const std::string& from_addr_0x,
                             const std::string& privkey_0x,
                             const std::string& to_addr_0x,
                             unsigned long long amount_micro,
                             std::string& out_txhash,
                             std::string& out_err);

bool wait_tx_confirmations(const std::string& rpc_url,
                           const std::string& txhash_0x,
                           int min_confirmations,
                           int timeout_ms,
                           std::string& out_err);

} // namespace tradeboy::arb
