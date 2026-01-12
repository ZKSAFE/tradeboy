#include "File.h"

#include <fstream>
#include <sstream>

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
