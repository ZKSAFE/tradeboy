#include "HyperliquidExchange.h"

#include "utils/Hex.h"
#include "utils/Keccak.h"
#include "utils/Process.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>

#include "utils/Log.h"

namespace tradeboy::market {

static bool write_file(const char* path, const std::string& s) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    size_t w = std::fwrite(s.data(), 1, s.size(), f);
    std::fclose(f);
    return w == s.size();
}

static bool http_post_json_wget(const char* url, const char* json_path, std::string& out_json) {
    out_json.clear();
    std::string cmd = "/usr/bin/wget -qO- --header=\"Content-Type: application/json\" --post-file=";
    cmd += json_path;
    cmd += " ";
    cmd += url;
    return tradeboy::utils::run_cmd_capture(cmd, out_json) && !out_json.empty();
}

static void bn_freep(BIGNUM*& b) {
    if (b) BN_free(b);
    b = nullptr;
}

static void ec_key_freep(EC_KEY*& k) {
    if (k) EC_KEY_free(k);
    k = nullptr;
}

static void ecdsa_sig_freep(ECDSA_SIG*& s) {
    if (s) ECDSA_SIG_free(s);
    s = nullptr;
}

static std::string addr_to_40hex_lower_no0x(const std::string& addr_0x) {
    std::string s = addr_0x;
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s = s.substr(2);
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    if (s.size() > 40) s = s.substr(s.size() - 40);
    if (s.size() < 40) s = std::string(40 - s.size(), '0') + s;
    return s;
}

static void store_u256_be(unsigned long long v, unsigned char out32[32]) {
    std::memset(out32, 0, 32);
    for (int i = 0; i < 8; i++) {
        out32[31 - i] = (unsigned char)(v & 0xFFu);
        v >>= 8;
    }
}

static void keccak_256_vec(const std::vector<unsigned char>& in, unsigned char out32[32]) {
    tradeboy::utils::keccak_256(in.data(), in.size(), out32);
}

static void keccak_256_str(const std::string& in, unsigned char out32[32]) {
    tradeboy::utils::keccak_256(in.data(), in.size(), out32);
}

static std::vector<unsigned char> cat32(const unsigned char a[32], const unsigned char b[32]) {
    std::vector<unsigned char> out;
    out.reserve(64);
    out.insert(out.end(), a, a + 32);
    out.insert(out.end(), b, b + 32);
    return out;
}

static std::vector<unsigned char> cat3_32(const unsigned char a[32], const unsigned char b[32], const unsigned char c[32]) {
    std::vector<unsigned char> out;
    out.reserve(96);
    out.insert(out.end(), a, a + 32);
    out.insert(out.end(), b, b + 32);
    out.insert(out.end(), c, c + 32);
    return out;
}

static std::vector<unsigned char> cat4_32(const unsigned char a[32], const unsigned char b[32], const unsigned char c[32], const unsigned char d[32]) {
    std::vector<unsigned char> out;
    out.reserve(128);
    out.insert(out.end(), a, a + 32);
    out.insert(out.end(), b, b + 32);
    out.insert(out.end(), c, c + 32);
    out.insert(out.end(), d, d + 32);
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
    BIGNUM* prv = BN_bin2bn(priv32.data(), 32, nullptr);
    if (!prv) {
        EC_KEY_free(key);
        out_err = "BN_bin2bn_failed";
        return false;
    }
    if (EC_KEY_set_private_key(key, prv) != 1) {
        BN_free(prv);
        EC_KEY_free(key);
        out_err = "set_private_failed";
        return false;
    }

    const EC_GROUP* group = EC_KEY_get0_group(key);
    EC_POINT* pub = EC_POINT_new(group);
    if (!pub) {
        BN_free(prv);
        EC_KEY_free(key);
        out_err = "EC_POINT_new_failed";
        return false;
    }
    if (EC_POINT_mul(group, pub, prv, nullptr, nullptr, nullptr) != 1) {
        EC_POINT_free(pub);
        BN_free(prv);
        EC_KEY_free(key);
        out_err = "EC_POINT_mul_failed";
        return false;
    }
    if (EC_KEY_set_public_key(key, pub) != 1) {
        EC_POINT_free(pub);
        BN_free(prv);
        EC_KEY_free(key);
        out_err = "set_public_failed";
        return false;
    }
    EC_POINT_free(pub);
    BN_free(prv);

    out_key = key;
    return true;
}

