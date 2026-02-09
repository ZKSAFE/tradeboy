#pragma once

#include <vector>

#include "imgui.h"

#include "../model/TradeModel.h"

namespace tradeboy::perp {

void render_perp_screen(const std::vector<tradeboy::model::PerpRow>& rows,
                        int page_start_idx,
                        int selected_row_idx,
                        int action_idx,
                        bool primary_pressed,
                        bool close_pressed,
                        ImFont* font_bold);

} // namespace tradeboy::perp
