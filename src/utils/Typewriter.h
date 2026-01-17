#pragma once

#include <string>

namespace tradeboy::utils {

struct TypewriterState {
    std::string last_text;
    double start_time = 0.0;
};

inline std::string typewriter_shown(TypewriterState& st, const std::string& full_text, double now_time, double chars_per_sec = 35.0) {
    if (full_text != st.last_text) {
        st.last_text = full_text;
        st.start_time = now_time;
    }

    int shown = (int)((now_time - st.start_time) * chars_per_sec);
    if (shown < 0) shown = 0;
    if (shown > (int)full_text.size()) shown = (int)full_text.size();
    return full_text.substr(0, (size_t)shown);
}

} // namespace tradeboy::utils
