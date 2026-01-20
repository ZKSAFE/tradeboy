#include "arb/ArbitrumRpc.h"

#include "utils/Hex.h"
#include "utils/Process.h"
#include "utils/Format.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace tradeboy::arb {

static bool write_file(const char* path, const std::string& s) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    size_t w = std::fwrite(s.data(), 1, s.size(), f);
    std::fclose(f);
    return w == s.size();
}

static bool http_post_json_wget(const std::string& rpc_url, const char* json_path, std::string& out_json) {
    out_json.clear();
    std::string cmd = "/usr/bin/wget -qO- --header=\"Content-Type: application/json\" --post-file=";
    cmd += json_path;
    cmd += " ";
    cmd += rpc_url;
    return tradeboy::utils::run_cmd_capture(cmd, out_json) && !out_json.empty();
}

static bool parse_json_result_hex(const std::string& json, std::string& out_hex_0x) {
    // naive parse: find "\"result\"" then next "0x..." string.
    size_t p = json.find("\"result\"");
    if (p == std::string::npos) return false;
    p = json.find("0x", p);
    if (p == std::string::npos) return false;
    size_t q = p + 2;
    while (q < json.size()) {
        char c = json[q];
        bool ishex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!ishex) break;
        q++;
    }
    if (q <= p + 2) return false;
    out_hex_0x = json.substr(p, q - p);
    return true;
}

static long double hex_quantity_to_ld(const std::string& hex_0x) {
    size_t i = 0;
    if (hex_0x.size() >= 2 && hex_0x[0] == '0' && (hex_0x[1] == 'x' || hex_0x[1] == 'X')) i = 2;
    long double v = 0.0L;
    for (; i < hex_0x.size(); i++) {
        char c = hex_0x[i];
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
        else break;
        v = v * 16.0L + (long double)d;
    }
    return v;
}

static std::string left_pad_64(const std::string& hex_no_0x_lower) {
    std::string s = hex_no_0x_lower;
    if (s.size() > 64) s = s.substr(s.size() - 64);
    if (s.size() < 64) s = std::string(64 - s.size(), '0') + s;
    return s;
}

static std::string addr_to_40hex_lower_no0x(const std::string& addr_0x) {
    std::string s = addr_0x;
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    if (s.size() > 40) s = s.substr(s.size() - 40);
    if (s.size() < 40) s = std::string(40 - s.size(), '0') + s;
    return s;
}

static bool rpc_call(const std::string& rpc_url, const std::string& body, std::string& out_json) {
    const char* path = "/tmp/tb_rpc.json";
    if (!write_file(path, body + "\n")) return false;
    return http_post_json_wget(rpc_url, path, out_json);
}

static bool rpc_eth_getBalance(const std::string& rpc_url, const std::string& addr_0x, std::string& out_hex) {
    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getBalance\",\"params\":[\"" + addr_0x + "\",\"latest\"]}";
    std::string resp;
    if (!rpc_call(rpc_url, body, resp)) return false;
    return parse_json_result_hex(resp, out_hex);
}

static bool rpc_eth_gasPrice(const std::string& rpc_url, std::string& out_hex) {
    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_gasPrice\",\"params\":[]}";
    std::string resp;
    if (!rpc_call(rpc_url, body, resp)) return false;
    return parse_json_result_hex(resp, out_hex);
}

static bool rpc_eth_call_balanceOf(const std::string& rpc_url, const std::string& usdc_contract_0x, const std::string& addr_0x, std::string& out_hex) {
    // balanceOf(address) selector: 70a08231
    std::string addr40 = addr_to_40hex_lower_no0x(addr_0x);
    std::string data = "0x70a08231" + left_pad_64(addr40);

    std::string body;
    body += "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_call\",\"params\":[{";
    body += "\"to\":\"" + usdc_contract_0x + "\",";
    body += "\"data\":\"" + data + "\"";
    body += "},\"latest\"]}";

    std::string resp;
    if (!rpc_call(rpc_url, body, resp)) return false;
    return parse_json_result_hex(resp, out_hex);
}

bool fetch_wallet_data(const std::string& rpc_url,
                       const std::string& wallet_address_0x,
                       WalletOnchainData& out,
                       std::string& out_err) {
    out = WalletOnchainData();
    out_err.clear();

    if (rpc_url.empty() || wallet_address_0x.empty()) {
        out_err = "missing_rpc_or_address";
        return false;
    }

    std::string bal_hex;
    std::string gas_hex;

    if (!rpc_eth_getBalance(rpc_url, wallet_address_0x, bal_hex)) {
        out_err = "eth_getBalance_failed";
        return false;
    }
    if (!rpc_eth_gasPrice(rpc_url, gas_hex)) {
        out_err = "eth_gasPrice_failed";
        return false;
    }

    // USDC on Arbitrum One
    const std::string usdc_contract = "0xaf88d065e77c8cC2239327C5EDb3A432268e5831";
    std::string usdc_hex;
    if (!rpc_eth_call_balanceOf(rpc_url, usdc_contract, wallet_address_0x, usdc_hex)) {
        out_err = "usdc_balanceOf_failed";
        return false;
    }

    long double wei = hex_quantity_to_ld(bal_hex);
    long double eth = wei / 1000000000000000000.0L;

    long double gaswei = hex_quantity_to_ld(gas_hex);
    long double gwei = gaswei / 1000000000.0L;

    long double usdc_raw = hex_quantity_to_ld(usdc_hex);
    long double usdc = usdc_raw / 1000000.0L;

    // Format
    out.eth_balance = tradeboy::utils::format_fixed_trunc_sig((double)eth, 7, 6);
    out.usdc_balance = tradeboy::utils::format_fixed_trunc_sig((double)usdc, 7, 6);

    std::string gwei_s = tradeboy::utils::format_fixed_trunc_sig((double)gwei, 7, 3);
    out.gas = std::string("GAS: ") + gwei_s + " GWEI";

    out.gas_price_wei = gaswei;

    out.rpc_ok = true;
    return true;
}

} // namespace tradeboy::arb
