#pragma once

#include <atomic>
#include <string>
#include <thread>

#include <pthread.h>

namespace tradeboy::model { struct TradeModel; }

namespace tradeboy::arb {

struct ArbitrumRpcService {
    ArbitrumRpcService(tradeboy::model::TradeModel& model,
                       const std::string& rpc_url,
                       const std::string& wallet_address_0x);
    ~ArbitrumRpcService();

    void start();
    void stop();

    void set_wallet(const std::string& rpc_url, const std::string& wallet_address_0x);

private:
    void run();

    tradeboy::model::TradeModel& model;

    pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
    std::string rpc_url_;
    std::string wallet_address_0x_;

    std::atomic<bool> stop_flag{false};
    std::thread th;
};

} // namespace tradeboy::arb
