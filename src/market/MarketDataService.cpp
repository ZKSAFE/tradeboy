#include "MarketDataService.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "../model/TradeModel.h"
#include "Hyperliquid.h"
#include "utils/Log.h"

namespace tradeboy::market {

static bool parse_spot_usdc_balance_any(const std::string& json, double& out_usdc) {
    if (tradeboy::market::parse_spot_usdc_balance(json, out_usdc)) return true;

    size_t p = json.find("\"balances\"");
    if (p == std::string::npos) return false;
    size_t win_end = std::min(json.size(), p + (size_t)4096);
    std::string win = json.substr(p, win_end - p);
    if (tradeboy::market::parse_spot_usdc_balance(win, out_usdc)) return true;

    size_t start = json.rfind('{', p);
    size_t end = json.find(']', p);
    if (start != std::string::npos && end != std::string::npos && end > start) {
        std::string win2 = json.substr(start, end - start + 1);
        if (tradeboy::market::parse_spot_usdc_balance(win2, out_usdc)) return true;
    }
    return false;
}

static bool parse_perp_usdc_balance_any(const std::string& json, double& out_usdc) {
    if (tradeboy::market::parse_perp_usdc_balance(json, out_usdc)) return true;

    size_t p = json.find("\"accountValue\"");
    if (p == std::string::npos) return false;
    size_t win_end = std::min(json.size(), p + (size_t)2048);
    std::string win = json.substr(p, win_end - p);
    return tradeboy::market::parse_perp_usdc_balance(win, out_usdc);
}

MarketDataService::MarketDataService(tradeboy::model::TradeModel& model, IMarketDataSource& src)
    : model(model), src(src) {}

MarketDataService::~MarketDataService() {
    log_str("[Market] ~MarketDataService()\n");
    stop();
}

void MarketDataService::start() {
    if (th.joinable()) return;
    stop_flag = false;
    th = std::thread([this]() {
        try {
            log_str("[Market] thread start\n");
            run();
            log_str("[Market] thread exit\n");
        } catch (...) {
            log_str("[Market] thread crashed (exception)\n");
        }
    });
}

void MarketDataService::stop() {
    log_str("[Market] stop() called\n");
    stop_flag = true;
    if (th.joinable()) th.join();
}

void MarketDataService::run() {
    log_str("[Market] run() enter\n");
    std::string mids_json;
    std::string user_json;
    std::string perp_json;
    bool logged_user_dump = false;
    long long last_mids_ms = 0;
    int mids_backoff_ms = 0;
    long long last_user_ms = 0;
    int user_backoff_ms = 0;
    long long last_perp_ms = 0;
    int perp_backoff_ms = 0;
    long long last_heartbeat_ms = 0;
    bool logged_user_fail = false;
    bool logged_perp_dump = false;
    bool logged_perp_fail = false;

    while (!stop_flag.load()) {
        long long now_ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();

        if (last_heartbeat_ms == 0 || (now_ms - last_heartbeat_ms) > 5000) {
            log_str("[Market] heartbeat\n");
            last_heartbeat_ms = now_ms;
        }

        const int mids_interval_ms = (mids_backoff_ms > 0) ? mids_backoff_ms : 2500;
        if (now_ms - last_mids_ms > mids_interval_ms) {
            if (src.fetch_all_mids_raw(mids_json)) {
                model.update_mid_prices_from_allmids_json(mids_json);
                mids_backoff_ms = 0;
            } else {
                mids_backoff_ms = (mids_backoff_ms == 0) ? 5000 : std::min(30000, mids_backoff_ms * 2);
                log_str("[Market] allMids fetch failed\n");
            }
            last_mids_ms = now_ms;
        }

        const int user_interval_ms = (user_backoff_ms > 0) ? user_backoff_ms : 2000;
        if (now_ms - last_user_ms > user_interval_ms) {
            if (src.fetch_spot_clearinghouse_state_raw(user_json)) {
                if (!logged_user_dump) {
                    logged_user_dump = true;
                    log_str("[Market] spotClearinghouseState raw received\n");
                }
                double usdc = 0.0;
                if (parse_spot_usdc_balance_any(user_json, usdc)) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%.2f", usdc);
                    model.set_hl_usdc(usdc, buf, true);
                    user_backoff_ms = 0;
                    logged_user_fail = false;
                } else {
                    if (!logged_user_fail) {
                        logged_user_fail = true;
                        log_str("[Market] spotClearinghouseState parse failed\n");
                    }
                    user_backoff_ms = (user_backoff_ms == 0) ? 5000 : std::min(30000, user_backoff_ms * 2);
                }
            } else {
                user_backoff_ms = (user_backoff_ms == 0) ? 5000 : std::min(30000, user_backoff_ms * 2);
            }
            last_user_ms = now_ms;
        }

        const int perp_interval_ms = (perp_backoff_ms > 0) ? perp_backoff_ms : 3000;
        if (now_ms - last_perp_ms > perp_interval_ms) {
            if (src.fetch_perp_clearinghouse_state_raw(perp_json)) {
                if (!logged_perp_dump) {
                    logged_perp_dump = true;
                    log_str("[Market] clearinghouseState raw received\n");
                }
                double usdc = 0.0;
                if (parse_perp_usdc_balance_any(perp_json, usdc)) {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%.2f", usdc);
                    model.set_hl_perp_usdc(usdc, buf, true);
                    perp_backoff_ms = 0;
                    logged_perp_fail = false;
                } else {
                    if (!logged_perp_fail) {
                        logged_perp_fail = true;
                        log_str("[Market] clearinghouseState parse failed\n");
                    }
                    perp_backoff_ms = (perp_backoff_ms == 0) ? 5000 : std::min(30000, perp_backoff_ms * 2);
                }
            } else {
                perp_backoff_ms = (perp_backoff_ms == 0) ? 5000 : std::min(30000, perp_backoff_ms * 2);
            }
            last_perp_ms = now_ms;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace tradeboy::market
