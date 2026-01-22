#include "arb/ArbitrumRpc.h"

#include "utils/Hex.h"
#include "utils/Process.h"
#include "utils/Format.h"
#include "utils/Keccak.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>

extern void log_str(const char* s);

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

static std::string parse_json_error_summary(const std::string& json) {
    // Extremely lightweight JSON-RPC error parser.
    // Expected shape: {"jsonrpc":"2.0","id":1,"error":{"code":-32000,"message":"...","data":...}}
    size_t pe = json.find("\"error\"");
    if (pe == std::string::npos) return std::string();

    int code = 0;
    bool has_code = false;
    {
        size_t pc = json.find("\"code\"", pe);
        if (pc != std::string::npos) {
            size_t colon = json.find(':', pc);
            if (colon != std::string::npos) {
                size_t s = colon + 1;
                while (s < json.size() && (json[s] == ' ' || json[s] == '\n' || json[s] == '\t')) s++;
                bool neg = false;
                if (s < json.size() && json[s] == '-') {
                    neg = true;
                    s++;
                }
                long v = 0;
                bool any = false;
                while (s < json.size() && json[s] >= '0' && json[s] <= '9') {
                    any = true;
                    v = v * 10 + (json[s] - '0');
                    s++;
                }
                if (any) {
                    if (neg) v = -v;
                    code = (int)v;
                    has_code = true;
                }
            }
        }
    }

    std::string message;
    {
        size_t pm = json.find("\"message\"", pe);
        if (pm != std::string::npos) {
            size_t q1 = json.find('"', pm + 9);
            if (q1 != std::string::npos) {
                size_t q2 = json.find('"', q1 + 1);
                if (q2 != std::string::npos && q2 > q1 + 1) {
                    message = json.substr(q1 + 1, q2 - (q1 + 1));
                }
            }
        }
    }

    std::string data_prefix;
    {
        size_t pd = json.find("\"data\"", pe);
        if (pd != std::string::npos) {
            size_t colon = json.find(':', pd);
            if (colon != std::string::npos) {
                size_t s = colon + 1;
                while (s < json.size() && (json[s] == ' ' || json[s] == '\n' || json[s] == '\t')) s++;
                size_t e = json.find_first_of(",}\n", s);
                if (e == std::string::npos) e = json.size();
                if (e > s) {
                    data_prefix = json.substr(s, e - s);
                    if (data_prefix.size() > 120) data_prefix = data_prefix.substr(0, 120);
                }
            }
        }
    }

    std::ostringstream oss;
    oss << "rpc_error";
    if (has_code) oss << " code=" << code;
    if (!message.empty()) oss << " message=\"" << message << "\"";
    if (!data_prefix.empty()) oss << " data_prefix=" << data_prefix;
    return oss.str();
}

static std::string rpc_resp_summary(const std::string& resp) {
    std::string s = parse_json_error_summary(resp);
    if (!s.empty()) return s;
    if (resp.empty()) return std::string("no_response");
    return std::string("unexpected_response");
}