static bool secp256k1_sign_rs(const unsigned char hash32[32],
                             const std::vector<unsigned char>& priv32,
                             BIGNUM*& out_r,
                             BIGNUM*& out_s,
                             bool& out_s_was_high,
                             std::string& out_err) {
    out_err.clear();
    out_r = nullptr;
    out_s = nullptr;
    out_s_was_high = false;

    EC_KEY* key = nullptr;
    if (!secp256k1_key_from_priv(priv32, key, out_err)) return false;

    ECDSA_SIG* sig = ECDSA_do_sign(hash32, 32, key);
    ec_key_freep(key);
    if (!sig) {
        out_err = "ECDSA_do_sign_failed";
        return false;
    }

    const BIGNUM* r = nullptr;
    const BIGNUM* s = nullptr;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    ECDSA_SIG_get0(sig, &r, &s);
#else
    r = sig->r;
    s = sig->s;
#endif

    if (!r || !s) {
        ecdsa_sig_freep(sig);
        out_err = "sig_get_failed";
        return false;
    }

    // Enforce low-s per Ethereum.
    BN_CTX* ctx = BN_CTX_new();
    if (!ctx) {
        ecdsa_sig_freep(sig);
        out_err = "BN_CTX_new_failed";
        return false;
    }
    BN_CTX_start(ctx);

    BIGNUM* n = BN_CTX_get(ctx);
    BIGNUM* half_n = BN_CTX_get(ctx);
    if (!half_n) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        ecdsa_sig_freep(sig);
        out_err = "BN_CTX_get_failed";
        return false;
    }

    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (!group) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        ecdsa_sig_freep(sig);
        out_err = "EC_GROUP_new_failed";
        return false;
    }
    EC_GROUP_get_order(group, n, ctx);
    BN_rshift1(half_n, n);

    BIGNUM* s_adj = BN_dup(s);
    if (!s_adj) {
        EC_GROUP_free(group);
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        ecdsa_sig_freep(sig);
        out_err = "BN_dup_failed";
        return false;
    }

    if (BN_cmp(s_adj, half_n) > 0) {
        out_s_was_high = true;
        BN_sub(s_adj, n, s_adj);
    }

    out_r = BN_dup(r);
    out_s = s_adj;

    EC_GROUP_free(group);
    BN_CTX_end(ctx);
    BN_CTX_free(ctx);
    ecdsa_sig_freep(sig);

    if (!out_r || !out_s) {
        bn_freep(out_r);
        bn_freep(out_s);
        out_err = "dup_failed";
        return false;
    }

    return true;
}

