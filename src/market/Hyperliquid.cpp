#include "Hyperliquid.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace tradeboy::market {

static bool write_file(const char* path, const std::string& s) {
    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f.good()) return false;
    f << s;
    return true;
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

static bool hl_post_file(const char* json_path, std::string& out_json) {
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

bool parse_mid_price(const std::string& all_mids_json, const std::string& coin, double& out_price) {
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

} // namespace tradeboy::market