static unsigned long long hex_quantity_to_ull(const std::string& hex_0x) {
    size_t i = 0;
    if (hex_0x.size() >= 2 && hex_0x[0] == '0' && (hex_0x[1] == 'x' || hex_0x[1] == 'X')) i = 2;
    unsigned long long v = 0ULL;
    for (; i < hex_0x.size(); i++) {
        char c = hex_0x[i];
        unsigned int d = 0;
        if (c >= '0' && c <= '9') d = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') d = 10u + (unsigned int)(c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10u + (unsigned int)(c - 'A');
        else break;
        v = (v << 4) | (unsigned long long)d;
    }
    return v;
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

static bool rpc_eth_getBalance_raw(const std::string& rpc_url, const std::string& addr_0x, std::string& out_hex, std::string& out_resp) {
    out_hex.clear();
    out_resp.clear();
    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getBalance\",\"params\":[\"" + addr_0x + "\",\"latest\"]}";
    if (!rpc_call(rpc_url, body, out_resp)) return false;
    return parse_json_result_hex(out_resp, out_hex);
}

static bool rpc_eth_gasPrice(const std::string& rpc_url, std::string& out_hex) {
    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_gasPrice\",\"params\":[]}";
    std::string resp;
    if (!rpc_call(rpc_url, body, resp)) return false;
    return parse_json_result_hex(resp, out_hex);
}

static bool rpc_eth_gasPrice_raw(const std::string& rpc_url, std::string& out_hex, std::string& out_resp) {
    out_hex.clear();
    out_resp.clear();
    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_gasPrice\",\"params\":[]}";
    if (!rpc_call(rpc_url, body, out_resp)) return false;
    return parse_json_result_hex(out_resp, out_hex);
}

static bool rpc_eth_getTransactionCount(const std::string& rpc_url, const std::string& addr_0x, std::string& out_hex) {
    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getTransactionCount\",\"params\":[\"" + addr_0x + "\",\"pending\"]}";
    std::string resp;
    if (!rpc_call(rpc_url, body, resp)) return false;
    return parse_json_result_hex(resp, out_hex);
}

static bool rpc_eth_getTransactionCount_raw(const std::string& rpc_url, const std::string& addr_0x, std::string& out_hex, std::string& out_resp) {
    out_hex.clear();
    out_resp.clear();
    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getTransactionCount\",\"params\":[\"" + addr_0x + "\",\"pending\"]}";
    if (!rpc_call(rpc_url, body, out_resp)) return false;
    return parse_json_result_hex(out_resp, out_hex);
}

static bool rpc_eth_sendRawTransaction(const std::string& rpc_url, const std::string& rawtx_0x, std::string& out_txhash_0x, std::string& out_resp) {
    out_txhash_0x.clear();
    out_resp.clear();
    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_sendRawTransaction\",\"params\":[\"" + rawtx_0x + "\"]}";
    if (!rpc_call(rpc_url, body, out_resp)) return false;
    return parse_json_result_hex(out_resp, out_txhash_0x);
}

static bool parse_json_base_fee_per_gas_hex(const std::string& json, std::string& out_hex_0x) {
    // naive parse: find "baseFeePerGas" then next "0x..." string.
    size_t p = json.find("\"baseFeePerGas\"");
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

static bool rpc_eth_getBaseFeePerGas_raw(const std::string& rpc_url, std::string& out_hex_0x, std::string& out_resp) {
    out_hex_0x.clear();
    out_resp.clear();
    std::string body = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getBlockByNumber\",\"params\":[\"latest\",false]}";
    if (!rpc_call(rpc_url, body, out_resp)) return false;
    return parse_json_base_fee_per_gas_hex(out_resp, out_hex_0x);
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

static void bn_freep(BIGNUM*& p) {
    if (p) BN_free(p);
    p = nullptr;
}

static void ec_point_freep(EC_POINT*& p) {
    if (p) EC_POINT_free(p);
    p = nullptr;
}

static void ec_key_freep(EC_KEY*& p) {
    if (p) EC_KEY_free(p);
    p = nullptr;
}

static std::vector<unsigned char> bn_to_bytes_32(const BIGNUM* bn) {
    std::vector<unsigned char> out(32, 0);
    int n = BN_num_bytes(bn);
    if (n <= 0) return out;
    std::vector<unsigned char> tmp((size_t)n);
    BN_bn2bin(bn, tmp.data());
    if (tmp.size() >= 32) {
        std::memcpy(out.data(), tmp.data() + (tmp.size() - 32), 32);
    } else {
        std::memcpy(out.data() + (32 - tmp.size()), tmp.data(), tmp.size());
    }
    return out;
}

static std::vector<unsigned char> hex_quantity_to_bytes_be(const std::string& hex_0x) {
    std::vector<unsigned char> out;

    // Accept odd-length quantities (e.g. 0xabc) by left-padding a nibble.
    std::string s = hex_0x;
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
    size_t i0 = 0;
    while (i0 < s.size() && (s[i0] == ' ' || s[i0] == '\t' || s[i0] == '\n' || s[i0] == '\r')) i0++;
    s = s.substr(i0);
    if (s.empty()) return out;
    if ((s.size() % 2) == 1) s = std::string("0") + s;
    s = std::string("0x") + s;

    std::vector<unsigned char> b;
    if (!tradeboy::utils::hex_to_bytes(s, b)) return out;

    size_t i = 0;
    while (i < b.size() && b[i] == 0) i++;
    if (i == b.size()) return out;
    out.assign(b.begin() + (long)i, b.end());
    return out;
}

static void rlp_encode_bytes(const unsigned char* data, size_t n, std::vector<unsigned char>& out) {
    if (n == 0) {
        out.push_back(0x80);
        return;
    }
    if (n == 1 && data[0] < 0x80) {
        out.push_back(data[0]);
        return;
    }
    if (n <= 55) {
        out.push_back((unsigned char)(0x80 + n));
        out.insert(out.end(), data, data + n);
        return;
    }
    std::vector<unsigned char> len;
    size_t v = n;
    while (v > 0) {
        len.push_back((unsigned char)(v & 0xFF));
        v >>= 8;
    }
    std::reverse(len.begin(), len.end());
    out.push_back((unsigned char)(0xB7 + len.size()));
    out.insert(out.end(), len.begin(), len.end());
    out.insert(out.end(), data, data + n);
}

static void rlp_encode_bytes_vec(const std::vector<unsigned char>& v, std::vector<unsigned char>& out) {
    rlp_encode_bytes(v.empty() ? nullptr : v.data(), v.size(), out);
}

static void rlp_encode_string(const std::string& s, std::vector<unsigned char>& out) {
    rlp_encode_bytes((const unsigned char*)s.data(), s.size(), out);
}

static void rlp_encode_list(const std::vector<unsigned char>& payload, std::vector<unsigned char>& out) {
    size_t n = payload.size();
    if (n <= 55) {
        out.push_back((unsigned char)(0xC0 + n));
        out.insert(out.end(), payload.begin(), payload.end());
        return;
    }
    std::vector<unsigned char> len;
    size_t v = n;
    while (v > 0) {
        len.push_back((unsigned char)(v & 0xFF));
        v >>= 8;
    }
    std::reverse(len.begin(), len.end());
    out.push_back((unsigned char)(0xF7 + len.size()));
    out.insert(out.end(), len.begin(), len.end());
    out.insert(out.end(), payload.begin(), payload.end());
}

static std::vector<unsigned char> rlp_u256_be(const std::vector<unsigned char>& be_min) {
    if (be_min.empty()) return std::vector<unsigned char>();
    std::vector<unsigned char> v = be_min;
    size_t i = 0;
    while (i < v.size() && v[i] == 0) i++;
    if (i == v.size()) return std::vector<unsigned char>();
    if (i > 0) v.erase(v.begin(), v.begin() + (long)i);
    return v;
}

static std::vector<unsigned char> addr_0x_to_20(const std::string& addr_0x) {
    std::vector<unsigned char> out;
    std::vector<unsigned char> b;
    if (!tradeboy::utils::hex_to_bytes(addr_0x, b)) return out;
    if (b.size() == 20) return b;
    if (b.size() > 20) {
        out.assign(b.end() - 20, b.end());
        return out;
    }
    out.assign(20 - b.size(), 0);
    out.insert(out.end(), b.begin(), b.end());
    return out;
}

static std::vector<unsigned char> build_erc20_transfer_data(const std::string& to_addr_0x, unsigned long long amount) {
    // transfer(address,uint256) selector: a9059cbb
    std::vector<unsigned char> out;
    out.reserve(4 + 32 + 32);
    out.push_back(0xa9);
    out.push_back(0x05);
    out.push_back(0x9c);
    out.push_back(0xbb);

    std::vector<unsigned char> to20 = addr_0x_to_20(to_addr_0x);
    out.insert(out.end(), 12, 0);
    out.insert(out.end(), to20.begin(), to20.end());

    unsigned char amt[32];
    std::memset(amt, 0, sizeof(amt));
    unsigned long long v = amount;
    for (int i = 0; i < 8; i++) {
        amt[31 - i] = (unsigned char)(v & 0xFF);
        v >>= 8;
    }
    out.insert(out.end(), amt, amt + 32);
    return out;
}

static bool secp256k1_key_from_priv(const std::vector<unsigned char>& priv32, EC_KEY*& out_key, std::string& out_err) {
    out_err.clear();
    out_key = nullptr;
    if (priv32.size() != 32) {
        out_err = "priv32_invalid";
        return false;
    }
    EC_KEY* key = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!key) {
        out_err = "EC_KEY_new_failed";
        return false;
    }
    BIGNUM* d = BN_bin2bn(priv32.data(), (int)priv32.size(), nullptr);
    if (!d) {
        EC_KEY_free(key);
        out_err = "BN_bin2bn_failed";
        return false;
    }
    if (EC_KEY_set_private_key(key, d) != 1) {
        BN_free(d);
        EC_KEY_free(key);
        out_err = "set_priv_failed";
        return false;
    }

    const EC_GROUP* group = EC_KEY_get0_group(key);
    EC_POINT* pub = EC_POINT_new(group);
    if (!pub) {
        BN_free(d);
        EC_KEY_free(key);
        out_err = "EC_POINT_new_failed";
        return false;
    }
    BN_CTX* ctx = BN_CTX_new();
    if (!ctx) {
        EC_POINT_free(pub);
        BN_free(d);
        EC_KEY_free(key);
        out_err = "BN_CTX_new_failed";
        return false;
    }
    if (EC_POINT_mul(group, pub, d, nullptr, nullptr, ctx) != 1) {
        BN_CTX_free(ctx);
        EC_POINT_free(pub);
        BN_free(d);
        EC_KEY_free(key);
        out_err = "EC_POINT_mul_failed";
        return false;
    }
    if (EC_KEY_set_public_key(key, pub) != 1) {
        BN_CTX_free(ctx);
        EC_POINT_free(pub);
        BN_free(d);
        EC_KEY_free(key);
        out_err = "set_pub_failed";
        return false;
    }
    BN_CTX_free(ctx);
    EC_POINT_free(pub);
    BN_free(d);
    out_key = key;
    return true;
}

static bool secp256k1_sign_rs(const unsigned char hash32[32], const std::vector<unsigned char>& priv32, BIGNUM*& out_r, BIGNUM*& out_s, std::string& out_err) {
    out_err.clear();
    out_r = nullptr;
    out_s = nullptr;

    EC_KEY* key = nullptr;
    if (!secp256k1_key_from_priv(priv32, key, out_err)) return false;

    ECDSA_SIG* sig = ECDSA_do_sign(hash32, 32, key);
    if (!sig) {
        ec_key_freep(key);
        out_err = "ECDSA_do_sign_failed";
        return false;
    }

    const BIGNUM* r0 = nullptr;
    const BIGNUM* s0 = nullptr;
    ECDSA_SIG_get0(sig, &r0, &s0);
    out_r = BN_dup(r0);
    out_s = BN_dup(s0);
    ECDSA_SIG_free(sig);
    ec_key_freep(key);

    if (!out_r || !out_s) {
        bn_freep(out_r);
        bn_freep(out_s);
        out_err = "BN_dup_failed";
        return false;
    }
    return true;
}

static bool secp256k1_compute_recid(const unsigned char hash32[32], const std::vector<unsigned char>& priv32, const BIGNUM* r, const BIGNUM* s, int& out_recid, std::string& out_err) {
    out_err.clear();
    out_recid = -1;

    EC_KEY* key = nullptr;
    if (!secp256k1_key_from_priv(priv32, key, out_err)) return false;
    const EC_GROUP* group = EC_KEY_get0_group(key);
    const EC_POINT* pub = EC_KEY_get0_public_key(key);

    BN_CTX* ctx = BN_CTX_new();
    if (!ctx) {
        ec_key_freep(key);
        out_err = "BN_CTX_new_failed";
        return false;
    }
    BN_CTX_start(ctx);

    BIGNUM* n = BN_CTX_get(ctx);
    BIGNUM* p = BN_CTX_get(ctx);
    BIGNUM* a = BN_CTX_get(ctx);
    BIGNUM* b = BN_CTX_get(ctx);
    if (!b) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        ec_key_freep(key);
        out_err = "BN_CTX_get_failed";
        return false;
    }
    if (EC_GROUP_get_order(group, n, ctx) != 1) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        ec_key_freep(key);
        out_err = "get_order_failed";
        return false;
    }
    if (EC_GROUP_get_curve_GFp(group, p, a, b, ctx) != 1) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        ec_key_freep(key);
        out_err = "get_curve_failed";
        return false;
    }

    BIGNUM* e = BN_CTX_get(ctx);
    if (!e) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        ec_key_freep(key);
        out_err = "BN_CTX_get_failed";
        return false;
    }
    BN_bin2bn(hash32, 32, e);
    BN_mod(e, e, n, ctx);

    BIGNUM* rinv = BN_mod_inverse(nullptr, r, n, ctx);
    if (!rinv) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        ec_key_freep(key);
        out_err = "r_inv_failed";
        return false;
    }

    for (int recid = 0; recid < 4; recid++) {
        BIGNUM* x = BN_CTX_get(ctx);
        BIGNUM* j = BN_CTX_get(ctx);
        if (!j) {
            BN_free(rinv);
            BN_CTX_end(ctx);
            BN_CTX_free(ctx);
            ec_key_freep(key);
            out_err = "BN_CTX_get_failed";
            return false;
        }
        BN_copy(x, r);
        if (recid >= 2) {
            BN_set_word(j, 1);
            BN_mul(j, j, n, ctx);
            BN_add(x, x, j);
        }

        if (BN_cmp(x, p) >= 0) {
            continue;
        }

        // y^2 = x^3 + 7 mod p  (secp256k1 has a=0, b=7)
        BIGNUM* y2 = BN_CTX_get(ctx);
        BIGNUM* y = BN_CTX_get(ctx);
        if (!y) {
            BN_free(rinv);
            BN_CTX_end(ctx);
            BN_CTX_free(ctx);
            ec_key_freep(key);
            out_err = "BN_CTX_get_failed";
            return false;
        }
        BN_mod_sqr(y2, x, p, ctx);        // x^2
        BN_mod_mul(y2, y2, x, p, ctx);    // x^3
        BN_mod_add(y2, y2, b, p, ctx);    // x^3 + b
        BIGNUM* y_sqrt = BN_mod_sqrt(nullptr, y2, p, ctx);
        if (!y_sqrt) {
            continue;
        }
        BN_copy(y, y_sqrt);
        BN_free(y_sqrt);

        int y_is_odd = BN_is_odd(y) ? 1 : 0;
        int want_odd = (recid & 1);
        if (y_is_odd != want_odd) {
            BN_sub(y, p, y);
        }

        EC_POINT* R = EC_POINT_new(group);
        if (!R) {
            continue;
        }
        if (EC_POINT_set_affine_coordinates_GFp(group, R, x, y, ctx) != 1) {
            EC_POINT_free(R);
            continue;
        }

        // Check n*R == infinity
        EC_POINT* nR = EC_POINT_new(group);
        if (!nR) {
            EC_POINT_free(R);
            continue;
        }
        if (EC_POINT_mul(group, nR, nullptr, R, n, ctx) != 1) {
            EC_POINT_free(nR);
            EC_POINT_free(R);
            continue;
        }
        bool is_inf = (EC_POINT_is_at_infinity(group, nR) == 1);
        EC_POINT_free(nR);
        if (!is_inf) {
            EC_POINT_free(R);
            continue;
        }

        // Q = r^{-1}(sR - eG)
        EC_POINT* sR = EC_POINT_new(group);
        EC_POINT* eG = EC_POINT_new(group);
        EC_POINT* P = EC_POINT_new(group);
        EC_POINT* Q = EC_POINT_new(group);
        if (!sR || !eG || !P || !Q) {
            ec_point_freep(sR);
            ec_point_freep(eG);
            ec_point_freep(P);
            ec_point_freep(Q);
            EC_POINT_free(R);
            continue;
        }

        if (EC_POINT_mul(group, sR, nullptr, R, s, ctx) != 1) {
            ec_point_freep(sR);
            ec_point_freep(eG);
            ec_point_freep(P);
            ec_point_freep(Q);
            EC_POINT_free(R);
            continue;
        }
        if (EC_POINT_mul(group, eG, e, nullptr, nullptr, ctx) != 1) {
            ec_point_freep(sR);
            ec_point_freep(eG);
            ec_point_freep(P);
            ec_point_freep(Q);
            EC_POINT_free(R);
            continue;
        }
        if (EC_POINT_invert(group, eG, ctx) != 1) {
            ec_point_freep(sR);
            ec_point_freep(eG);
            ec_point_freep(P);
            ec_point_freep(Q);
            EC_POINT_free(R);
            continue;
        }
        if (EC_POINT_add(group, P, sR, eG, ctx) != 1) {
            ec_point_freep(sR);
            ec_point_freep(eG);
            ec_point_freep(P);
            ec_point_freep(Q);
            EC_POINT_free(R);
            continue;
        }
        if (EC_POINT_mul(group, Q, nullptr, P, rinv, ctx) != 1) {
            ec_point_freep(sR);
            ec_point_freep(eG);
            ec_point_freep(P);
            ec_point_freep(Q);
            EC_POINT_free(R);
            continue;
        }

        int eq = EC_POINT_cmp(group, Q, pub, ctx);
        ec_point_freep(sR);
        ec_point_freep(eG);
        ec_point_freep(P);
        ec_point_freep(Q);
        EC_POINT_free(R);

        if (eq == 0) {
            out_recid = recid;
            BN_free(rinv);
            BN_CTX_end(ctx);
            BN_CTX_free(ctx);
            ec_key_freep(key);
            return true;
        }
    }

    BN_free(rinv);
    BN_CTX_end(ctx);
    BN_CTX_free(ctx);
    ec_key_freep(key);
    out_err = "recid_not_found";
    return false;
}