static bool secp256k1_compute_recid(const unsigned char hash32[32],
                                   const std::vector<unsigned char>& priv32,
                                   const BIGNUM* r,
                                   const BIGNUM* s,
                                   int& out_recid,
                                   std::string& out_err) {
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
    BIGNUM* e = BN_CTX_get(ctx);
    if (!e) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        ec_key_freep(key);
        out_err = "BN_CTX_get_failed";
        return false;
    }

    EC_GROUP_get_order(group, n, ctx);
    EC_GROUP_get_curve_GFp(group, p, a, b, ctx);

    BN_bin2bn(hash32, 32, e);
    BN_mod(e, e, n, ctx);

    BIGNUM* rinv = BN_mod_inverse(nullptr, r, n, ctx);
    if (!rinv) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        ec_key_freep(key);
        out_err = "rinv_failed";
        return false;
    }

    for (int recid = 0; recid < 4; recid++) {
        BIGNUM* x = BN_new();
        BIGNUM* j = BN_new();
        BIGNUM* y2 = BN_new();
        BIGNUM* y = BN_new();
        if (!x || !j || !y2 || !y) {
            if (x) BN_free(x);
            if (j) BN_free(j);
            if (y2) BN_free(y2);
            if (y) BN_free(y);
            BN_free(rinv);
            BN_CTX_end(ctx);
            BN_CTX_free(ctx);
            ec_key_freep(key);
            out_err = "BN_new_failed";
            return false;
        }

        BN_copy(x, r);
        if (recid >= 2) {
            BN_set_word(j, 1);
            BN_mul(j, j, n, ctx);
            BN_add(x, x, j);
        }
        if (BN_cmp(x, p) >= 0) {
            BN_free(x);
            BN_free(j);
            BN_free(y2);
            BN_free(y);
            continue;
        }

        BN_mod_sqr(y2, x, p, ctx);
        BN_mod_mul(y2, y2, x, p, ctx);
        BN_mod_add(y2, y2, b, p, ctx);

        BIGNUM* y_sqrt = BN_mod_sqrt(nullptr, y2, p, ctx);
        if (!y_sqrt) {
            BN_free(x);
            BN_free(j);
            BN_free(y2);
            BN_free(y);
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
            BN_free(x);
            BN_free(j);
            BN_free(y2);
            BN_free(y);
            BN_free(rinv);
            BN_CTX_end(ctx);
            BN_CTX_free(ctx);
            ec_key_freep(key);
            out_err = "EC_POINT_new_failed";
            return false;
        }
        if (EC_POINT_set_affine_coordinates_GFp(group, R, x, y, ctx) != 1) {
            EC_POINT_free(R);
            BN_free(x);
            BN_free(j);
            BN_free(y2);
            BN_free(y);
            continue;
        }

        // Q = r^-1 (sR - eG)
        EC_POINT* sR = EC_POINT_new(group);
        EC_POINT* eG = EC_POINT_new(group);
        EC_POINT* sR_minus_eG = EC_POINT_new(group);
        EC_POINT* Q = EC_POINT_new(group);
        if (!Q || !sR || !eG || !sR_minus_eG) {
            if (sR) EC_POINT_free(sR);
            if (eG) EC_POINT_free(eG);
            if (sR_minus_eG) EC_POINT_free(sR_minus_eG);
            if (Q) EC_POINT_free(Q);
            EC_POINT_free(R);
            BN_free(x);
            BN_free(j);
            BN_free(y2);
            BN_free(y);
            BN_free(rinv);
            BN_CTX_end(ctx);
            BN_CTX_free(ctx);
            ec_key_freep(key);
            out_err = "EC_POINT_alloc_failed";
            return false;
        }

        EC_POINT_mul(group, sR, nullptr, R, s, ctx);
        EC_POINT_mul(group, eG, e, nullptr, nullptr, ctx);
        EC_POINT_invert(group, eG, ctx);
        EC_POINT_add(group, sR_minus_eG, sR, eG, ctx);
        EC_POINT_mul(group, Q, nullptr, sR_minus_eG, rinv, ctx);

        int eq = EC_POINT_cmp(group, Q, pub, ctx);

        EC_POINT_free(sR);
        EC_POINT_free(eG);
        EC_POINT_free(sR_minus_eG);
        EC_POINT_free(Q);
        EC_POINT_free(R);

        BN_free(x);
        BN_free(j);
        BN_free(y2);
        BN_free(y);

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

static void bn_to_32be(const BIGNUM* bn, unsigned char out32[32]) {
    std::memset(out32, 0, 32);
    int n = BN_num_bytes(bn);
    if (n <= 0) return;
    std::vector<unsigned char> tmp((size_t)n);
    BN_bn2bin(bn, tmp.data());
    size_t copy_n = std::min((size_t)32, tmp.size());
    std::memcpy(out32 + (32 - copy_n), tmp.data() + (tmp.size() - copy_n), copy_n);
}

static std::string eth_addr_from_pubkey_point(const EC_GROUP* group, const EC_POINT* pub, BN_CTX* ctx) {
    std::string out;
    BIGNUM* x = BN_new();
    BIGNUM* y = BN_new();
    if (!x || !y) {
        if (x) BN_free(x);
        if (y) BN_free(y);
        return out;
    }

    if (EC_POINT_get_affine_coordinates_GFp(group, pub, x, y, ctx) != 1) {
        BN_free(x);
        BN_free(y);
        return out;
    }

    unsigned char x32[32];
    unsigned char y32[32];
    bn_to_32be(x, x32);
    bn_to_32be(y, y32);
    BN_free(x);
    BN_free(y);

    unsigned char pub64[64];
    std::memcpy(pub64, x32, 32);
    std::memcpy(pub64 + 32, y32, 32);

    unsigned char h[32];
    tradeboy::utils::keccak_256(pub64, 64, h);

    std::string addr_no0x = tradeboy::utils::bytes_to_hex_lower(h + 12, 20, false);
    out = std::string("0x") + addr_no0x;
    return out;
}

static bool recover_pubkey_from_sig(const unsigned char hash32[32],
                                   const BIGNUM* r,
                                   const BIGNUM* s,
                                   int recid,
                                   EC_POINT*& out_Q,
                                   std::string& out_err) {
    out_err.clear();
    out_Q = nullptr;
    if (recid < 0 || recid > 3) {
        out_err = "recid_range";
        return false;
    }

    EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (!group) {
        out_err = "EC_GROUP_new_failed";
        return false;
    }

    BN_CTX* ctx = BN_CTX_new();
    if (!ctx) {
        EC_GROUP_free(group);
        out_err = "BN_CTX_new_failed";
        return false;
    }
    BN_CTX_start(ctx);

    BIGNUM* n = BN_CTX_get(ctx);
    BIGNUM* p = BN_CTX_get(ctx);
    BIGNUM* a = BN_CTX_get(ctx);
    BIGNUM* b = BN_CTX_get(ctx);
    BIGNUM* e = BN_CTX_get(ctx);
    if (!e) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        EC_GROUP_free(group);
        out_err = "BN_CTX_get_failed";
        return false;
    }

    EC_GROUP_get_order(group, n, ctx);
    EC_GROUP_get_curve_GFp(group, p, a, b, ctx);

    BN_bin2bn(hash32, 32, e);
    BN_mod(e, e, n, ctx);

    BIGNUM* rinv = BN_mod_inverse(nullptr, r, n, ctx);
    if (!rinv) {
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        EC_GROUP_free(group);
        out_err = "rinv_failed";
        return false;
    }

    BIGNUM* x = BN_new();
    BIGNUM* j = BN_new();
    BIGNUM* y2 = BN_new();
    BIGNUM* y = BN_new();
    if (!x || !j || !y2 || !y) {
        if (x) BN_free(x);
        if (j) BN_free(j);
        if (y2) BN_free(y2);
        if (y) BN_free(y);
        BN_free(rinv);
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        EC_GROUP_free(group);
        out_err = "BN_new_failed";
        return false;
    }

    BN_copy(x, r);
    if (recid >= 2) {
        BN_set_word(j, 1);
        BN_mul(j, j, n, ctx);
        BN_add(x, x, j);
    }

    if (BN_cmp(x, p) >= 0) {
        BN_free(x);
        BN_free(j);
        BN_free(y2);
        BN_free(y);
        BN_free(rinv);
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        EC_GROUP_free(group);
        out_err = "x_ge_p";
        return false;
    }

    BN_mod_sqr(y2, x, p, ctx);
    BN_mod_mul(y2, y2, x, p, ctx);
    BN_mod_add(y2, y2, b, p, ctx);

    BIGNUM* y_sqrt = BN_mod_sqrt(nullptr, y2, p, ctx);
    if (!y_sqrt) {
        BN_free(x);
        BN_free(j);
        BN_free(y2);
        BN_free(y);
        BN_free(rinv);
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        EC_GROUP_free(group);
        out_err = "sqrt_failed";
        return false;
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
        BN_free(x);
        BN_free(j);
        BN_free(y2);
        BN_free(y);
        BN_free(rinv);
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        EC_GROUP_free(group);
        out_err = "EC_POINT_new_failed";
        return false;
    }
    if (EC_POINT_set_affine_coordinates_GFp(group, R, x, y, ctx) != 1) {
        EC_POINT_free(R);
        BN_free(x);
        BN_free(j);
        BN_free(y2);
        BN_free(y);
        BN_free(rinv);
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        EC_GROUP_free(group);
        out_err = "set_affine_failed";
        return false;
    }

    EC_POINT* sR = EC_POINT_new(group);
    EC_POINT* eG = EC_POINT_new(group);
    EC_POINT* sR_minus_eG = EC_POINT_new(group);
    EC_POINT* Q = EC_POINT_new(group);
    if (!Q || !sR || !eG || !sR_minus_eG) {
        if (sR) EC_POINT_free(sR);
        if (eG) EC_POINT_free(eG);
        if (sR_minus_eG) EC_POINT_free(sR_minus_eG);
        if (Q) EC_POINT_free(Q);
        EC_POINT_free(R);
        BN_free(x);
        BN_free(j);
        BN_free(y2);
        BN_free(y);
        BN_free(rinv);
        BN_CTX_end(ctx);
        BN_CTX_free(ctx);
        EC_GROUP_free(group);
        out_err = "EC_POINT_alloc_failed";
        return false;
    }

    EC_POINT_mul(group, sR, nullptr, R, s, ctx);
    EC_POINT_mul(group, eG, e, nullptr, nullptr, ctx);
    EC_POINT_invert(group, eG, ctx);
    EC_POINT_add(group, sR_minus_eG, sR, eG, ctx);
    EC_POINT_mul(group, Q, nullptr, sR_minus_eG, rinv, ctx);

    EC_POINT_free(sR);
    EC_POINT_free(eG);
    EC_POINT_free(sR_minus_eG);
    EC_POINT_free(R);

    BN_free(x);
    BN_free(j);
    BN_free(y2);
    BN_free(y);
    BN_free(rinv);

    out_Q = Q;

    BN_CTX_end(ctx);
    BN_CTX_free(ctx);
    EC_GROUP_free(group);
    return true;
}

