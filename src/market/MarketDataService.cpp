#include "MarketDataService.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "../../third_party/picojson/picojson.h"

#include "../model/TradeModel.h"
#include "Hyperliquid.h"
#include "utils/Log.h"

namespace tradeboy::market {

struct HistoryPoint {
    long long ts_ms = 0;
    double v = 0.0;
};

static bool pj_get_number_like(const picojson::value& v, double& out_num, std::string& out_str) {
    out_str.clear();
    if (v.is<double>()) {
        out_num = v.get<double>();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", out_num);
        out_str = buf;
        return true;
    }
    if (v.is<std::string>()) {
        out_str = v.get<std::string>();
        out_num = std::strtod(out_str.c_str(), nullptr);
        return true;
    }
    return false;
}

static const picojson::object* pj_get_obj(const picojson::value& v) {
    if (!v.is<picojson::object>()) return nullptr;
    return &v.get<picojson::object>();
}

static const picojson::array* pj_get_arr(const picojson::value& v) {
    if (!v.is<picojson::array>()) return nullptr;
    return &v.get<picojson::array>();
}

static const picojson::value* pj_find(const picojson::object& obj, const char* key) {
    picojson::object::const_iterator it = obj.find(key);
    if (it == obj.end()) return nullptr;
    return &it->second;
}

static bool pj_parse_portfolio_root_obj(const std::string& s, picojson::object& out_obj) {
    out_obj.clear();
    picojson::value root;
    std::string err = picojson::parse(root, s);
    if (!err.empty()) return false;

    if (root.is<picojson::object>()) {
        out_obj = root.get<picojson::object>();
        return true;
    }
    if (root.is<picojson::array>()) {
        const picojson::array& a = root.get<picojson::array>();
        // Observed schemas:
        // - ["day", { ... }]
        // - [["day", { ... }], ...]
        if (!a.empty() && a[0].is<picojson::array>()) {
            const picojson::array& row = a[0].get<picojson::array>();
            if (row.size() >= 2 && row[1].is<picojson::object>()) {
                out_obj = row[1].get<picojson::object>();
                return true;
            }
        }
        if (a.size() >= 2 && a[1].is<picojson::object>()) {
            out_obj = a[1].get<picojson::object>();
            return true;
        }
    }
    return false;
}

static bool pj_parse_history_points_from_obj(const picojson::object& obj, const char* key, std::vector<HistoryPoint>& out) {
    out.clear();
    const picojson::value* hv = pj_find(obj, key);
    if (!hv) return false;
    const picojson::array* arr = pj_get_arr(*hv);
    if (!arr) return false;

    for (size_t i = 0; i < arr->size(); i++) {
        const picojson::array* row = pj_get_arr((*arr)[i]);
        if (!row || row->size() < 2) continue;

        long long ts = 0;
        if ((*row)[0].is<double>()) ts = (long long)(*row)[0].get<double>();
        else if ((*row)[0].is<std::string>()) ts = std::strtoll((*row)[0].get<std::string>().c_str(), nullptr, 10);
        else continue;

        double num = 0.0;
        std::string str;
        if (!pj_get_number_like((*row)[1], num, str)) continue;

        HistoryPoint hp;
        hp.ts_ms = ts;
        hp.v = num;
        out.push_back(hp);
    }

    return !out.empty();
}

static bool parse_history_points(const std::string& s, const char* key, std::vector<HistoryPoint>& out) {
    out.clear();
    picojson::object obj;
    if (!pj_parse_portfolio_root_obj(s, obj)) return false;
    return pj_parse_history_points_from_obj(obj, key, out);
}

static bool compute_24h_pnl_from_history(const std::vector<HistoryPoint>& pts,
                                        long long now_ms,
                                        double& out_pnl) {
    out_pnl = 0.0;
    if (pts.size() < 2) return false;

    const long long day_ms = 24LL * 60LL * 60LL * 1000LL;
    const long long target = now_ms - day_ms;
    const HistoryPoint& last = pts.back();

    // Find latest point at or before target.
    long long best_ts = -1;
    double best_v = pts.front().v;
    for (size_t i = 0; i < pts.size(); i++) {
        if (pts[i].ts_ms <= target && pts[i].ts_ms > best_ts) {
            best_ts = pts[i].ts_ms;
            best_v = pts[i].v;
        }
    }

    out_pnl = last.v - best_v;
    return true;
}

static bool parse_account_value_str(const std::string& s, std::string& out) {
    out.clear();
    picojson::object obj;
    if (!pj_parse_portfolio_root_obj(s, obj)) return false;

    const char* keys[] = {"accountValue", "totalValue", "equity", "totalEquity"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        const picojson::value* v = pj_find(obj, keys[i]);
        if (!v) continue;
        double num = 0.0;
        std::string str;
        if (pj_get_number_like(*v, num, str) && !str.empty()) {
            out = str;
            return true;
        }
    }

    const picojson::value* hv = pj_find(obj, "accountValueHistory");
    if (!hv) return false;
    const picojson::array* h = pj_get_arr(*hv);
    if (!h || h->empty()) return false;

    // History format: [[ts,"val"], ...]
    for (size_t i = h->size(); i > 0; i--) {
        const picojson::array* row = pj_get_arr((*h)[i - 1]);
        if (!row || row->size() < 2) continue;
        double num = 0.0;
        std::string str;
        if (pj_get_number_like((*row)[1], num, str) && !str.empty()) {
            out = str;
            return true;
        }
    }

    return false;
}

static void log_portfolio_prefix_once(const std::string& s) {
    static bool logged = false;
    if (logged) return;
    logged = true;
    std::string prefix = s;
    if (prefix.size() > 400) prefix.resize(400);
    for (size_t i = 0; i < prefix.size(); i++) {
        if (prefix[i] == '\n' || prefix[i] == '\r' || prefix[i] == '\t') prefix[i] = ' ';
    }
    std::string dbg = std::string("[HL] portfolio prefix=") + prefix + "\n";
    log_str(dbg.c_str());
}

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
    std::string portfolio_json;
    bool logged_user_dump = false;
    long long last_mids_ms = 0;
    int mids_backoff_ms = 0;
    long long last_user_ms = 0;
    int user_backoff_ms = 0;
    long long last_perp_ms = 0;
    int perp_backoff_ms = 0;
    long long last_portfolio_ms = 0;
    long long last_heartbeat_ms = 0;
    bool logged_user_fail = false;
    bool logged_perp_dump = false;
    bool logged_perp_fail = false;
    bool logged_portfolio_once = false;
    bool portfolio_failed_once = false;

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

