#include "File.h"

#include <fstream>
#include <sstream>

#include <cstdio>

namespace tradeboy::utils {

bool file_exists(const std::string& path) {
    std::ifstream in(path);
    return (bool)in;
}

std::string read_text_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_text_file(const std::string& path, const std::string& contents) {
    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) return false;
    out << contents;
    return (bool)out;
}

bool read_random_bytes(size_t n, std::string& out_bytes) {
    out_bytes.clear();
    if (n == 0) return true;

    FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) return false;
    out_bytes.resize(n);
    size_t got = std::fread(&out_bytes[0], 1, n, f);
    std::fclose(f);
    if (got != n) {
        out_bytes.clear();
        return false;
    }
    return true;
}

bool read_true_random_bytes(size_t n, std::string& out_bytes) {
    out_bytes.clear();
    if (n == 0) return true;

    FILE* f = std::fopen("/dev/random", "rb");
    if (!f) {
        // Fallback if /dev/random is not available.
        return read_random_bytes(n, out_bytes);
    }
    out_bytes.resize(n);
    size_t got = std::fread(&out_bytes[0], 1, n, f);
    std::fclose(f);
    if (got != n) {
        out_bytes.clear();
        return false;
    }
    return true;
}

std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) a++;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

std::string normalize_hex_private_key(std::string s) {
    s = trim(s);
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
    return s;
}

} // namespace tradeboy::utils