static unsigned long long parse_hex_u64(const std::string& hex) {
    size_t i = 0;
    unsigned long long v = 0;
    std::string s = hex;
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) i = 2;
    for (; i < s.size(); i++) {
        char c = s[i];
        unsigned int d = 0;
        if (c >= '0' && c <= '9') d = (unsigned int)(c - '0');
        else if (c >= 'a' && c <= 'f') d = 10u + (unsigned int)(c - 'a');
        else if (c >= 'A' && c <= 'F') d = 10u + (unsigned int)(c - 'A');
        else break;
        v = (v << 4) | (unsigned long long)d;
    }
    return v;
}

static void eip712_hash_usd_class_transfer(const std::string& signature_chain_id_hex,
                                          const std::string& hyperliquid_chain,
                                          const std::string& amount_str,
                                          bool to_perp,
                                          unsigned long long nonce,
                                          unsigned char out_digest32[32]) {
    unsigned char typehash_domain[32];
    {
        const char* t = "EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)";
        keccak_256_str(t, typehash_domain);
    }

    unsigned char name_hash[32];
    unsigned char version_hash[32];
    keccak_256_str("HyperliquidSignTransaction", name_hash);
    keccak_256_str("1", version_hash);

    unsigned char chain_id_u256[32];
    store_u256_be(parse_hex_u64(signature_chain_id_hex), chain_id_u256);

    unsigned char verifying_contract[32];
    std::memset(verifying_contract, 0, 32);

    unsigned char domain_sep[32];
    {
        std::vector<unsigned char> enc = cat4_32(typehash_domain, name_hash, version_hash, chain_id_u256);
        enc.insert(enc.end(), verifying_contract, verifying_contract + 32);
        tradeboy::utils::keccak_256(enc.data(), enc.size(), domain_sep);
    }

    unsigned char typehash_msg[32];
    {
        const char* t = "HyperliquidTransaction:UsdClassTransfer(string hyperliquidChain,string amount,bool toPerp,uint64 nonce)";
        keccak_256_str(t, typehash_msg);
    }

    unsigned char chain_hash[32];
    unsigned char amount_hash[32];
    keccak_256_str(hyperliquid_chain, chain_hash);
    keccak_256_str(amount_str, amount_hash);

    unsigned char to_perp_u256[32];
    std::memset(to_perp_u256, 0, 32);
    to_perp_u256[31] = to_perp ? 1 : 0;

    unsigned char nonce_u256[32];
    store_u256_be(nonce, nonce_u256);

    unsigned char msg_hash[32];
    {
        std::vector<unsigned char> enc = cat4_32(typehash_msg, chain_hash, amount_hash, to_perp_u256);
        enc.insert(enc.end(), nonce_u256, nonce_u256 + 32);
        tradeboy::utils::keccak_256(enc.data(), enc.size(), msg_hash);
    }

    unsigned char prefix[2] = {0x19, 0x01};
    std::vector<unsigned char> dig;
    dig.reserve(2 + 32 + 32);
    dig.push_back(prefix[0]);
    dig.push_back(prefix[1]);
    dig.insert(dig.end(), domain_sep, domain_sep + 32);
    dig.insert(dig.end(), msg_hash, msg_hash + 32);
    tradeboy::utils::keccak_256(dig.data(), dig.size(), out_digest32);
}