        // Minimal portfolio logging: only log TOTAL_ASSET_VALUE once (or every 30s if it succeeds).
        const int portfolio_interval_ms = (logged_portfolio_once || portfolio_failed_once) ? 30000 : 5000;
        if (now_ms - last_portfolio_ms > portfolio_interval_ms) {
            tradeboy::model::WalletSnapshot w = model.wallet_snapshot();
            if (!w.wallet_address.empty()) {
                std::string req = std::string("{\"type\":\"portfolio\",\"user\":\"") + w.wallet_address + "\"}\n";
                if (tradeboy::market::fetch_info_raw(req, portfolio_json)) {
                    std::string v;
                    if (parse_account_value_str(portfolio_json, v)) {
                        std::string line = std::string("[HL] TOTAL_ASSET_VALUE=") + v + "\n";
                        log_str(line.c_str());

                        double total_asset = std::strtod(v.c_str(), nullptr);
                        double pnl24 = 0.0;
                        double pct = 0.0;
                        bool ok = true;

                        // 24H_PNL_FLUX from pnlHistory
                        std::vector<HistoryPoint> pnl_pts;
                        if (parse_history_points(portfolio_json, "pnlHistory", pnl_pts)) {
                            if (compute_24h_pnl_from_history(pnl_pts, now_ms, pnl24)) {
                                char buf[96];
                                std::snprintf(buf, sizeof(buf), "[HL] 24H_PNL_FLUX=%+.6f\n", pnl24);
                                log_str(buf);

                                if (total_asset != 0.0) {
                                    pct = (pnl24 / total_asset) * 100.0;
                                    char buf2[96];
                                    std::snprintf(buf2, sizeof(buf2), "[HL] 24H_PNL_FLUX_PCT=%+.4f%%\n", pct);
                                    log_str(buf2);
                                }
                            } else {
                                log_str("[HL] 24H_PNL_FLUX compute failed\n");
                                ok = false;
                            }
                        } else {
                            log_str("[HL] pnlHistory parse failed\n");
                            ok = false;
                        }

                        {
                            char total_buf[64];
                            char pnl_buf[64];
                            char pct_buf[64];
                            std::snprintf(total_buf, sizeof(total_buf), "$%.2f", total_asset);
                            std::snprintf(pnl_buf, sizeof(pnl_buf), "%+.6f", pnl24);
                            std::snprintf(pct_buf, sizeof(pct_buf), "(%+.4f%%)", pct);
                            model.set_hl_portfolio(total_asset,
                                                   total_buf,
                                                   pnl24,
                                                   pnl_buf,
                                                   pct,
                                                   pct_buf,
                                                   ok);
                            log_str("[Model] hl_portfolio updated\n");
                        }

                        logged_portfolio_once = true;
                    } else {
                        log_str("[HL] TOTAL_ASSET_VALUE parse failed\n");
                        log_portfolio_prefix_once(portfolio_json);
                        portfolio_failed_once = true;
                    }
                } else {
                    log_str("[HL] portfolio fetch failed\n");
                    portfolio_failed_once = true;
                }
            }
            last_portfolio_ms = now_ms;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

} // namespace tradeboy::market
