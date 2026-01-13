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

private:
    void run();

    std::atomic<bool> stop_{false};
    std::thread th_;

    std::mutex mu_;
    std::string latest_mids_json_;
    long long latest_mids_ms_ = 0;
};

} // namespace tradeboy::market
