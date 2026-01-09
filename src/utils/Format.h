#pragma once

#include <string>

namespace tradeboy::utils {

double trunc_to_decimals(double v, int decimals);
std::string format_fixed_trunc_sig(double v, int max_sig_digits, int max_decimals);

} // namespace tradeboy::utils
