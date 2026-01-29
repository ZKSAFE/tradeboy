#pragma once

#include "imgui.h"

#include <vector>

#include "../model/TradeModel.h"

namespace tradeboy::spot {

void render_spot_screen(const std::vector<tradeboy::model::SpotRow>& rows,
                         int page_start_idx,
                         int selected_row_idx,
                         int action_idx,
                         bool buy_pressed,
                         bool sell_pressed,
                         ImFont* font_bold = nullptr,
                         bool action_btn_held = false,
                         bool l1_btn_held = false,
                         bool r1_btn_held = false);

} // namespace tradeboy::spot
