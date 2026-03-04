#include "SpotOrderScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../ui/MatrixTheme.h"

namespace tradeboy::spotOrder {

static const char* side_label(Side s) {
    return (s == Side::Buy) ? "BUY" : "SELL";
}

static double ceil_to_decimals(double v, int decimals) {
    if (!std::isfinite(v)) return 0.0;
    int d = std::max(0, std::min(10, decimals));
    const double p = std::pow(10.0, (double)d);
    // Protect against floating-point error at decimal boundaries (e.g. 0.35 becoming 0.35000000000004).
    const double eps = 1e-12;
    return std::ceil(v * p - eps) / p;
}

static double trunc_to_decimals_eps(double v, int decimals) {
    if (!std::isfinite(v)) return 0.0;
    int d = std::max(0, std::min(10, decimals));
    const double p = std::pow(10.0, (double)d);
    const double eps = 1e-12;
    return std::trunc((v + eps) * p) / p;
}

void SpotOrderState::open_with(const tradeboy::model::SpotRow& row, Side in_side, double in_max_possible) {
    side = in_side;
    sym = row.sym;
    price = row.price;
    price_decimals = row.price_decimals;
    size_decimals = row.size_decimals;
    
    tradeboy::ui::NumberInputConfig cfg;
    
    char title[64];
    std::snprintf(title, sizeof(title), "%s_%s", side_label(in_side), row.sym.c_str());
    cfg.title = std::string(title);
    
    cfg.title_color = (in_side == Side::Buy) ? MatrixTheme::TEXT : MatrixTheme::ALERT;
    
    if (in_side == Side::Buy) {
        cfg.min_value = 10.3;
    } else {
        double raw_min = (row.price > 0.0) ? (10.3 / row.price) : 0.0;
        cfg.min_value = ceil_to_decimals(raw_min, row.size_decimals);
    }
    if (in_side == Side::Sell) {
        cfg.max_value = std::max(0.0, trunc_to_decimals_eps(in_max_possible, row.size_decimals));
    } else {
        cfg.max_value = std::max(0.0, in_max_possible);
    }
    cfg.available_label = (in_side == Side::Buy) ? "USDC" : row.sym;
    cfg.available_decimals = (in_side == Side::Buy) ? 2 : row.size_decimals;
    cfg.allowed_decimals = (in_side == Side::Buy) ? 2 : cfg.available_decimals;
    
    char price_label[64];
    std::snprintf(price_label, sizeof(price_label), "PRICE: $%.*f", row.price_decimals, row.price);
    cfg.price_label = std::string(price_label);
    cfg.price = row.price;
    cfg.approx_divide = (in_side == Side::Buy);
    cfg.approx_label = (in_side == Side::Buy) ? row.sym : "USD";
    cfg.approx_decimals = (in_side == Side::Buy) ? row.size_decimals : 2;
    
    cfg.show_available_panel = true;
    
    input_state.open_with(cfg);
}

void SpotOrderState::sync_price(double new_price, int new_price_decimals) {
    if (!open()) return;
    if (new_price <= 0.0) return;
    new_price_decimals = std::max(0, std::min(10, new_price_decimals));
    if (std::fabs(new_price - price) < 0.0000001 && new_price_decimals == price_decimals) return;
    price = new_price;
    price_decimals = new_price_decimals;
    input_state.config.price = new_price;
    if (side == Side::Sell) {
        double raw_min = 10.3 / new_price;
        input_state.config.min_value = ceil_to_decimals(raw_min, size_decimals);
    }
    char price_label[64];
    std::snprintf(price_label, sizeof(price_label), "PRICE: $%.*f", price_decimals, new_price);
    input_state.config.price_label = std::string(price_label);
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
