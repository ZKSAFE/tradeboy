#include "MarketDataService.h"

#include <algorithm>
#include <chrono>
#include <vector>

#include "../model/TradeModel.h"
#include "Hyperliquid.h"

extern void log_to_file(const char* fmt, ...);

namespace tradeboy::market {

MarketDataService::MarketDataService(tradeboy::model::TradeModel& model, IMarketDataSource& src)
    : model(model), src(src) {}

MarketDataService::~MarketDataService() {
    log_to_file("[Market] ~MarketDataService()\n");
    stop();
}

void MarketDataService::start() {
    if (th.joinable()) return;
    stop_flag = false;
    th = std::thread([this]() {
        try {
            log_to_file("[Market] thread start\n");
            run();
            log_to_file("[Market] thread exit\n");
        } catch (...) {
            log_to_file("[Market] thread crashed (exception)\n");
        }
    });
}

void MarketDataService::stop() {
    log_to_file("[Market] stop() called\n");
    stop_flag = true;
    if (th.joinable()) th.join();
}

void MarketDataService::run() {
    log_to_file("[Market] run() enter\n");
    std::string mids_json;
    long long last_mids_ms = 0;
    int mids_backoff_ms = 0;
    long long last_heartbeat_ms = 0;

    while (!stop_flag.load()) {
        long long now_ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();

        if (last_heartbeat_ms == 0 || (now_ms - last_heartbeat_ms) > 5000) {
            log_to_file("[Market] heartbeat\n");
            last_heartbeat_ms = now_ms;
        }

        const int mids_interval_ms = (mids_backoff_ms > 0) ? mids_backoff_ms : 2500;
        if (now_ms - last_mids_ms > mids_interval_ms) {
            if (src.fetch_all_mids_raw(mids_json)) {
                model.update_mid_prices_from_allmids_json(mids_json);
                mids_backoff_ms = 0;
            } else {
                mids_backoff_ms = (mids_backoff_ms == 0) ? 5000 : std::min(30000, mids_backoff_ms * 2);
                log_to_file("[Market] allMids fetch failed, backoff=%dms\n", mids_backoff_ms);
            }
            last_mids_ms = now_ms;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace tradeboy::market
