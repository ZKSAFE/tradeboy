#include "MarketDataService.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <unordered_set>
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

static bool pj_get_string_like(const picojson::value& v, std::string& out) {
    out.clear();
    if (v.is<std::string>()) {
        out = v.get<std::string>();
        return true;
    }
    if (v.is<double>()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", v.get<double>());
        out = buf;
        return true;
    }
    return false;
}

static bool pj_get_double_like(const picojson::value& v, double& out) {
    if (v.is<double>()) {
        out = v.get<double>();
        return true;
    }
    if (v.is<std::string>()) {
        out = std::strtod(v.get<std::string>().c_str(), nullptr);
        return true;
    }
    return false;
}

static int infer_decimals_from_px_string(const std::string& s) {
    size_t dot = s.find('.');
    if (dot == std::string::npos) return 0;
    size_t end = s.size();
    while (end > dot + 1 && s[end - 1] == '0') end--;
    if (end <= dot + 1) return 0;
    int d = (int)(end - (dot + 1));
    if (d < 0) d = 0;
    if (d > 10) d = 10;
    return d;
}

static std::string map_token_display_sym(const std::string& token_name, const std::string& token_full_name) {
    // Website display-name overrides (derived from app.hyperliquid.xyz bundle mapping).
    // These tokens exist on L1 as distinct assets, but the UI displays the canonical ticker.
    struct NameMap {
        const char* l1;
        const char* disp;
    };
    static const NameMap kOverrides[] = {
        {"UBTC", "BTC"},
        {"UETH", "ETH"},
        {"USOL", "SOL"},
        {"UPUMP", "PUMP"},
        {"UBONK", "BONK"},
        {"UMON", "MON"},
        {"MON", "MONPRO"},
        {"UFART", "FARTCOIN"},
        {"UXPL", "XPL"},
        {"UENA", "ENA"},
        {"HPENGU", "PENGU"},
        {"UDZ", "2Z"},
        {"USDE", "USDE"},
        {"FEUSD", "FEUSD"},
        {"USDHL", "USDHL"},
        {"MMOVE", "MOVE"},
        {"USDT0", "USDT"},
        {"XAUT0", "XAUT"},
        {"LINK0", "LINK"},
        {"TRX0", "TRX"},
        {"AAVE0", "AAVE"},
        {"AVAX0", "AVAX"},
        {"PEPE0", "PEPE"},
        {"BNB1", "BNB"},
        {"XMR1", "XMR"},
    };
    for (size_t i = 0; i < sizeof(kOverrides) / sizeof(kOverrides[0]); i++) {
        if (token_name == kOverrides[i].l1) return kOverrides[i].disp;
    }

    if (!token_full_name.empty()) {
        const std::string prefix = "Unit ";
        if (token_full_name.size() > prefix.size() && token_full_name.compare(0, prefix.size(), prefix) == 0) {
            const std::string base = token_full_name.substr(prefix.size());
            if (base == "Bitcoin") return "BTC";
            if (base == "Ethereum") return "ETH";
            if (base == "Solana") return "SOL";
            if (base == "Pump Fun") return "PUMP";
            if (base == "Bonk") return "BONK";
        }

        // HyBridge-style tokens often have a numeric suffix in name (e.g. LINK0, TRX0, AVAX0).
        // The UI on app.hyperliquid.xyz typically displays the base ticker.
        // Prefer extracting an uppercase ticker from fullName when present.
        auto is_upper_alnum = [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        };
        for (size_t i = 0; i < token_full_name.size(); i++) {
            if (token_full_name[i] == '$' && (i + 1) < token_full_name.size()) {
                size_t j = i + 1;
                while (j < token_full_name.size() && is_upper_alnum(token_full_name[j])) j++;
                if (j > i + 1) {
                    std::string t = token_full_name.substr(i + 1, j - (i + 1));
                    // Example: "$PEPE0" should display as "PEPE".
                    while (!t.empty() && t.back() >= '0' && t.back() <= '9') t.pop_back();
                    if (!t.empty()) return t;
                }
            }
        }
        // If fullName contains a phrase like "... powered LINK ..." or starts with "AAVE ...",
        // use the first all-caps token found.
        {
            std::string best;
            std::string cur;
            for (size_t i = 0; i <= token_full_name.size(); i++) {
                char c = (i < token_full_name.size()) ? token_full_name[i] : ' ';
                if (is_upper_alnum(c)) {
                    cur.push_back(c);
                } else {
                    if (cur.size() >= 3) {
                        while (!cur.empty() && cur.back() >= '0' && cur.back() <= '9') cur.pop_back();
                        if (cur.size() >= 3) {
                            best = cur;
                            break;
                        }
                    }
                    cur.clear();
                }
            }
            if (!best.empty()) return best;
        }
    }
    return token_name;
}

