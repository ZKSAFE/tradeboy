#include "wallet/Wallet.h"

#include "utils/File.h"
#include "utils/Hex.h"
#include "utils/Keccak.h"
#include "utils/Process.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <vector>

extern void log_to_file(const char* fmt, ...);

namespace tradeboy::wallet {

static std::string trim_line(std::string s) {
    // reuse trim semantics from utils::trim but keep dependencies minimal
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) a++;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

static bool parse_kv(const std::string& text, const std::string& key, std::string& out_val) {
    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = trim_line(line);
        if (line.empty()) continue;
        if (line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim_line(line.substr(0, eq));
        if (k != key) continue;
        out_val = trim_line(line.substr(eq + 1));
        return true;
    }
    return false;
}

static std::string default_cfg_text(const std::string& rpc, const std::string& addr, const std::string& priv) {
    std::string s;
    s += "arb_rpc_url=" + rpc + "\n";
    s += "wallet_address=" + addr + "\n";
    s += "private_key=" + priv + "\n";
    s += "usdc_contract=0xaf88d065e77c8cC2239327C5EDb3A432268e5831\n";
    return s;
}

static bool write_ec_privkey_der(const std::vector<unsigned char>& priv32, const char* path) {
    if (priv32.size() != 32) return false;
    unsigned char der[48];
    size_t i = 0;
    der[i++] = 0x30; der[i++] = 0x2e;
    der[i++] = 0x02; der[i++] = 0x01; der[i++] = 0x01;
    der[i++] = 0x04; der[i++] = 0x20;
    for (size_t k = 0; k < 32; k++) der[i++] = priv32[k];
    der[i++] = 0xa0; der[i++] = 0x07;
    der[i++] = 0x06; der[i++] = 0x05;
    der[i++] = 0x2b; der[i++] = 0x81; der[i++] = 0x04; der[i++] = 0x00; der[i++] = 0x0a;

    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    size_t w = std::fwrite(der, 1, sizeof(der), f);
    std::fclose(f);
    return w == sizeof(der);
}

static bool parse_openssl_pubkey_uncompressed_hex(const std::string& out, std::string& out_hex) {
    // Expect "pub:" then one or more indented lines containing hex bytes with ':' separators.
    size_t p = out.find("pub:");
    if (p == std::string::npos) return false;
    p = out.find('\n', p);
    if (p == std::string::npos) return false;
    p++;

    std::string hex;
    while (p < out.size()) {
        size_t line_end = out.find('\n', p);
        std::string line = (line_end == std::string::npos) ? out.substr(p) : out.substr(p, line_end - p);
        std::string t = trim_line(line);
        if (t.empty()) {
            if (line_end == std::string::npos) break;
            p = line_end + 1;
            continue;
        }
        if (t.find("ASN1 OID") != std::string::npos || t.find("NIST") != std::string::npos) break;

        for (size_t i = 0; i < t.size(); i++) {
            char c = t[i];
            if (c == ':') continue;
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) hex.push_back(c);
        }

        if (line_end == std::string::npos) break;
        p = line_end + 1;
    }

    // uncompressed pubkey is 65 bytes => 130 hex chars
    if (hex.size() < 130) return false;
    // openssl sometimes appends extra lines; just take first 130.
    out_hex = hex.substr(0, 130);
    return true;
}

static bool derive_address_from_privkey(const std::vector<unsigned char>& priv32, std::string& out_addr_0x, std::string& out_pubkey_hex, std::string& out_err) {
    out_err.clear();

    const char* der_path = "/tmp/tb_ec_priv.der";
    if (!write_ec_privkey_der(priv32, der_path)) {
        out_err = "write_privkey_der_failed";
        return false;
    }

    std::string cmd = "/usr/bin/openssl ec -inform DER -in ";
    cmd += der_path;
    cmd += " -pubout -conv_form uncompressed -text -noout 2>&1";

    std::string out;
    if (!tradeboy::utils::run_cmd_capture(cmd, out)) {
        out_err = "openssl_ec_failed";
        return false;
    }

    std::string pub_hex;
    if (!parse_openssl_pubkey_uncompressed_hex(out, pub_hex)) {
        out_err = "parse_pubkey_failed";
        return false;
    }

    std::vector<unsigned char> pub_bytes;
    if (!tradeboy::utils::hex_to_bytes(pub_hex, pub_bytes)) {
        out_err = "pubkey_hex_to_bytes_failed";
        return false;
    }
    if (pub_bytes.size() != 65 || pub_bytes[0] != 0x04) {
        out_err = "pubkey_uncompressed_invalid";
        return false;
    }

    unsigned char h[32];
    tradeboy::utils::keccak_256(&pub_bytes[1], 64, h);

    out_addr_0x = tradeboy::utils::bytes_to_hex_lower(h + 12, 20, true);
    out_pubkey_hex = pub_hex;
    return true;
}

static bool generate_wallet(std::string& out_priv_0x, std::string& out_addr_0x, std::string& out_err) {
    out_err.clear();
    std::string r;
    if (!tradeboy::utils::read_true_random_bytes(32, r) || r.size() != 32) {
        out_err = "random_failed";
        return false;
    }

    std::vector<unsigned char> priv32(32);
    for (size_t i = 0; i < 32; i++) priv32[i] = (unsigned char)r[i];

    // Ensure it isn't all-zero (invalid)
    bool all_zero = true;
    for (size_t i = 0; i < 32; i++) if (priv32[i] != 0) { all_zero = false; break; }
    if (all_zero) {
        out_err = "privkey_all_zero";
        return false;
    }

    std::string pub_hex;
    std::string addr;
    if (!derive_address_from_privkey(priv32, addr, pub_hex, out_err)) {
        return false;
    }

    out_priv_0x = tradeboy::utils::bytes_to_hex_lower(&priv32[0], 32, true);
    out_addr_0x = addr;
    return true;
}

bool load_or_create_config(const std::string& path, WalletConfig& out_cfg, bool& out_created, std::string& out_err) {
    out_created = false;
    out_err.clear();

    std::string text = tradeboy::utils::read_text_file(path);
    if (!text.empty()) {
        parse_kv(text, "arb_rpc_url", out_cfg.arb_rpc_url);
        parse_kv(text, "wallet_address", out_cfg.wallet_address);
        parse_kv(text, "private_key", out_cfg.private_key);

        if (!out_cfg.arb_rpc_url.empty() && !out_cfg.wallet_address.empty() && !out_cfg.private_key.empty()) {
            return true;
        }
    }

    // Create new config
    const std::string default_rpc = "https://arb1.arbitrum.io/rpc";
    std::string priv, addr;
    std::string err;
    if (!generate_wallet(priv, addr, err)) {
        out_err = err;
        return false;
    }

    out_cfg.arb_rpc_url = default_rpc;
    out_cfg.wallet_address = addr;
    out_cfg.private_key = priv;

    std::string cfg_text = default_cfg_text(default_rpc, addr, priv);
    if (!tradeboy::utils::write_text_file(path, cfg_text)) {
        out_err = "write_cfg_failed";
        return false;
    }

    out_created = true;
    log_to_file("[CFG] created %s\n", path.c_str());
    log_to_file("[CFG] address=%s\n", addr.c_str());

    return true;
}

} // namespace tradeboy::wallet
