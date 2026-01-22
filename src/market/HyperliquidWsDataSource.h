#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "IMarketDataSource.h"

namespace tradeboy::market {

struct HyperliquidWsDataSource : public IMarketDataSource {
    HyperliquidWsDataSource();
    ~HyperliquidWsDataSource() override;

    bool fetch_all_mids_raw(std::string& out_json) override;
    void set_user_address(const std::string& user_address_0x) override;
    bool fetch_user_webdata_raw(std::string& out_json) override;
    bool fetch_spot_clearinghouse_state_raw(std::string& out_json) override;

private:
    void run();

    std::atomic<bool> stop_{false};
    std::thread th_;

    std::mutex mu_;
    std::string latest_mids_json_;
    long long latest_mids_ms_ = 0;

    std::string latest_user_json_;
    long long latest_user_ms_ = 0;
    std::string user_address_0x_;

    std::string latest_spot_json_;
    long long latest_spot_ms_ = 0;
    std::atomic<bool> spot_request_pending_{false};
    unsigned int spot_request_id_ = 1;
    unsigned int spot_request_sent_id_ = 0;
    long long spot_request_last_ms_ = 0;
    int spot_request_interval_ms_ = 3000;

    std::atomic<bool> reconnect_requested_{false};
};

} // namespace tradeboy::market
