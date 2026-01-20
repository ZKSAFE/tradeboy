#pragma once

#include <stddef.h>

namespace tradeboy::utils {

void keccak_256(const void* data, size_t len, unsigned char out32[32]);

} // namespace tradeboy::utils
