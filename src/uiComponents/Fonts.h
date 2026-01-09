#pragma once

#include "imgui.h"

namespace tradeboy::ui {

struct Fonts {
    ImFont* body = nullptr;   // 44
    ImFont* large = nullptr;  // 66
    ImFont* small = nullptr;
};

Fonts& fonts();

// Loads fonts from font_path if not null. Returns true if any font loaded.
bool init_fonts(ImGuiIO& io, const char* font_path);

} // namespace tradeboy::ui
