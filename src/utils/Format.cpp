#include "Format.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace tradeboy::utils {

double trunc_to_decimals(double v, int decimals) {
    if (decimals < 0) return v;
    const double p = std::pow(10.0, (double)decimals);
    return std::trunc(v * p) / p;
}

std::string format_price_sig(double v, int max_decimals) {
    const int sig = (std::isfinite(v) && std::fabs(v) > 100000.0) ? 6 : 5;
    return format_fixed_round_sig(v, sig, max_decimals);
}

std::string format_fixed_round_sig(double v, int max_sig_digits, int max_decimals) {
    if (!std::isfinite(v)) return "0";

    if (max_sig_digits <= 0) return "0";

    if (v == 0.0) {
        if (max_decimals > 0) {
            std::ostringstream ss;
            ss.setf(std::ios::fixed);
            ss.precision(std::min(2, max_decimals));
            ss << 0.0;
            return ss.str();
        }
        return "0";
    }

    const double abs_v = std::fabs(v);
    int decimals = 0;
    if (abs_v > 0.0) {
        const int exp = (int)std::floor(std::log10(abs_v));
        decimals = max_sig_digits - 1 - exp;
    }
    if (decimals < 0) decimals = 0;
    if (max_decimals >= 0) decimals = std::min(decimals, max_decimals);

    double p = std::pow(10.0, (double)decimals);
    double rv = std::round(v * p) / p;

    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(decimals);
    ss << rv;

    std::string out = ss.str();
    if (decimals > 0) {
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
    }
    if (out.empty()) out = "0";
    return out;
}

std::string format_fixed_trunc_sig(double v, int max_sig_digits, int max_decimals) {
    if (!std::isfinite(v)) return "0";

    if (v == 0.0) {
        if (max_decimals > 0) {
            std::ostringstream ss;
            ss.setf(std::ios::fixed);
            ss.precision(std::min(2, max_decimals));
            ss << 0.0;
            return ss.str();
        }
        return "0";
    }

    const double abs_v = std::fabs(v);

    if (abs_v < std::pow(10.0, -(double)max_sig_digits)) {
        if (max_decimals >= 2) return "0.00";
        return "0";
    }

    int int_digits = 1;
    if (abs_v >= 1.0) {
        int_digits = (int)std::floor(std::log10(abs_v)) + 1;
    } else {
        int_digits = 0;
    }

    int decimals = 0;
    if (int_digits < max_sig_digits) {
        decimals = std::min(max_decimals, max_sig_digits - int_digits);
    }

    double tv = trunc_to_decimals(v, decimals);

    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(decimals);
    ss << tv;

    std::string out = ss.str();
    if (decimals > 0) {
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
    }
    if (out.empty()) out = "0";
    return out;
}

} // namespace tradeboy::utils