static void eip712_hash_withdraw3(const std::string& signature_chain_id_hex,
                                  const std::string& hyperliquid_chain,
                                  const std::string& destination_addr_0x,
                                  const std::string& amount_str,
                                  unsigned long long time_ms,
                                  unsigned char out_digest32[32]) {
    unsigned char typehash_domain[32];
    {
        const char* t = "EIP712Domain(string name,string version,uint256 chainId,address verifyingContract)";
        keccak_256_str(t, typehash_domain);
    }

    unsigned char name_hash[32];
    unsigned char version_hash[32];
    keccak_256_str("HyperliquidSignTransaction", name_hash);
    keccak_256_str("1", version_hash);

    unsigned char chain_id_u256[32];
    store_u256_be(parse_hex_u64(signature_chain_id_hex), chain_id_u256);

    unsigned char verifying_contract[32];
    std::memset(verifying_contract, 0, 32);

    unsigned char domain_sep[32];
    {
        std::vector<unsigned char> enc = cat4_32(typehash_domain, name_hash, version_hash, chain_id_u256);
        enc.insert(enc.end(), verifying_contract, verifying_contract + 32);
        tradeboy::utils::keccak_256(enc.data(), enc.size(), domain_sep);
    }

    unsigned char typehash_msg[32];
    {
        const char* t = "HyperliquidTransaction:Withdraw(string hyperliquidChain,string destination,string amount,uint64 time)";
        keccak_256_str(t, typehash_msg);
    }

    unsigned char chain_hash[32];
    unsigned char dest_hash[32];
    unsigned char amount_hash[32];
    keccak_256_str(hyperliquid_chain, chain_hash);

    std::string dest_norm = std::string("0x") + addr_to_40hex_lower_no0x(destination_addr_0x);
    keccak_256_str(dest_norm, dest_hash);
    keccak_256_str(amount_str, amount_hash);

    unsigned char time_u256[32];
    store_u256_be(time_ms, time_u256);

    unsigned char msg_hash[32];
    {
        std::vector<unsigned char> enc = cat4_32(typehash_msg, chain_hash, dest_hash, amount_hash);
        enc.insert(enc.end(), time_u256, time_u256 + 32);
        tradeboy::utils::keccak_256(enc.data(), enc.size(), msg_hash);
    }

    unsigned char prefix[2] = {0x19, 0x01};
    std::vector<unsigned char> dig;
    dig.reserve(2 + 32 + 32);
    dig.push_back(prefix[0]);
    dig.push_back(prefix[1]);
    dig.insert(dig.end(), domain_sep, domain_sep + 32);
    dig.insert(dig.end(), msg_hash, msg_hash + 32);
    tradeboy::utils::keccak_256(dig.data(), dig.size(), out_digest32);
}

