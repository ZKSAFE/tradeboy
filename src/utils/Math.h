#pragma once

#include <algorithm>

namespace tradeboy::utils {

inline int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }
inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

inline bool pressed(bool now, bool prev) { return now && !prev; }

} // namespace tradeboy::utils
