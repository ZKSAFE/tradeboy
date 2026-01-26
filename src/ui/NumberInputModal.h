/**
 * @file NumberInputModal.h
 * @brief Generic number input modal with keypad, range validation, and customizable appearance.
 * 
 * Usage:
 * 1. Create NumberInputState instance
 * 2. Call open_with() to show modal with custom title, color, range
 * 3. Call handle_input() each frame to process input
 * 4. Call render() each frame to draw
 * 5. Check result after modal closes
 */
#pragma once

#include <string>
#include <functional>

#include "imgui.h"

#include "../app/Input.h"
#include "../ui/DialogState.h"

namespace tradeboy::ui {

enum class NumberInputResult {
    None = 0,
    Confirmed = 1,
    Cancelled = 2,
    OutOfRange = 3,
};

struct NumberInputConfig {
    std::string title;
    ImU32 title_color = IM_COL32(0, 255, 65, 255);  // Default: MatrixTheme::TEXT
    
    double min_value = 0.0;
    double max_value = 0.0;
    std::string available_label;  // e.g. "USDC", "BTC"
    
    std::string price_label;      // e.g. "PRICE: $87482.75"
    double price = 0.0;           // For USD approximation display
    
    bool show_available_panel = true;
};

struct NumberInputState {
    bool open = false;

    int open_frames = 0;
    bool closing = false;
    int close_frames = 0;
    
    NumberInputConfig config;

    tradeboy::ui::DialogState out_of_range_dialog;
    
    std::string input = "0";
    
    int grid_r = 0;
    int grid_c = 1;
    int footer_idx = -1;
    
    int flash_timer = 0;
    int flash_btn_idx = -1;
    
    int l1_flash_timer = 0;
    int r1_flash_timer = 0;
    int b_flash_timer = 0;
    
    NumberInputResult result = NumberInputResult::None;
    double result_value = 0.0;

    NumberInputResult pending_result = NumberInputResult::None;
    double pending_result_value = 0.0;
    
    void open_with(const NumberInputConfig& cfg);
    void close();
    void close_with_result(NumberInputResult res, double value);

    float get_open_t() const;
    bool tick_open_anim();
    bool tick_close_anim(int close_dur = 18);
    
    double get_input_value() const;
    bool is_in_range() const;
};

bool handle_input(NumberInputState& st, const tradeboy::app::InputState& in, const tradeboy::app::EdgeState& edges);

void render(NumberInputState& st, ImFont* font_bold);

} // namespace tradeboy::ui
