/**
 * @file SpotOrderScreen.h
 * @brief Spot order entry using NumberInputModal.
 * 
 * This is a thin wrapper around NumberInputModal for spot trading.
 * The actual input UI is provided by NumberInputModal.
 */
#pragma once

#include <string>

#include "imgui.h"

#include "../app/Input.h"
#include "../model/TradeModel.h"
#include "../ui/NumberInputModal.h"

namespace tradeboy::spotOrder {

enum class Side {
    Buy = 0,
    Sell = 1,
};

struct SpotOrderState {
    tradeboy::ui::NumberInputState input_state;
    
    Side side = Side::Buy;
    std::string sym;
    double price = 0.0;
    
    bool open() const { return input_state.open; }
    
    void open_with(const tradeboy::model::SpotRow& row, Side in_side, double in_max_possible);
    void close();
    
    tradeboy::ui::NumberInputResult get_result() const { return input_state.result; }
    double get_result_value() const { return input_state.result_value; }
    void clear_result() { input_state.result = tradeboy::ui::NumberInputResult::None; }
};

bool handle_input(SpotOrderState& st, const tradeboy::app::InputState& in, const tradeboy::app::EdgeState& edges);

void render(SpotOrderState& st, ImFont* font_bold);

} // namespace tradeboy::spotOrder
