#pragma once

#include <string>
#include <vector>

#include "imgui.h"

#include "KLineChart.h"

namespace tradeboy::spot {

struct SpotRowVM {
    std::string sym;
    std::string price;
    ImU32 price_col = 0;
};

struct SpotHeaderVM {
    std::string pair;
    std::string price;
    std::string change;
    ImU32 change_col = 0;

    std::string tf;
    bool x_pressed = false;
};

struct SpotBottomVM {
    std::string hold;
    std::string val;
    std::string pnl;
    ImU32 pnl_col = 0;

    bool buy_hover = false;
    bool sell_hover = false;
    bool buy_pressed = false;
    bool sell_pressed = false;
};

struct SpotViewModel {
    SpotHeaderVM header;
    std::vector<SpotRowVM> rows;
    int selected_row_idx = 0;

    std::vector<tradeboy::spot::OHLC> kline_data;

    SpotBottomVM bottom;
};

} // namespace tradeboy::spot
