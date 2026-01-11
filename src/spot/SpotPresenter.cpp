#include "SpotPresenter.h"

#include <algorithm>
#include <cstdio>

#include "../uiComponents/Theme.h"
#include "../utils/Format.h"

namespace tradeboy::spot {

SpotViewModel build_spot_view_model(const tradeboy::model::TradeModelSnapshot& snap, const SpotUiState& ui) {
    SpotViewModel vm;

    const auto& spot_rows = snap.spot_rows;
    const auto& kline_data = snap.kline_data;
    int spot_row_idx = snap.spot_row_idx;
    int tf_idx = snap.tf_idx;

    vm.selected_row_idx = spot_row_idx;
    vm.kline_data = kline_data;

    const auto& c = tradeboy::ui::colors();

    const char* tf = (tf_idx == 0) ? "24H" : (tf_idx == 1) ? "4H" : "1H";
    vm.header.tf = tf;
    vm.header.x_pressed = (ui.x_press_frames > 0);

    std::string sym;
    double cur_price = 0.0;
    double prev_price = 0.0;
    double bal = 0.0;

    if (!spot_rows.empty() && spot_row_idx >= 0 && spot_row_idx < (int)spot_rows.size()) {
        const auto& r = spot_rows[(size_t)spot_row_idx];
        sym = r.sym;
        cur_price = r.price;
        prev_price = r.prev_price;
        bal = r.balance;
    }

    if (!sym.empty()) vm.header.pair = sym + "/USDC";
    else vm.header.pair = "--/USDC";

    if (cur_price > 0.0) vm.header.price = tradeboy::utils::format_fixed_trunc_sig(cur_price, 8, 8);
    else vm.header.price = "--";

    double tf_open = 0.0;
    if (!kline_data.empty()) tf_open = kline_data.front().o;

    if (tf_open > 0.0 && cur_price > 0.0) {
        double delta = cur_price - tf_open;
        double pct = (delta / tf_open) * 100.0;
        vm.header.change_col = (delta >= 0.0) ? c.green : c.red;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%+.4f (%+.2f%%)", delta, pct);
        vm.header.change = buf;

        double pnl_v = delta * bal;
        vm.bottom.pnl_col = (pnl_v >= 0.0) ? c.green : c.red;
        char buf3[64];
        std::snprintf(buf3, sizeof(buf3), "%+.2f", pnl_v);
        vm.bottom.pnl = buf3;
    } else {
        vm.header.change = "--";
        vm.header.change_col = c.muted;
        vm.bottom.pnl = "--";
        vm.bottom.pnl_col = c.muted;
    }

    if (!sym.empty()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.4f %s", bal, sym.c_str());
        vm.bottom.hold = buf;

        double v = bal * cur_price;
        char buf2[64];
        std::snprintf(buf2, sizeof(buf2), "$%.2f", v);
        vm.bottom.val = buf2;
    } else {
        vm.bottom.hold = "--";
        vm.bottom.val = "--";
    }

    vm.rows.reserve(spot_rows.size());
    for (const auto& r : spot_rows) {
        SpotRowVM rv;
        rv.sym = r.sym;
        rv.price = tradeboy::utils::format_fixed_trunc_sig(r.price, 8, 8);
        const bool down = (r.price < r.prev_price);
        rv.price_col = down ? c.red : c.green;
        vm.rows.push_back(std::move(rv));
    }

    vm.bottom.buy_hover = (!ui.spot_action_focus) || (ui.spot_action_focus && ui.spot_action_idx == 0);
    vm.bottom.sell_hover = (ui.spot_action_focus && ui.spot_action_idx == 1);
    vm.bottom.buy_pressed = (ui.buy_press_frames > 0);
    vm.bottom.sell_pressed = (ui.sell_press_frames > 0);

    (void)prev_price;
    return vm;
}

} // namespace tradeboy::spot
