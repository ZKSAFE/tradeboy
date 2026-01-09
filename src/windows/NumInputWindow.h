#pragma once

#include <string>

#include "../app/Input.h"

namespace tradeboy::windows {

struct NumInputState {
    bool open = false;
    double max_value = 0.0;
    double value = 0.0;
    std::string text = "0";
    int focus_r = 0;
    int focus_c = 0;
    bool is_buy = true;
    std::string sym;

    bool show_error = false;
    std::string error_text;

    void reset(const std::string& in_sym, bool buy, double in_max);
    void close();

    bool parse_text();
    void set_from_percent(int percent);
    int current_percent() const;
    void adjust_percent_step(int delta);

    void append_char(char ch);
    void del();
    void ac();
    void maxv();

    bool over_max() const;
};

// Returns true if it consumed input (i.e. modal is open)
bool handle_input(NumInputState& st, const tradeboy::app::InputState& in, const tradeboy::app::EdgeState& edges);

void render(NumInputState& st);

} // namespace tradeboy::windows
