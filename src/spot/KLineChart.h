#pragma once

#include <vector>

#include "imgui.h"

#include "../uiComponents/Primitives.h"

namespace tradeboy::spot {

struct OHLC {
    float o;
    float h;
    float l;
    float c;
};

struct KLineStyle {
    ImU32 grid = 0;
    ImU32 green = 0;
    ImU32 red = 0;
    ImU32 muted = 0;
};

void render_kline(ImDrawList* dl, const tradeboy::ui::Rect& chart, const std::vector<OHLC>& ohlc, int num_h, const KLineStyle& style);

} // namespace tradeboy::spot