static std::vector<unsigned char> rlp_item_u64(unsigned long long v) {
    std::vector<unsigned char> be;
    if (v == 0) return be;
    while (v > 0) {
        be.push_back((unsigned char)(v & 0xFF));
        v >>= 8;
    }
    std::reverse(be.begin(), be.end());
    return be;
}

static void rlp_append_item_bytes(const std::vector<unsigned char>& bytes, std::vector<unsigned char>& payload) {
    std::vector<unsigned char> enc;
    rlp_encode_bytes(bytes.empty() ? nullptr : bytes.data(), bytes.size(), enc);
    payload.insert(payload.end(), enc.begin(), enc.end());
}

static void rlp_append_item_u64(unsigned long long v, std::vector<unsigned char>& payload) {
    std::vector<unsigned char> be = rlp_item_u64(v);
    rlp_append_item_bytes(be, payload);
}

static void rlp_append_item_bn(const BIGNUM* bn, std::vector<unsigned char>& payload) {
    if (!bn || BN_is_zero(bn)) {
        rlp_append_item_bytes(std::vector<unsigned char>(), payload);
        return;
    }
    int n = BN_num_bytes(bn);
    std::vector<unsigned char> tmp((size_t)n);
    BN_bn2bin(bn, tmp.data());
    size_t i = 0;
    while (i < tmp.size() && tmp[i] == 0) i++;
    std::vector<unsigned char> be;
    if (i < tmp.size()) be.assign(tmp.begin() + (long)i, tmp.end());
    rlp_append_item_bytes(be, payload);
}

