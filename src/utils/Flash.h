#pragma once

namespace tradeboy::utils {

inline bool blink_on(int frames, int period = 6, int on = 3) {
    return (frames > 0) && ((frames % period) < on);
}

inline bool blink_on_time(double now_time, double hz = 3.0) {
    return (((int)(now_time * hz)) % 2) == 0;
}

inline void dec_frame_counter(int& v) {
    if (v > 0) v--;
}

} // namespace tradeboy::utils
