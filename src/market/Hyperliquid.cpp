#include "Hyperliquid.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

#include "../../third_party/picojson/picojson.h"

#include "utils/Log.h"

namespace tradeboy::market {

static bool write_file(const char* path, const std::string& s) {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f.good()) return false;
    f << s;
    return true;
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

static std::string map_token_display_sym(const std::string& token_name, const std::string& token_full_name) {
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

        auto is_upper_alnum = [](char c) {
            return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
        };
        for (size_t i = 0; i < token_full_name.size(); i++) {
            if (token_full_name[i] == '$' && (i + 1) < token_full_name.size()) {
                size_t j = i + 1;
                while (j < token_full_name.size() && is_upper_alnum(token_full_name[j])) j++;
                if (j > i + 1) {
                    std::string t = token_full_name.substr(i + 1, j - (i + 1));
                    while (!t.empty() && t.back() >= '0' && t.back() <= '9') t.pop_back();
                    if (!t.empty()) return t;
                }
            }
        }
    }

    return token_name;
}

static bool pj_parse_root_object(const std::string& s, picojson::object& out_obj) {
    out_obj.clear();
    picojson::value root;
    std::string err = picojson::parse(root, s);
    if (!err.empty()) return false;
    if (!root.is<picojson::object>()) return false;
    out_obj = root.get<picojson::object>();
    return true;
}

static bool hl_post_file(const char* json_path, std::string& out_json);

bool fetch_info_raw(const std::string& request_json, std::string& out_json) {
    const char* path = "/tmp/hl_req.json";
    if (!write_file(path, request_json)) return false;
    return hl_post_file(path, out_json);
}

bool fetch_user_role_raw(const std::string& user_address_0x, std::string& out_json) {
    std::string req = std::string("{\"type\":\"userRole\",\"user\":\"") + user_address_0x + "\"}\n";
    return fetch_info_raw(req, out_json);
}

bool fetch_spot_clearinghouse_state_raw(const std::string& user_address_0x, std::string& out_json) {
    std::string req = std::string("{\"type\":\"spotClearinghouseState\",\"user\":\"") + user_address_0x + "\"}\n";
    return fetch_info_raw(req, out_json);
}

bool fetch_perp_clearinghouse_state_raw(const std::string& user_address_0x, std::string& out_json) {
    std::string req = std::string("{\"type\":\"clearinghouseState\",\"user\":\"") + user_address_0x + "\"}\n";
    return fetch_info_raw(req, out_json);
}

static bool run_cmd_capture(const std::string& cmd, std::string& out) {
    out.clear();
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return false;
    char buf[4096];
    while (true) {
        size_t n = fread(buf, 1, sizeof(buf), p);
        if (n > 0) out.append(buf, n);
        if (n < sizeof(buf)) break;
    }
    int rc = pclose(p);
    return rc == 0 && !out.empty();
}

static const char* resolve_curl_path() {
    static const char* path = nullptr;
    if (path) return path;
    const char* candidates[] = {
        "/opt/homebrew/bin/curl",
        "/opt/homebrew/opt/curl/bin/curl",
        "/usr/local/bin/curl",
        "/usr/local/opt/curl/bin/curl",
        "/usr/bin/curl"
    };
    for (const char* candidate : candidates) {
        FILE* f = std::fopen(candidate, "rb");
        if (f) {
            std::fclose(f);
            path = candidate;
            return path;
        }
    }
    path = "/usr/bin/curl";
    return path;
}

static bool hl_post_file(const char* json_path, std::string& out_json) {
#if defined(TRADEBOY_DESKTOP)
    std::string cmd = std::string(resolve_curl_path()) + " -sS -H \"Content-Type: application/json\" --data-binary @";
    cmd += json_path;
    cmd += " https://api.hyperliquid.xyz/info";
    if (run_cmd_capture(cmd, out_json)) return true;

    std::string diag;
    std::string cmd2 = std::string(resolve_curl_path()) + " -sS -D - -o - -H \"Content-Type: application/json\" --data-binary @";
    cmd2 += json_path;
    cmd2 += " https://api.hyperliquid.xyz/info 2>&1";
    run_cmd_capture(cmd2, diag);
#else
    // NOTE: we rely on /usr/bin/wget existing on device. The builder does not ship TLS libs.
    // -qO- prints response body to stdout.
    std::string cmd = "/usr/bin/wget -qO- --header=\"Content-Type: application/json\" --post-file=";
    cmd += json_path;
    cmd += " https://api.hyperliquid.xyz/info";
    if (run_cmd_capture(cmd, out_json)) return true;

    // Retry once with headers + stderr to help diagnose failures (rate limit, DNS, etc.).
    std::string diag;
    std::string cmd2 = "/usr/bin/wget -S -O- --header=\"Content-Type: application/json\" --post-file=";
    cmd2 += json_path;
    cmd2 += " https://api.hyperliquid.xyz/info 2>&1";
    run_cmd_capture(cmd2, diag);
#endif
    out_json = diag;
    return false;
}