static bool build_spot_rows_from_spot_meta_and_ctxs(const std::string& spot_meta_and_ctxs_json,
                                                    std::vector<tradeboy::model::SpotRow>& out_rows) {
    out_rows.clear();
    picojson::value root;
    std::string err = picojson::parse(root, spot_meta_and_ctxs_json);
    if (!err.empty()) return false;
    if (!root.is<picojson::array>()) return false;

    const picojson::array& top = root.get<picojson::array>();
    if (top.size() < 2) return false;

    const picojson::object* meta = pj_get_obj(top[0]);
    const picojson::array* ctxs = pj_get_arr(top[1]);
    if (!meta || !ctxs) return false;

    const picojson::value* tokens_v = pj_find(*meta, "tokens");
    const picojson::value* uni_v = pj_find(*meta, "universe");
    const picojson::array* tokens = tokens_v ? pj_get_arr(*tokens_v) : nullptr;
    const picojson::array* universe = uni_v ? pj_get_arr(*uni_v) : nullptr;
    if (!tokens || !universe) return false;

    std::unordered_map<std::string, const picojson::object*> ctx_by_coin;
    ctx_by_coin.reserve(ctxs->size());
    for (size_t i = 0; i < ctxs->size(); i++) {
        const picojson::object* ctx = pj_get_obj((*ctxs)[i]);
        if (!ctx) continue;
        const picojson::value* cv = pj_find(*ctx, "coin");
        std::string ck;
        if (!cv || !pj_get_string_like(*cv, ck) || ck.empty()) continue;
        ctx_by_coin[ck] = ctx;
    }

    // NOTE: USDC token index is 0 in current Hyperliquid spot metadata.
    // We only keep markets quoted in USDC.
    const int usdc_token_idx = 0;

    static const char* kExcludeSyms[] = {
        "XMR",
        "CEX",
        "ZEC",
        "PUP",
        "XRP",
        "AAVE",
        "LINK",
        "GYAN",
        "WOULD",
        "WMNT",
        "ANON",
        "ADHD",
        "SLV",
        "THBILL",
        "CRCL",
        "LICK",
        "SPH",
        "VEGAS",
        "AVAX",
        "LAUNCH",
        "USD",
        "SWAP",
        "DEX",
        "KITTEN",
        "FLR",
        "TIME",
        "HOLD",
        "AUTIST",
        "OTTI",
        "LQNA",
        "HPYH",
        "RICH",
        "FARM",
        "BRIDGE",
        "HOOD",
        "HYENA",
        "FUND",
        "COZY",
        "YEETI",
        "BNB",
        "NIGGO",
        "FLASK",
        "GOOGL",
        "P2E",
        "HORSY",
        "DEPIN",
        "UPHL",
        "TJIF",
        "FRIED",
        "LOOP",
        "RETARD",
        "DROP",
        "PERP",
        "LIQUID",
        "HWAVE",
        "ASI",
        "PURRO",
        "AAPL",
        "PEAR",
        "EX",
        "HREKT",
        "GPT",
        "DAO",
        "LADY",
        "SELL",
        "JPEG",
        "EARTH",
        "SENT",
        "STRICT",
        "SHREK",
        "ALL",
        "CZ",
        "RUG",
        "WASH",
        "FLY",
        "VORTX",
        "COPE",
        "PENIS",
        "CHEF",
        "LATINA",
        "CAT",
        "LICK0",
        "MANLET",
        "SIX",
        "WAGMI",
        "CAPPY",
        "TRUMP",
        "GMEOW",
        "XULIAN",
        "ILIENS",
        "FUCKY",
        "BAGS",
        "ANSEM",
        "TATE",
        "KOBE",
        "HAPPY",
        "BIGBEN",
    };
    std::unordered_set<std::string> exclude;
    exclude.reserve(sizeof(kExcludeSyms) / sizeof(kExcludeSyms[0]));
    for (size_t i = 0; i < sizeof(kExcludeSyms) / sizeof(kExcludeSyms[0]); i++) {
        exclude.insert(kExcludeSyms[i]);
    }

    out_rows.reserve(universe->size());
    for (size_t i = 0; i < universe->size(); i++) {
        const picojson::object* pair = pj_get_obj((*universe)[i]);
        if (!pair) continue;

        std::string name;
        {
            const picojson::value* nv = pj_find(*pair, "name");
            if (!nv || !pj_get_string_like(*nv, name) || name.empty()) continue;
        }

        // Filter: only keep pairs whose quote token is USDC.
        {
            const picojson::value* tv = pj_find(*pair, "tokens");
            const picojson::array* toks = tv ? pj_get_arr(*tv) : nullptr;
            int quote_idx = -1;
            if (toks && toks->size() >= 2 && (*toks)[1].is<double>()) {
                quote_idx = (int)(*toks)[1].get<double>();
            }
            if (quote_idx != usdc_token_idx) continue;
        }

        int index = -1;
        {
            const picojson::value* iv = pj_find(*pair, "index");
            if (iv && iv->is<double>()) index = (int)iv->get<double>();
        }
        if (index < 0) continue;

        bool isCanonical = false;
        {
            const picojson::value* cv = pj_find(*pair, "isCanonical");
            if (cv && cv->is<bool>()) isCanonical = cv->get<bool>();
        }

        // Price key for allMids:
        // - canonical: BASE (e.g. BTC)
        // - non-canonical: @<index>
        std::string base_sym;
        {
            size_t slash = name.find('/');
            base_sym = (slash == std::string::npos) ? name : name.substr(0, slash);
        }
        std::string price_key = isCanonical ? base_sym : (std::string("@") + std::to_string(index));

        std::string display_sym;
        std::string display_full;
        int fallback_decimals = 2;
        {
            const picojson::value* tv = pj_find(*pair, "tokens");
            const picojson::array* toks = tv ? pj_get_arr(*tv) : nullptr;
            if (toks && toks->size() >= 1) {
                int base_idx = -1;
                if ((*toks)[0].is<double>()) base_idx = (int)(*toks)[0].get<double>();
                if (base_idx >= 0 && (size_t)base_idx < tokens->size()) {
                    const picojson::object* tok = pj_get_obj((*tokens)[(size_t)base_idx]);
                    if (tok) {
                        const picojson::value* tname = pj_find(*tok, "name");
                        if (tname) (void)pj_get_string_like(*tname, display_sym);
                        const picojson::value* tfull = pj_find(*tok, "fullName");
                        if (tfull) (void)pj_get_string_like(*tfull, display_full);
                        const picojson::value* sdv = pj_find(*tok, "szDecimals");
                        if (sdv && sdv->is<double>()) fallback_decimals = std::max(0, (int)sdv->get<double>());
                    }
                }
            }
        }

        if (!display_sym.empty()) {
            display_sym = map_token_display_sym(display_sym, display_full);
        }
        if (display_sym.empty()) {
            size_t slash = name.find('/');
            display_sym = (slash == std::string::npos) ? name : name.substr(0, slash);
        }

        tradeboy::model::SpotRow r(price_key, display_sym, 0.0, 0.0, 0.0, 0.0);
        r.price_decimals = fallback_decimals;

        // assetCtxs coin key can be one of:
        // - BASE (e.g. BTC)
        // - @<index>
        // - BASE/USDC (e.g. PURR/USDC)
        const picojson::object* ctxp = nullptr;
        {
            auto it = ctx_by_coin.find(price_key);
            if (it != ctx_by_coin.end()) ctxp = it->second;
        }
        if (!ctxp) {
            const std::string alt = std::string("@") + std::to_string(index);
            auto it = ctx_by_coin.find(alt);
            if (it != ctx_by_coin.end()) ctxp = it->second;
        }
        if (!ctxp) {
            auto it = ctx_by_coin.find(name);
            if (it != ctx_by_coin.end()) ctxp = it->second;
        }

        if (ctxp) {
            const picojson::object& ctx = *ctxp;
            double prev = 0.0;
            double dayv = 0.0;
            double dayb = 0.0;
            double mid = 0.0;
            const picojson::value* pv = pj_find(ctx, "prevDayPx");
            const picojson::value* bv = pj_find(ctx, "dayBaseVlm");
            const picojson::value* dv = pj_find(ctx, "dayNtlVlm");
            const picojson::value* mv = pj_find(ctx, "midPx");
            if (pv && pj_get_double_like(*pv, prev)) r.prev_day_px = prev;
            if (bv && pj_get_double_like(*bv, dayb)) r.day_base_vlm = dayb;
            if (dv && pj_get_double_like(*dv, dayv)) r.day_ntl_vlm = dayv;
            if (mv && pj_get_double_like(*mv, mid)) r.price = mid;

            if (mv) {
                std::string mvs;
                if (pj_get_string_like(*mv, mvs) && !mvs.empty()) {
                    r.price_decimals = infer_decimals_from_px_string(mvs);
                }
            }
        }

        if (exclude.find(r.sym) != exclude.end()) {
            continue;
        }

        out_rows.push_back(std::move(r));
    }

    std::stable_sort(out_rows.begin(), out_rows.end(), [](const tradeboy::model::SpotRow& a, const tradeboy::model::SpotRow& b) {
        if (a.day_ntl_vlm != b.day_ntl_vlm) return a.day_ntl_vlm > b.day_ntl_vlm;
        return a.sym < b.sym;
    });
    if (out_rows.size() > 20) out_rows.resize(20);

    return !out_rows.empty();
}

