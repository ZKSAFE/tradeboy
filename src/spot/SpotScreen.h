#pragma once

#include "imgui.h"

namespace tradeboy::spot {

void render_spot_screen(int selected_row_idx, int action_idx, bool buy_pressed, bool sell_pressed);

} // namespace tradeboy::spot
