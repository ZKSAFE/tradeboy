#include "ArbitrumRpcService.h"

#include "arb/ArbitrumRpc.h"
#include "model/TradeModel.h"

#include <chrono>

extern void log_str(const char* s);

namespace tradeboy::arb {

ArbitrumRpcService::ArbitrumRpcService(tradeboy::model::TradeModel& model,
                                       const std::string& rpc_url,
                                       const std::string& wallet_address_0x)
    : model(model), rpc_url_(rpc_url), wallet_address_0x_(wallet_address_0x) {
    pthread_mutex_init(&mu, nullptr);
}

ArbitrumRpcService::~ArbitrumRpcService() {
    stop();
    pthread_mutex_destroy(&mu);
}

void ArbitrumRpcService::start() {
    if (th.joinable()) return;
    stop_flag = false;
    th = std::thread([this]() { run(); });
}

void ArbitrumRpcService::stop() {
    stop_flag = true;
    if (th.joinable()) th.join();
}

void ArbitrumRpcService::set_wallet(const std::string& rpc_url, const std::string& wallet_address_0x) {
    pthread_mutex_lock(&mu);
    rpc_url_ = rpc_url;
    wallet_address_0x_ = wallet_address_0x;
    pthread_mutex_unlock(&mu);
}

void ArbitrumRpcService::run() {
    while (!stop_flag.load()) {
        std::string rpc_url;
        std::string wallet_address_0x;
        {
            pthread_mutex_lock(&mu);
            rpc_url = rpc_url_;
            wallet_address_0x = wallet_address_0x_;
            pthread_mutex_unlock(&mu);
        }

        if (!rpc_url.empty() && !wallet_address_0x.empty()) {
            tradeboy::arb::WalletOnchainData d;
            std::string e;
            bool ok = tradeboy::arb::fetch_wallet_data(rpc_url, wallet_address_0x, d, e);
            if (ok && d.rpc_ok) {
                model.set_arb_wallet_data(d.eth_balance, d.usdc_balance, d.gas, d.gas_price_wei, true);
            } else {
                model.set_arb_wallet_data("", "", "", 0.0L, false);
                if (!e.empty()) {
                    log_str("[ARB] fetch_wallet_data failed\n");
                }
            }
        } else {
            model.set_arb_wallet_data("", "", "", 0.0L, false);
        }

        for (int i = 0; i < 20; i++) {
            if (stop_flag.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

} // namespace tradeboy::arb
