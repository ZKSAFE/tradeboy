#include "SpotOrderScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../ui/MatrixTheme.h"

namespace tradeboy::spotOrder {

static const char* side_label(Side s) {
    return (s == Side::Buy) ? "BUY" : "SELL";
}

void SpotOrderState::open_with(const tradeboy::model::SpotRow& row, Side in_side, double in_max_possible) {
    side = in_side;
    sym = row.sym;
    price = row.price;
    
    tradeboy::ui::NumberInputConfig cfg;
    
    char title[64];
    std::snprintf(title, sizeof(title), "%s_%s", side_label(in_side), row.sym.c_str());
    cfg.title = title;
    
    cfg.title_color = (in_side == Side::Buy) ? MatrixTheme::TEXT : MatrixTheme::ALERT;
    
    cfg.min_value = 0.0;
    cfg.max_value = std::max(0.0, in_max_possible);
    cfg.available_label = (in_side == Side::Buy) ? "USDC" : row.sym;
    cfg.available_decimals = (in_side == Side::Buy) ? 2 : row.price_decimals;
    cfg.allowed_decimals = (in_side == Side::Buy) ? 2 : cfg.available_decimals;
    
    char price_label[64];
    std::snprintf(price_label, sizeof(price_label), "PRICE: $%.2f", row.price);
    cfg.price_label = price_label;
    cfg.price = row.price;
    cfg.approx_divide = (in_side == Side::Buy);
    cfg.approx_label = (in_side == Side::Buy) ? row.sym : "USD";
    
    cfg.show_available_panel = true;
    
    input_state.open_with(cfg);
}

void SpotOrderState::sync_price(double new_price) {
    if (!open()) return;
    if (new_price <= 0.0 || std::fabs(new_price - price) < 0.0000001) return;
    price = new_price;
    input_state.config.price = new_price;
    char price_label[64];
    std::snprintf(price_label, sizeof(price_label), "PRICE: $%.2f", new_price);
    input_state.config.price_label = price_label;
}

void SpotOrderState::close() {
    input_state.close();
}

bool handle_input(SpotOrderState& st, const tradeboy::app::InputState& in, const tradeboy::app::EdgeState& edges) {
    if (!st.open()) return false;
    return tradeboy::ui::handle_input(st.input_state, in, edges);
}

void render(SpotOrderState& st, ImFont* font_bold) {
    if (!st.open()) return;
    tradeboy::ui::render(st.input_state, font_bold);
}

} // namespace tradeboy::spotOrder
