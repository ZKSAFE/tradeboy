#pragma once

#include <string>

#include "imgui.h"

namespace tradeboy {
namespace ui {

struct DialogResult {
    bool confirm = false;
    bool cancel = false;
};

struct DialogLayout {
    bool active = false;
    ImVec4 rect_uv = ImVec4(0, 0, 0, 0);
};

DialogResult render_bottom_dialog(const char* prompt,
                                 const std::string& body,
                                 const char* btn_a,
                                 const char* btn_b,
                                 ImFont* font_bold,
                                 DialogLayout* out_layout);

DialogResult render_dialog(const char* id,
                           const char* prompt,
                           const std::string& body,
                           const char* btn_a,
                           const char* btn_b,
                           int* io_selected_btn,
                           int flash_frames,
                           float open_anim_t,
                           ImFont* font_bold,
                           DialogLayout* out_layout);

} // namespace ui
} // namespace tradeboy
