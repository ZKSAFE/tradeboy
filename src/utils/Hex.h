#pragma once

#include <string>
#include <vector>

namespace tradeboy::utils {

std::string bytes_to_hex_lower(const void* data, size_t n, bool with_0x);
bool hex_to_bytes(const std::string& hex, std::vector<unsigned char>& out_bytes);

} // namespace tradeboy::utils
