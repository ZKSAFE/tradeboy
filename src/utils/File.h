#pragma once

#include <string>

namespace tradeboy::utils {

bool file_exists(const std::string& path);
std::string read_text_file(const std::string& path);
bool write_text_file(const std::string& path, const std::string& contents);
bool read_random_bytes(size_t n, std::string& out_bytes);
bool read_true_random_bytes(size_t n, std::string& out_bytes);
std::string trim(const std::string& s);
std::string normalize_hex_private_key(std::string s);

} // namespace tradeboy::utils
