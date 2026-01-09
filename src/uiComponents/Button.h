#pragma once

#include "imgui.h"

namespace tradeboy::ui {

void draw_circle_button(ImDrawList* dl, ImVec2 center, float r, ImU32 bg, ImU32 fg, const char* label, bool pseudo_bold);
void draw_pill_button(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, ImU32 bg, ImU32 fg, const char* label, bool pseudo_bold);

} // namespace tradeboy::ui