bool fetch_all_mids_raw(std::string& out_json) {
    const char* path = "/tmp/hl_allmids.json";
    if (!write_file(path, "{\"type\":\"allMids\"}\n")) return false;
    return hl_post_file(path, out_json);
}

static bool parse_quoted_value(const std::string& s, size_t start, std::string& out) {
    // expects s[start] == '"'
    if (start >= s.size() || s[start] != '"') return false;
    size_t i = start + 1;
    while (i < s.size()) {
        if (s[i] == '\\') {
            i += 2;
            continue;
        }
        if (s[i] == '"') {
            out.assign(s.begin() + (start + 1), s.begin() + i);
            return true;
        }
        i++;
    }
    return false;
}

static bool parse_json_string_field(const std::string& s, const std::string& key, std::string& out) {
    std::string needle = "\"" + key + "\":";
    size_t p = s.find(needle);
    if (p == std::string::npos) return false;
    p += needle.size();
    while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t')) p++;
    if (p >= s.size() || s[p] != '"') return false;
    return parse_quoted_value(s, p, out);
}

static bool parse_json_number_string_field(const std::string& s, const std::string& key, std::string& out) {
    // Many HL numeric fields are encoded as strings, but sometimes they may be real JSON numbers.
    // Accept either "123.45" or 123.45.
    std::string needle = "\"" + key + "\":";
    size_t p = s.find(needle);
    if (p == std::string::npos) return false;
    p += needle.size();
    while (p < s.size() && (s[p] == ' ' || s[p] == '\n' || s[p] == '\r' || s[p] == '\t')) p++;
    if (p >= s.size()) return false;
    if (s[p] == '"') {
        return parse_quoted_value(s, p, out);
    }
    // Parse a JSON number token.
    size_t start = p;
    while (p < s.size()) {
        char c = s[p];
        if ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') {
            p++;
            continue;
        }
        break;
    }
    if (p == start) return false;
    out.assign(s.begin() + start, s.begin() + p);
    return true;
}

bool parse_usdc_deposit_address(const std::string& user_role_json, std::string& out_addr) {
    out_addr.clear();
    picojson::object obj;
    if (!pj_parse_root_object(user_role_json, obj)) {
        if (parse_json_string_field(user_role_json, "usdcDepositAddress", out_addr)) return true;
        if (parse_json_string_field(user_role_json, "depositAddress", out_addr)) return true;
        if (parse_json_string_field(user_role_json, "usdc_deposit_address", out_addr)) return true;
        if (parse_json_string_field(user_role_json, "deposit_address", out_addr)) return true;
        return false;
    }

    const char* keys[] = {"usdcDepositAddress", "depositAddress", "usdc_deposit_address", "deposit_address"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        const picojson::value* v = pj_find(obj, keys[i]);
        if (!v) continue;
        std::string s;
        if (pj_get_string_like(*v, s) && !s.empty()) {
            out_addr = s;
            return true;
        }
    }
    return false;
}

static bool parse_spot_usdc_balance_coin_total(const std::string& s, double& out_usdc) {
    // Look for an object like {"coin":"USDC", ... "total":"123.45"}
    size_t p = s.find("\"coin\":\"USDC\"");
    if (p == std::string::npos) return false;
    size_t win_end = s.find('}', p);
    if (win_end == std::string::npos) win_end = std::min(s.size(), p + (size_t)2048);
    std::string win = s.substr(p, win_end - p);
    std::string v;
    if (!parse_json_number_string_field(win, "total", v)) {
        if (!parse_json_number_string_field(win, "available", v)) return false;
    }
    out_usdc = std::strtod(v.c_str(), nullptr);
    return true;
}

