#pragma once

#include <string>

#include "imgui.h"

#include "../app/Input.h"
#include "../model/TradeModel.h"

namespace tradeboy::spotOrder {

enum class Side {
    Buy = 0,
    Sell = 1,
};

struct SpotOrderState {
    bool open = false;

    Side side = Side::Buy;
    std::string sym;
    double price = 0.0;
    double max_possible = 0.0;

    std::string input = "0";

    int grid_r = 0;
    int grid_c = 1;

    int footer_idx = -1;

    void open_with(const tradeboy::model::SpotRow& row, Side in_side, double in_max_possible);
    void close();
};

bool handle_input(SpotOrderState& st, const tradeboy::app::InputState& in, const tradeboy::app::EdgeState& edges);

void render(SpotOrderState& st, ImFont* font_bold);

} // namespace tradeboy::spotOrder