static bool sign_digest_eth(const unsigned char digest32[32],
                            const std::vector<unsigned char>& priv32,
                            const std::string& expected_wallet_addr_0x,
                            std::string& out_r_0x,
                            std::string& out_s_0x,
                            int& out_v,
                            std::string& out_err) {
    out_err.clear();
    out_r_0x.clear();
    out_s_0x.clear();
    out_v = 0;

    std::string expected = addr_to_40hex_lower_no0x(expected_wallet_addr_0x);
    expected = std::string("0x") + expected;

    for (int attempt = 0; attempt < 12; attempt++) {
        BIGNUM* r = nullptr;
        BIGNUM* s = nullptr;
        bool s_was_high = false;
        if (!secp256k1_sign_rs(digest32, priv32, r, s, s_was_high, out_err)) {
            bn_freep(r);
            bn_freep(s);
            return false;
        }

        int found_recid = -1;
        for (int recid = 0; recid < 4; recid++) {
            const int effective_recid = recid ^ (s_was_high ? 1 : 0);
            EC_POINT* Q = nullptr;
            std::string rec_err;
            if (!recover_pubkey_from_sig(digest32, r, s, effective_recid, Q, rec_err)) {
                continue;
            }

            BN_CTX* ctx = BN_CTX_new();
            if (!ctx) {
                EC_POINT_free(Q);
                continue;
            }
            EC_GROUP* group = EC_GROUP_new_by_curve_name(NID_secp256k1);
            if (!group) {
                BN_CTX_free(ctx);
                EC_POINT_free(Q);
                continue;
            }
            std::string got = eth_addr_from_pubkey_point(group, Q, ctx);
            EC_GROUP_free(group);
            BN_CTX_free(ctx);
            EC_POINT_free(Q);

            if (!got.empty()) {
                std::string line = std::string("[HLX] recover recid=") + std::to_string(effective_recid) + " addr=" + got + "\n";
                log_str(line.c_str());
            }

            if (got == expected) {
                found_recid = effective_recid;
                break;
            }
        }

        if (found_recid < 0) {
            bn_freep(r);
            bn_freep(s);
            continue;
        }

        unsigned char r32[32];
        unsigned char s32[32];
        bn_to_32be(r, r32);
        bn_to_32be(s, s32);

        bn_freep(r);
        bn_freep(s);

        out_r_0x = tradeboy::utils::bytes_to_hex_lower(r32, 32, true);
        out_s_0x = tradeboy::utils::bytes_to_hex_lower(s32, 32, true);
        out_v = 27 + (found_recid & 1);
        return true;
    }

    out_err = "recover_mismatch";
    return false;
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n') {
            out += "\\n";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

bool exchange_usd_class_transfer(const std::string& wallet_address_0x,
                                const std::string& private_key_hex,
                                bool to_perp,
                                const std::string& amount_str,
                                unsigned long long nonce_ms,
                                bool is_mainnet,
                                std::string& out_resp,
                                std::string& out_err) {
    out_resp.clear();
    out_err.clear();

    std::vector<unsigned char> priv;
    if (!tradeboy::utils::hex_to_bytes(private_key_hex, priv) || priv.size() != 32) {
        out_err = "invalid_private_key";
        return false;
    }

    const std::string signature_chain_id = "0x66eee";
    const std::string hl_chain = is_mainnet ? "Mainnet" : "Testnet";

    unsigned char digest[32];
    eip712_hash_usd_class_transfer(signature_chain_id, hl_chain, amount_str, to_perp, nonce_ms, digest);

    std::string r_0x, s_0x;
    int v = 0;
    std::string sign_err;
    if (!sign_digest_eth(digest, priv, wallet_address_0x, r_0x, s_0x, v, sign_err)) {
        out_err = std::string("sign_failed:") + sign_err;
        return false;
    }
    std::string action_json = std::string("{") +
                              "\"type\":\"usdClassTransfer\"," +
                              "\"hyperliquidChain\":\"" + hl_chain + "\"," +
                              "\"signatureChainId\":\"" + signature_chain_id + "\"," +
                              "\"amount\":\"" + json_escape(amount_str) + "\"," +
                              "\"toPerp\":" + (to_perp ? "true" : "false") + "," +
                              "\"nonce\":" + std::to_string(nonce_ms) +
                              "}";

    std::string payload = std::string("{") +
                          "\"action\":" + action_json + "," +
                          "\"nonce\":" + std::to_string(nonce_ms) + "," +
                          "\"signature\":{" +
                          "\"r\":\"" + r_0x + "\"," +
                          "\"s\":\"" + s_0x + "\"," +
                          "\"v\":" + std::to_string(v) +
                          "}" +
                          "}\n";

    const char* path = "/tmp/hl_exchange_req.json";
    if (!write_file(path, payload)) {
        out_err = "write_req_failed";
        return false;
    }

    const char* url = is_mainnet ? "https://api.hyperliquid.xyz/exchange" : "https://api.hyperliquid-testnet.xyz/exchange";
    std::string resp;
    if (!http_post_json_wget(url, path, resp)) {
        out_err = "http_post_failed";
        out_resp = resp;
        log_str("[HLX] exchange http_post_failed\n");
        return false;
    }

    out_resp = resp;

    // Very light success detection.
    if (resp.find("\"error\"") != std::string::npos) {
        out_err = "exchange_error";
        std::string prefix = resp.substr(0, std::min<size_t>(256, resp.size()));
        std::string line = std::string("[HLX] exchange_error resp_prefix=") + prefix + "\n";
        log_str(line.c_str());
        return false;
    }

    return true;
}

bool exchange_withdraw3(const std::string& wallet_address_0x,
                        const std::string& private_key_hex,
                        const std::string& destination_addr_0x,
                        const std::string& amount_str,
                        unsigned long long nonce_ms,
                        bool is_mainnet,
                        std::string& out_resp,
                        std::string& out_err) {
    out_resp.clear();
    out_err.clear();

    std::vector<unsigned char> priv;
    if (!tradeboy::utils::hex_to_bytes(private_key_hex, priv) || priv.size() != 32) {
        out_err = "invalid_private_key";
        return false;
    }

    const std::string signature_chain_id = "0x66eee";
    const std::string hl_chain = is_mainnet ? "Mainnet" : "Testnet";

    {
        std::string dest_norm = std::string("0x") + addr_to_40hex_lower_no0x(destination_addr_0x);
        std::string line = std::string("[HLW] withdraw3 req amount=") + amount_str +
                           " dest=" + dest_norm +
                           " time=" + std::to_string(nonce_ms) + "\n";
        log_str(line.c_str());
    }

    unsigned char digest[32];
    eip712_hash_withdraw3(signature_chain_id, hl_chain, destination_addr_0x, amount_str, nonce_ms, digest);
    {
        std::string dig_0x = tradeboy::utils::bytes_to_hex_lower(digest, 32, true);
        std::string line = std::string("[HLW] digest=") + dig_0x + "\n";
        log_str(line.c_str());
    }

    std::string r_0x, s_0x;
    int v = 0;
    std::string sign_err;
    if (!sign_digest_eth(digest, priv, wallet_address_0x, r_0x, s_0x, v, sign_err)) {
        out_err = std::string("sign_failed:") + sign_err;
        log_str("[HLW] sign_failed\n");
        return false;
    }

    {
        std::string r_p = r_0x.substr(0, std::min<size_t>(12, r_0x.size()));
        std::string s_p = s_0x.substr(0, std::min<size_t>(12, s_0x.size()));
        std::string line = std::string("[HLW] sig v=") + std::to_string(v) + " r=" + r_p + " s=" + s_p + "\n";
        log_str(line.c_str());
    }

    std::string dest_norm = std::string("0x") + addr_to_40hex_lower_no0x(destination_addr_0x);
    std::string action_json = std::string("{") +
                              "\"type\":\"withdraw3\"," +
                              "\"hyperliquidChain\":\"" + hl_chain + "\"," +
                              "\"signatureChainId\":\"" + signature_chain_id + "\"," +
                              "\"amount\":\"" + json_escape(amount_str) + "\"," +
                              "\"time\":" + std::to_string(nonce_ms) + "," +
                              "\"destination\":\"" + json_escape(dest_norm) + "\"" +
                              "}";

    std::string payload = std::string("{") +
                          "\"action\":" + action_json + "," +
                          "\"nonce\":" + std::to_string(nonce_ms) + "," +
                          "\"signature\":{" +
                          "\"r\":\"" + r_0x + "\"," +
                          "\"s\":\"" + s_0x + "\"," +
                          "\"v\":" + std::to_string(v) +
                          "}" +
                          "}\n";

    const char* path = "/tmp/hl_withdraw_req.json";
    if (!write_file(path, payload)) {
        out_err = "write_req_failed";
        log_str("[HLW] write_req_failed\n");
        return false;
    }

    const char* url = is_mainnet ? "https://api.hyperliquid.xyz/exchange" : "https://api.hyperliquid-testnet.xyz/exchange";
    std::string resp;
    if (!http_post_json_wget(url, path, resp)) {
        out_err = "http_post_failed";
        out_resp = resp;
        log_str("[HLW] http_post_failed\n");
        return false;
    }

    {
        std::string prefix = resp.substr(0, std::min<size_t>(512, resp.size()));
        std::string line = std::string("[HLW] resp_prefix=") + prefix + "\n";
        log_str(line.c_str());
    }

    out_resp = resp;
    if (resp.find("\"error\"") != std::string::npos) {
        out_err = "exchange_error";
        return false;
    }

    return true;
}

} // namespace tradeboy::market
