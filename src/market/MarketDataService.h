#pragma once

#include <atomic>
#include <thread>

#include "IMarketDataSource.h"

namespace tradeboy::model { struct TradeModel; }

namespace tradeboy::market {

struct MarketDataService {
    MarketDataService(tradeboy::model::TradeModel& model, IMarketDataSource& src);
    ~MarketDataService();

    void start();
    void stop();

private:
    void run();

    tradeboy::model::TradeModel& model;
    IMarketDataSource& src;

    std::atomic<bool> stop_flag{false};
    std::thread th;
};

} // namespace tradeboy::market
