#pragma once

#include "imgui.h"

namespace tradeboy::ui {

struct Rect {
    ImVec2 Min;
    ImVec2 Max;
    Rect() : Min(0, 0), Max(0, 0) {}
    Rect(float x0, float y0, float x1, float y1) : Min(x0, y0), Max(x1, y1) {}
    float w() const { return Max.x - Min.x; }
    float h() const { return Max.y - Min.y; }
};

} // namespace tradeboy::ui
