#pragma once

#include <string>

namespace tradeboy::utils {

std::string read_text_file(const std::string& path);
std::string trim(const std::string& s);
std::string normalize_hex_private_key(std::string s);

} // namespace tradeboy::utils