static bool parse_spot_usdc_balance_token0(const std::string& s, double& out_usdc) {
    // Fallback: if USDC token index is 0, some responses contain {"token":0, ...}
    size_t p = s.find("\"token\":0");
    if (p == std::string::npos) return false;
    size_t win_end = s.find('}', p);
    if (win_end == std::string::npos) win_end = std::min(s.size(), p + (size_t)2048);
    std::string win = s.substr(p, win_end - p);
    std::string v;
    if (!parse_json_number_string_field(win, "total", v)) {
        if (!parse_json_number_string_field(win, "balance", v)) return false;
    }
    out_usdc = std::strtod(v.c_str(), nullptr);
    return true;
}

bool parse_spot_usdc_balance(const std::string& spot_state_json, double& out_usdc) {
    out_usdc = 0.0;
    // If the account has never used HL spot, balances may be an empty array.
    // Treat that as a valid 0 balance instead of a parse failure.
    if (spot_state_json.find("\"balances\":[ ][]") != std::string::npos) return true;
    if (spot_state_json.find("\"balances\":[]") != std::string::npos) return true;
    if (spot_state_json.find("\"balances\": []") != std::string::npos) return true;

    picojson::object obj;
    if (pj_parse_root_object(spot_state_json, obj)) {
        const picojson::value* bv = pj_find(obj, "balances");
        const picojson::array* balances = bv ? pj_get_arr(*bv) : nullptr;
        if (balances) {
            if (balances->empty()) {
                out_usdc = 0.0;
                return true;
            }
            for (size_t i = 0; i < balances->size(); i++) {
                const picojson::object* bobj = pj_get_obj((*balances)[i]);
                if (!bobj) continue;

                std::string coin;
                const picojson::value* coin_v = pj_find(*bobj, "coin");
                if (coin_v && coin_v->is<std::string>()) coin = coin_v->get<std::string>();

                bool is_usdc = (coin == "USDC");
                if (!is_usdc) {
                    const picojson::value* tok = pj_find(*bobj, "token");
                    if (tok && tok->is<double>() && (int)tok->get<double>() == 0) is_usdc = true;
                }
                if (!is_usdc) continue;

                std::string val_s;
                const picojson::value* tv = pj_find(*bobj, "total");
                if (tv && pj_get_string_like(*tv, val_s) && !val_s.empty()) {
                    out_usdc = std::strtod(val_s.c_str(), nullptr);
                    return true;
                }
                const picojson::value* av = pj_find(*bobj, "available");
                if (av && pj_get_string_like(*av, val_s) && !val_s.empty()) {
                    out_usdc = std::strtod(val_s.c_str(), nullptr);
                    return true;
                }
                const picojson::value* bv2 = pj_find(*bobj, "balance");
                if (bv2 && pj_get_string_like(*bv2, val_s) && !val_s.empty()) {
                    out_usdc = std::strtod(val_s.c_str(), nullptr);
                    return true;
                }
            }
        }
    }

    if (parse_spot_usdc_balance_coin_total(spot_state_json, out_usdc)) return true;
    if (parse_spot_usdc_balance_token0(spot_state_json, out_usdc)) return true;
    return false;
}

bool parse_perp_usdc_balance(const std::string& perp_state_json, double& out_usdc) {
    out_usdc = 0.0;
    picojson::object obj;
    if (pj_parse_root_object(perp_state_json, obj)) {
        const picojson::value* v = pj_find(obj, "accountValue");
        if (v) {
            std::string s;
            if (pj_get_string_like(*v, s) && !s.empty()) {
                out_usdc = std::strtod(s.c_str(), nullptr);
                return true;
            }
        }
    }

    std::string v;
    if (parse_json_number_string_field(perp_state_json, "accountValue", v)) {
        out_usdc = std::strtod(v.c_str(), nullptr);
        return true;
    }
    return false;
}

