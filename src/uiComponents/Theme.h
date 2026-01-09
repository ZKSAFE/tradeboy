#pragma once

#include "imgui.h"

namespace tradeboy::ui {

void apply_retro_style();

struct Colors {
    ImU32 panel;
    ImU32 panel2;
    ImU32 panel_pressed;
    ImU32 stroke;
    ImU32 text;
    ImU32 muted;
    ImU32 green;
    ImU32 red;
    ImU32 grid;
};

const Colors& colors();

} // namespace tradeboy::ui