static bool parse_spot_balances_by_coin(const std::string& spot_state_json, std::unordered_map<std::string, double>& out) {
    out.clear();
    picojson::value root;
    std::string err = picojson::parse(root, spot_state_json);
    if (!err.empty()) return false;
    const picojson::object* obj = pj_get_obj(root);
    if (!obj) return false;
    const picojson::value* bv = pj_find(*obj, "balances");
    const picojson::array* bal = bv ? pj_get_arr(*bv) : nullptr;
    if (!bal) return false;
    for (size_t i = 0; i < bal->size(); i++) {
        const picojson::object* row = pj_get_obj((*bal)[i]);
        if (!row) continue;
        std::string coin;
        double total = 0.0;
        const picojson::value* cv = pj_find(*row, "coin");
        const picojson::value* tv = pj_find(*row, "total");
        if (!cv || !tv) continue;
        if (!pj_get_string_like(*cv, coin) || coin.empty()) continue;
        if (!pj_get_double_like(*tv, total)) continue;
        out[coin] = total;
    }
    return true;
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
    std::string perp_meta_json;
    std::string spot_meta_json;
    bool spot_rows_initialized = false;
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
    bool perp_meta_done = false;
    bool spot_meta_done = false;

    while (!stop_flag.load()) {
        long long now_ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();

        if (last_heartbeat_ms == 0 || (now_ms - last_heartbeat_ms) > 5000) {
            log_str("[Market] heartbeat\n");
            last_heartbeat_ms = now_ms;
        }

        if (!perp_meta_done) {
            const std::string req = std::string("{\"type\":\"allPerpMetas\"}\n");
            if (tradeboy::market::fetch_info_raw(req, perp_meta_json)) {
                model.set_hl_perp_meta_json(perp_meta_json, true);
                perp_meta_done = true;
                log_str("[HL] allPerpMetas cached\n");
            }
        }

        if (!spot_meta_done) {
            const std::string req = std::string("{\"type\":\"spotMetaAndAssetCtxs\"}\n");
            if (tradeboy::market::fetch_info_raw(req, spot_meta_json)) {
                model.set_hl_spot_meta_json(spot_meta_json, true);
                spot_meta_done = true;
                log_str("[HL] spotMetaAndAssetCtxs cached\n");
            }
        }

        if (spot_meta_done && !spot_rows_initialized) {
            std::vector<tradeboy::model::SpotRow> rows;
            if (build_spot_rows_from_spot_meta_and_ctxs(spot_meta_json, rows)) {
                int btc_idx = -1;
                for (size_t i = 0; i < rows.size(); i++) {
                    if (rows[i].sym == "BTC") {
                        btc_idx = (int)i;
                        break;
                    }
                }
                model.set_spot_rows(std::move(rows));
                if (btc_idx >= 0) model.set_spot_row_idx(btc_idx);
                spot_rows_initialized = true;
                log_str("[Model] spot_rows initialized from spotMetaAndAssetCtxs\n");
            }
        }

        const int mids_interval_ms = (mids_backoff_ms > 0) ? mids_backoff_ms : 2500;
        if (now_ms - last_mids_ms > mids_interval_ms) {
            if (src.fetch_all_mids_raw(mids_json)) {
                model.update_mid_prices_from_allmids_json(mids_json);
                model.sort_spot_rows();
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

                    std::unordered_map<std::string, double> balances;
                    if (parse_spot_balances_by_coin(user_json, balances)) {
                        model.update_spot_balances(balances);
                        model.sort_spot_rows();
                    }
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