bool parse_mid_price(const std::string& all_mids_json, const std::string& coin, double& out_price) {
    picojson::object obj;
    if (pj_parse_root_object(all_mids_json, obj)) {
        const picojson::value* mids_v = pj_find(obj, "mids");
        const picojson::object* mids = mids_v ? pj_get_obj(*mids_v) : nullptr;

        if (mids) {
            const picojson::value* pv = pj_find(*mids, coin.c_str());
            if (pv) {
                std::string s;
                if (pj_get_string_like(*pv, s) && !s.empty()) {
                    out_price = std::strtod(s.c_str(), nullptr);
                    return out_price > 0.0;
                }
            }
        }

        const picojson::value* pv2 = pj_find(obj, coin.c_str());
        if (pv2) {
            std::string s;
            if (pj_get_string_like(*pv2, s) && !s.empty()) {
                out_price = std::strtod(s.c_str(), nullptr);
                return out_price > 0.0;
            }
        }
    }

    // Fallback: substring scan.
    // allMids response includes "<COIN>":"<price>" somewhere.
    std::string needle = "\"" + coin + "\":";
    size_t p = all_mids_json.find(needle);
    if (p == std::string::npos) return false;
    p += needle.size();
    // skip whitespace
    while (p < all_mids_json.size() && (all_mids_json[p] == ' ' || all_mids_json[p] == '\n' || all_mids_json[p] == '\r' || all_mids_json[p] == '\t')) p++;
    if (p >= all_mids_json.size() || all_mids_json[p] != '"') return false;
    std::string v;
    if (!parse_quoted_value(all_mids_json, p, v)) return false;
    out_price = std::strtod(v.c_str(), nullptr);
    return out_price > 0.0;
}

bool parse_spot_asset_info(const std::string& spot_meta_json,
                           const std::string& display_sym,
                           int& out_asset,
                           int& out_sz_decimals) {
    out_asset = -1;
    out_sz_decimals = 0;
    picojson::value root;
    std::string err = picojson::parse(root, spot_meta_json);
    if (!err.empty()) return false;

    const picojson::object* obj = nullptr;
    if (root.is<picojson::object>()) {
        obj = pj_get_obj(root);
    } else if (root.is<picojson::array>()) {
        const picojson::array& top = root.get<picojson::array>();
        if (!top.empty()) {
            obj = pj_get_obj(top[0]);
        }
    }
    if (!obj) return false;

    const picojson::value* uni_v = pj_find(*obj, "universe");
    const picojson::value* tokens_v = pj_find(*obj, "tokens");
    const picojson::array* universe = uni_v ? pj_get_arr(*uni_v) : nullptr;
    const picojson::array* tokens = tokens_v ? pj_get_arr(*tokens_v) : nullptr;
    if (!universe || !tokens) return false;

    for (size_t i = 0; i < universe->size(); i++) {
        const picojson::object* pair = pj_get_obj((*universe)[i]);
        if (!pair) continue;

        const picojson::value* name_v = pj_find(*pair, "name");
        if (!name_v) continue;
        std::string name;
        if (!pj_get_string_like(*name_v, name) || name.empty()) continue;

        const picojson::value* toks_v = pj_find(*pair, "tokens");
        const picojson::array* toks = toks_v ? pj_get_arr(*toks_v) : nullptr;
        if (!toks || toks->size() < 1) continue;

        int base_idx = -1;
        if ((*toks)[0].is<double>()) base_idx = (int)(*toks)[0].get<double>();
        if (base_idx < 0 || (size_t)base_idx >= tokens->size()) continue;

        const picojson::object* base_tok = pj_get_obj((*tokens)[(size_t)base_idx]);
        if (!base_tok) continue;

        std::string token_name;
        std::string token_full;
        const picojson::value* tname = pj_find(*base_tok, "name");
        const picojson::value* tfull = pj_find(*base_tok, "fullName");
        if (tname) (void)pj_get_string_like(*tname, token_name);
        if (tfull) (void)pj_get_string_like(*tfull, token_full);

        if (!token_name.empty()) {
            token_name = map_token_display_sym(token_name, token_full);
        }
        if (token_name.empty()) {
            size_t slash = name.find('/');
            token_name = (slash == std::string::npos) ? name : name.substr(0, slash);
        }
        if (token_name != display_sym) continue;

        int index = -1;
        const picojson::value* iv = pj_find(*pair, "index");
        if (iv && iv->is<double>()) index = (int)iv->get<double>();
        if (index < 0) continue;

        int sz_decimals = 0;
        const picojson::value* sdv = pj_find(*base_tok, "szDecimals");
        if (sdv && sdv->is<double>()) sz_decimals = std::max(0, (int)sdv->get<double>());

        out_asset = index + 10000;
        out_sz_decimals = sz_decimals;
        return true;
    }

    return false;
}

} // namespace tradeboy::market
