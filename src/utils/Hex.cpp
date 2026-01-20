#include "Hex.h"

#include <cctype>

namespace tradeboy::utils {

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return (int)(c - '0');
    c = (char)std::tolower((unsigned char)c);
    if (c >= 'a' && c <= 'f') return 10 + (int)(c - 'a');
    return -1;
}

std::string bytes_to_hex_lower(const void* data, size_t n, bool with_0x) {
    static const char* k = "0123456789abcdef";
    const unsigned char* p = (const unsigned char*)data;
    std::string out;
    out.reserve((with_0x ? 2 : 0) + n * 2);
    if (with_0x) {
        out.push_back('0');
        out.push_back('x');
    }
    for (size_t i = 0; i < n; i++) {
        unsigned char b = p[i];
        out.push_back(k[(b >> 4) & 0xF]);
        out.push_back(k[b & 0xF]);
    }
    return out;
}

bool hex_to_bytes(const std::string& hex, std::vector<unsigned char>& out_bytes) {
    out_bytes.clear();
    size_t i = 0;
    if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) i = 2;

    // skip leading whitespace
    while (i < hex.size() && (hex[i] == ' ' || hex[i] == '\t' || hex[i] == '\n' || hex[i] == '\r')) i++;

    size_t n = hex.size() - i;
    if (n == 0) return true;
    if (n % 2 != 0) return false;

    out_bytes.reserve(n / 2);
    for (; i < hex.size(); i += 2) {
        int hi = hex_nibble(hex[i]);
        int lo = hex_nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out_bytes.push_back((unsigned char)((hi << 4) | lo));
    }
    return true;
}

} // namespace tradeboy::utils
