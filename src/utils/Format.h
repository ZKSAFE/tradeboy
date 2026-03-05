#pragma once

#include <string>

namespace tradeboy::utils {

double trunc_to_decimals(double v, int decimals);
std::string format_fixed_trunc_sig(double v, int max_sig_digits, int max_decimals);
std::string format_fixed_round_sig(double v, int max_sig_digits, int max_decimals);
std::string format_price_sig(double v, int max_decimals = 8);

} // namespace tradeboy::utils