bool send_usdc_transfer_test(const std::string& rpc_url,
                             const std::string& from_addr_0x,
                             const std::string& privkey_0x,
                             const std::string& to_addr_0x,
                             unsigned long long amount_micro,
                             std::string& out_txhash,
                             std::string& out_err) {
    out_txhash.clear();
    out_err.clear();

    if (rpc_url.empty() || from_addr_0x.empty() || privkey_0x.empty() || to_addr_0x.empty()) {
        out_err = "missing_params";
        return false;
    }

    // Parse private key
    std::vector<unsigned char> priv;
    if (!tradeboy::utils::hex_to_bytes(privkey_0x, priv) || priv.size() != 32) {
        out_err = "privkey_parse_failed";
        return false;
    }

    // Constants
    const unsigned long long chain_id = 42161ULL;
    const std::string usdc_contract = "0xaf88d065e77c8cC2239327C5EDb3A432268e5831";

    // Fetch nonce and gasPrice
    std::string nonce_hex;
    std::string gas_hex;
    std::string nonce_resp;
    {
        bool ok = false;
        for (int attempt = 1; attempt <= 3; attempt++) {
            nonce_resp.clear();
            ok = rpc_eth_getTransactionCount_raw(rpc_url, from_addr_0x, nonce_hex, nonce_resp);
            if (ok) break;
            std::string summary = rpc_resp_summary(nonce_resp);
            std::string p = nonce_resp.substr(0, 256);
            std::ostringstream oss;
            oss << "[ARB] eth_getTransactionCount attempt=" << attempt << " failed " << summary << " resp_len=" << (unsigned long long)nonce_resp.size() << " resp_prefix=<<<" << p << ">>>\n";
            std::string s = oss.str();
            log_str(s.c_str());
            if (summary != "no_response") break;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        if (!ok) {
            std::string summary = rpc_resp_summary(nonce_resp);
            std::string p = nonce_resp.substr(0, 512);
            out_err = std::string("eth_getTransactionCount_failed ") + summary + " resp_prefix=<<<" + p + ">>>";
            return false;
        }
    }

    std::string gas_resp;
    {
        bool ok = false;
        for (int attempt = 1; attempt <= 3; attempt++) {
            gas_resp.clear();
            ok = rpc_eth_gasPrice_raw(rpc_url, gas_hex, gas_resp);
            if (ok) break;
            std::string summary = rpc_resp_summary(gas_resp);
            std::string p = gas_resp.substr(0, 256);
            std::ostringstream oss;
            oss << "[ARB] eth_gasPrice attempt=" << attempt << " failed " << summary << " resp_len=" << (unsigned long long)gas_resp.size() << " resp_prefix=<<<" << p << ">>>\n";
            std::string s = oss.str();
            log_str(s.c_str());
            if (summary != "no_response") break;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        if (!ok) {
            std::string summary = rpc_resp_summary(gas_resp);
            std::string p = gas_resp.substr(0, 512);
            out_err = std::string("eth_gasPrice_failed ") + summary + " resp_prefix=<<<" + p + ">>>";
            return false;
        }
    }

    // Arbitrum can reject tx if effective fee < base fee. Ensure gasPrice >= baseFeePerGas.
    std::string basefee_hex;
    std::string basefee_resp;
    if (rpc_eth_getBaseFeePerGas_raw(rpc_url, basefee_hex, basefee_resp)) {
        unsigned long long gp = hex_quantity_to_ull(gas_hex);
        unsigned long long bf = hex_quantity_to_ull(basefee_hex);
        if (gp < bf) {
            unsigned long long bumped = bf + (bf / 2ULL) + 1ULL; // 1.5x base fee + 1 wei
            std::ostringstream oss;
            oss << "[ARB] bump gasPrice: gasPrice=" << gp << " baseFeePerGas=" << bf << " -> " << bumped << "\n";
            std::string s = oss.str();
            log_str(s.c_str());
            std::ostringstream hx;
            hx << std::hex << bumped;
            std::string hh = hx.str();
            if ((hh.size() % 2) == 1) hh = std::string("0") + hh;
            gas_hex = std::string("0x") + hh;
        }
    } else {
        std::string p = basefee_resp.substr(0, 512);
        std::string summary = rpc_resp_summary(basefee_resp);
        std::string msg = std::string("[ARB] eth_getBlockByNumber(baseFeePerGas) failed ") + summary + " resp_prefix=<<<" + p + ">>>\n";
        log_str(msg.c_str());
    }

    std::vector<unsigned char> nonce_be = hex_quantity_to_bytes_be(nonce_hex);
    std::vector<unsigned char> gasprice_be = hex_quantity_to_bytes_be(gas_hex);
    unsigned long long gas_limit = 90000ULL;

    // Pre-check ETH balance for gas.
    {
        std::string bal_hex;
        std::string bal_resp;
        if (!rpc_eth_getBalance_raw(rpc_url, from_addr_0x, bal_hex, bal_resp)) {
            std::string p = bal_resp.substr(0, 512);
            std::string summary = rpc_resp_summary(bal_resp);
            std::string msg = std::string("[ARB] eth_getBalance failed ") + summary + " resp_prefix=<<<" + p + ">>>\n";
            log_str(msg.c_str());
        } else {
            unsigned long long have = hex_quantity_to_ull(bal_hex);
            unsigned long long gp = hex_quantity_to_ull(gas_hex);
            unsigned long long want = gp * gas_limit;
            if (have < want) {
                std::ostringstream oss;
                oss << "[ARB] insufficient_eth_for_gas have=" << have << " want=" << want << " (wei)\n";
                std::string s = oss.str();
                log_str(s.c_str());
                out_err = std::string("insufficient_eth_for_gas have=") + std::to_string(have) + " want=" + std::to_string(want);
                return false;
            }
        }
    }

    std::vector<unsigned char> to20 = addr_0x_to_20(usdc_contract);
    std::vector<unsigned char> data = build_erc20_transfer_data(to_addr_0x, amount_micro);

    // RLP unsigned tx for EIP-155 signing: [nonce, gasPrice, gasLimit, to, value, data, chainId, 0, 0]
    std::vector<unsigned char> payload;
    rlp_append_item_bytes(rlp_u256_be(nonce_be), payload);
    rlp_append_item_bytes(rlp_u256_be(gasprice_be), payload);
    rlp_append_item_u64(gas_limit, payload);
    rlp_append_item_bytes(to20, payload);
    rlp_append_item_u64(0ULL, payload);
    rlp_append_item_bytes(data, payload);
    rlp_append_item_u64(chain_id, payload);
    rlp_append_item_u64(0ULL, payload);
    rlp_append_item_u64(0ULL, payload);
    std::vector<unsigned char> rlp_unsigned;
    rlp_encode_list(payload, rlp_unsigned);

    // keccak256
    unsigned char h[32];
    tradeboy::utils::keccak_256(rlp_unsigned.data(), rlp_unsigned.size(), h);

    BIGNUM* r = nullptr;
    BIGNUM* s = nullptr;
    std::string sign_err;
    if (!secp256k1_sign_rs(h, priv, r, s, sign_err)) {
        out_err = std::string("sign_failed:") + sign_err;
        bn_freep(r);
        bn_freep(s);
        return false;
    }

    // Enforce low-s (EIP-2)
    BN_CTX* ctx = BN_CTX_new();
    if (!ctx) {
        out_err = "BN_CTX_new_failed";
        bn_freep(r);
        bn_freep(s);
        return false;
    }
    BN_CTX_start(ctx);
    BIGNUM* n = BN_CTX_get(ctx);
    BIGNUM* halfn = BN_CTX_get(ctx);
    if (!halfn) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        out_err = "BN_CTX_get_failed";
        bn_freep(r);
        bn_freep(s);
        return false;
    }
    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (!group) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        out_err = "EC_GROUP_new_failed";
        bn_freep(r);
        bn_freep(s);
        return false;
    }
    EC_GROUP_get_order(group, n, ctx);
    BN_rshift1(halfn, n);
    int s_was_high = 0;
    if (BN_cmp(s, halfn) > 0) {
        BIGNUM* s2 = BN_dup(s);
        if (!s2) {
            EC_GROUP_free(group);
            BN_CTX_end(ctx);
            BN_CTX_free(ctx);
            out_err = "BN_dup_failed";
            bn_freep(r);
            bn_freep(s);
            return false;
        }
        BN_sub(s2, n, s2);
        BN_free(s);
        s = s2;
        s_was_high = 1;
    }
    EC_GROUP_free(group);
    BN_CTX_end(ctx);
    BN_CTX_free(ctx);

    int recid = -1;
    std::string rec_err;
    if (!secp256k1_compute_recid(h, priv, r, s, recid, rec_err)) {
        out_err = std::string("recid_failed:") + rec_err;
        bn_freep(r);
        bn_freep(s);
        return false;
    }
    if (s_was_high) {
        // If we flipped s, flip recid parity
        recid ^= 1;
    }

    unsigned long long v = chain_id * 2ULL + 35ULL + (unsigned long long)recid;

    // RLP signed tx: [nonce, gasPrice, gasLimit, to, value, data, v, r, s]
    std::vector<unsigned char> payload2;
    rlp_append_item_bytes(rlp_u256_be(nonce_be), payload2);
    rlp_append_item_bytes(rlp_u256_be(gasprice_be), payload2);
    rlp_append_item_u64(gas_limit, payload2);
    rlp_append_item_bytes(to20, payload2);
    rlp_append_item_u64(0ULL, payload2);
    rlp_append_item_bytes(data, payload2);
    rlp_append_item_u64(v, payload2);
    rlp_append_item_bn(r, payload2);
    rlp_append_item_bn(s, payload2);
    std::vector<unsigned char> raw;
    rlp_encode_list(payload2, raw);
    bn_freep(r);
    bn_freep(s);

    std::string raw_0x = tradeboy::utils::bytes_to_hex_lower(raw.data(), raw.size(), true);

    std::string resp;
    std::string txh;
    if (!rpc_eth_sendRawTransaction(rpc_url, raw_0x, txh, resp)) {
        std::string p = resp.substr(0, 512);
        std::string summary = rpc_resp_summary(resp);
        std::string msg = std::string("[ARB] eth_sendRawTransaction failed ") + summary + " resp_prefix=<<<" + p + ">>>\n";
        log_str(msg.c_str());
        out_err = std::string("send_failed ") + summary + " resp_prefix=<<<" + p + ">>>";
        return false;
    }

    out_txhash = txh;
    return true;
}

} // namespace tradeboy::arb
