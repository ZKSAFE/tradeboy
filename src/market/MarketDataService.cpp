#include "MarketDataService.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "../model/TradeModel.h"
#include "Hyperliquid.h"
#include "utils/Log.h"

namespace tradeboy::market {

static bool parse_number_field_str_any(const std::string& s, const char* key, std::string& out) {
    out.clear();
    if (!key || !key[0]) return false;
    std::string needle = std::string("\"") + key + "\"";
    size_t p = s.find(needle);
    if (p == std::string::npos) return false;
    p = s.find(':', p);
    if (p == std::string::npos) return false;
    p++;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t')) p++;
    if (p >= s.size()) return false;

    // value can be quoted string or JSON number
    if (s[p] == '"') {
        size_t start = p + 1;
        size_t end = start;
        while (end < s.size() && s[end] != '"') end++;
        if (end >= s.size()) return false;
        out.assign(s.begin() + start, s.begin() + end);
        return !out.empty();
    }

    size_t start = p;
    size_t end = start;
    while (end < s.size()) {
        char c = s[end];
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') {
            end++;
            continue;
        }
        break;
    }
    if (end == start) return false;
    out.assign(s.begin() + start, s.begin() + end);
    return !out.empty();
}

struct HistoryPoint {
    long long ts_ms = 0;
    double v = 0.0;
};

static bool extract_array_window_after_key(const std::string& s, const char* key_quoted, std::string& out_win) {
    out_win.clear();
    if (!key_quoted || !key_quoted[0]) return false;
    size_t p = s.find(key_quoted);
    if (p == std::string::npos) return false;
    size_t a = s.find('[', p);
    if (a == std::string::npos) return false;

    int depth = 0;
    size_t b = a;
    for (; b < s.size(); b++) {
        char c = s[b];
        if (c == '[') depth++;
        else if (c == ']') {
            depth--;
            if (depth == 0) {
                b++;
                break;
            }
        }
    }
    if (b <= a || b > s.size()) return false;
    out_win = s.substr(a, b - a);
    return true;
}

static bool parse_history_points(const std::string& s, const char* key, std::vector<HistoryPoint>& out) {
    out.clear();
    std::string win;
    std::string keyq = std::string("\"") + key + "\"";
    if (!extract_array_window_after_key(s, keyq.c_str(), win)) return false;

    // Parse pattern: [ [ts,"val"], [ts,"val"], ... ]
    size_t i = 0;
    while (i < win.size()) {
        size_t lb = win.find('[', i);
        if (lb == std::string::npos) break;
        size_t j = lb + 1;
        while (j < win.size() && (win[j] == ' ' || win[j] == '\n' || win[j] == '\r' || win[j] == '\t')) j++;
        // timestamp
        size_t ts_start = j;
        while (j < win.size() && (win[j] >= '0' && win[j] <= '9')) j++;
        if (j == ts_start) {
            i = lb + 1;
            continue;
        }
        long long ts = std::strtoll(win.substr(ts_start, j - ts_start).c_str(), nullptr, 10);
        // comma
        size_t comma = win.find(',', j);
        if (comma == std::string::npos) {
            i = lb + 1;
            continue;
        }
        j = comma + 1;
        while (j < win.size() && (win[j] == ' ' || win[j] == '\n' || win[j] == '\r' || win[j] == '\t')) j++;
        if (j >= win.size() || win[j] != '"') {
            i = lb + 1;
            continue;
        }
        size_t val_start = j + 1;
        size_t val_end = win.find('"', val_start);
        if (val_end == std::string::npos) {
            i = lb + 1;
            continue;
        }
        std::string vs = win.substr(val_start, val_end - val_start);
        double v = std::strtod(vs.c_str(), nullptr);
        HistoryPoint hp;
        hp.ts_ms = ts;
        hp.v = v;
        out.push_back(hp);
        i = val_end + 1;
    }
    return !out.empty();
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
    // The docs mention portfolio, but schema can vary; try several likely keys.
    if (parse_number_field_str_any(s, "accountValue", out)) return true;
    if (parse_number_field_str_any(s, "totalValue", out)) return true;
    if (parse_number_field_str_any(s, "equity", out)) return true;
    if (parse_number_field_str_any(s, "totalEquity", out)) return true;

    // Observed schema on device:
    // ["day", {"accountValueHistory":[[ts,"6.0"],...], ...}]
    const char* hk = "\"accountValueHistory\"";
    size_t p = s.find(hk);
    if (p != std::string::npos) {
        // Find the array bounds for accountValueHistory: "accountValueHistory" : [ ... ]
        size_t a = s.find('[', p);
        if (a != std::string::npos) {
            int depth = 0;
            size_t b = a;
            for (; b < s.size(); b++) {
                char c = s[b];
                if (c == '[') depth++;
                else if (c == ']') {
                    depth--;
                    if (depth == 0) {
                        b++; // include closing bracket
                        break;
                    }
                }
            }
            if (b > a && b <= s.size()) {
                std::string win = s.substr(a, b - a);
                std::string last_num;
                size_t cur = 0;
                while (true) {
                    size_t q = win.find(",\"", cur);
                    if (q == std::string::npos) break;
                    size_t val_start = q + 2;
                    size_t val_end = win.find('"', val_start);
                    if (val_end == std::string::npos) break;
                    if (val_end > val_start) {
                        std::string cand = win.substr(val_start, val_end - val_start);
                        bool ok = true;
                        for (size_t i = 0; i < cand.size(); i++) {
                            char cc = cand[i];
                            if ((cc >= '0' && cc <= '9') || cc == '.' || cc == '-' || cc == '+') continue;
                            ok = false;
                            break;
                        }
                        if (ok && !cand.empty()) {
                            last_num = cand;
                        }
                    }
                    cur = val_end + 1;
                }
                if (!last_num.empty()) {
                    out = last_num;
                    return true;
                }
            }
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
