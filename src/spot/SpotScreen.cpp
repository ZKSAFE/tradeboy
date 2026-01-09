#include "SpotScreen.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../app/App.h"
#include "../uiComponents/Primitives.h"
#include "../uiComponents/Button.h"
#include "../uiComponents/Theme.h"
#include "../uiComponents/Fonts.h"
#include "../utils/Format.h"
#include "../spot/KLineChart.h"

namespace tradeboy::spot {

void render_spot_screen(tradeboy::app::App& app) {
    ImFont* f_body = tradeboy::ui::fonts().body;
    ImFont* f_small = tradeboy::ui::fonts().small;
    if (f_body) ImGui::PushFont(f_body);

    const ImVec2 win_pos = ImGui::GetWindowPos();
    const ImVec2 win_size = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float m = 10.0f;
    const float header_h = 64.0f;
    const float bottom_h = 64.0f;
    const float gap = 10.0f;
    const float panel_gap = 10.0f;
    const float left_w = 230.0f;
    const float radius = 14.0f;
    const float stroke_w = 2.0f;

    const auto& c = tradeboy::ui::colors();

    tradeboy::ui::Rect hdr(win_pos.x + m, win_pos.y + m, win_pos.x + win_size.x - m, win_pos.y + m + header_h);
    tradeboy::ui::Rect bot(win_pos.x + m, win_pos.y + win_size.y - m - bottom_h, win_pos.x + win_size.x - m, win_pos.y + win_size.y - m);

    float mid_top = hdr.Max.y + gap;
    float mid_bottom = bot.Min.y - gap;

    tradeboy::ui::Rect lp(win_pos.x + m, mid_top, win_pos.x + m + left_w, mid_bottom);
    tradeboy::ui::Rect rp(lp.Max.x + panel_gap, mid_top, win_pos.x + win_size.x - m, mid_bottom);

    dl->AddRectFilled(hdr.Min, hdr.Max, c.panel, radius);
    dl->AddRect(hdr.Min, hdr.Max, c.stroke, radius, 0, stroke_w);

    dl->AddRectFilled(lp.Min, lp.Max, c.panel, radius);
    dl->AddRect(lp.Min, lp.Max, c.stroke, radius, 0, stroke_w);

    dl->AddRectFilled(rp.Min, rp.Max, c.panel, radius);
    dl->AddRect(rp.Min, rp.Max, c.stroke, radius, 0, stroke_w);

    dl->AddRectFilled(bot.Min, bot.Max, c.panel, radius);
    dl->AddRect(bot.Min, bot.Max, c.stroke, radius, 0, stroke_w);

    const char* pair = "BTC/USDC";
    const char* price = "68420.5";
    const char* change = "+1320.4 (+3.21%)";
    const ImU32 change_col = c.green;

    const char* tf = (app.tf_idx == 0) ? "24H" : (app.tf_idx == 1) ? "4H" : "1H";

    ImVec2 hdr_line = ImGui::CalcTextSize("Ag");
    float hdr_y = hdr.Min.y + (header_h - hdr_line.y) * 0.5f;
    ImVec2 hdr_text_base(hdr.Min.x + 18.0f, hdr_y);
    dl->AddText(hdr_text_base, c.text, pair);
    ImVec2 tf_sz = ImGui::CalcTextSize(tf);
    float x_r = 18.0f;
    ImVec2 x_center(hdr.Max.x - 20.0f - x_r, (hdr.Min.y + hdr.Max.y) * 0.5f + 2.0f);
    ImVec2 tf_pos(x_center.x - x_r - 16.0f - tf_sz.x, hdr_text_base.y);

    // Layout price/change from the right side, but keep clear of the TF label.
    const float hdr_right_limit = tf_pos.x - 28.0f;
    const float hdr_col_gap = 18.0f;
    ImVec2 change_sz = ImGui::CalcTextSize(change);
    ImVec2 price_sz = ImGui::CalcTextSize(price);
    float change_x = hdr_right_limit - change_sz.x;
    float price_x = change_x - hdr_col_gap - price_sz.x;
    dl->AddText(ImVec2(price_x, hdr_text_base.y), c.text, price);
    dl->AddText(ImVec2(change_x, hdr_text_base.y), change_col, change);

    dl->AddText(tf_pos, c.text, tf);

    const bool x_pressed = (app.x_press_frames > 0);
    tradeboy::ui::draw_circle_button(dl, x_center, x_r, x_pressed ? c.panel_pressed : c.panel2, c.text, "X", x_pressed);

    const float row_h = 50.0f;
    const float pad_lr = 22.0f;
    float y = lp.Min.y + 16.0f;

    auto row_text_y = [&](float row_y) {
        ImVec2 ts = ImGui::CalcTextSize("Ag");
        return row_y + (row_h - ts.y) * 0.5f - 3.0f;
    };

    for (int i = 0; i < (int)app.spot_rows.size(); i++) {
        if (y + row_h > lp.Max.y - 10.0f) break;
        const bool selected = (i == app.spot_row_idx);
        const auto& r = app.spot_rows[(size_t)i];

        if (selected) {
            dl->AddRectFilled(ImVec2(lp.Min.x + 10.0f, y), ImVec2(lp.Max.x - 10.0f, y + row_h), c.panel2, 10.0f);
        }

        std::string p = tradeboy::utils::format_fixed_trunc_sig(r.price, 8, 8);
        const bool down = (i % 3 == 1);
        ImU32 price_col = down ? c.red : c.green;

        const float ty = row_text_y(y);

        dl->AddText(ImVec2(lp.Min.x + pad_lr, ty), c.text, r.sym.c_str());

        float price_cell_r = lp.Max.x - pad_lr;
        ImVec2 p_sz = ImGui::CalcTextSize(p.c_str());
        dl->AddText(ImVec2(price_cell_r - p_sz.x, ty), price_col, p.c_str());

        y += row_h;
    }

    tradeboy::ui::Rect chart(rp.Min.x + 16.0f, rp.Min.y + 16.0f, rp.Max.x - 56.0f, rp.Max.y - 16.0f);

    // derive candle count from grid, same logic as before
    float cell_h = chart.h() / 5.0f;
    int num_v = (int)std::round(chart.w() / cell_h);
    num_v = std::max(5, std::min(10, num_v));
    // const int candle_count = std::max(16, (num_v - 2) * 3); // Unused now

    KLineStyle ks;
    ks.grid = c.grid;
    ks.green = c.green;
    ks.red = c.red;
    ks.muted = c.muted;
    if (f_small) ImGui::PushFont(f_small);
    render_kline(dl, chart, app.kline_data, 6, ks);
    if (f_small) ImGui::PopFont();

    const char* hold = "0.0500 BTC";
    const char* val = "$3421.55";
    const char* pnl = "+$65.2";

    ImVec2 bot_line = ImGui::CalcTextSize("Ag");
    float bot_y = bot.Min.y + (bottom_h - bot_line.y) * 0.5f;
    ImVec2 bot_base(bot.Min.x + 16.0f, bot_y);
    dl->AddText(bot_base, c.text, hold);
    dl->AddText(ImVec2(bot.Min.x + 210.0f, bot_base.y), c.text, val);
    dl->AddText(ImVec2(bot.Min.x + 360.0f, bot_base.y), c.green, pnl);

    float btn_w = 110.0f;
    float btn_h = 56.0f;
    float bx = bot.Max.x - 16.0f - btn_w * 2.0f;
    float by = (bot.Min.y + bot.Max.y) * 0.5f - btn_h * 0.5f;

    const bool buy_hover = (!app.spot_action_focus) || (app.spot_action_focus && app.spot_action_idx == 0);
    const bool sell_hover = (app.spot_action_focus && app.spot_action_idx == 1);
    const bool buy_pressed = (app.buy_press_frames > 0);
    const bool sell_pressed = (app.sell_press_frames > 0);

    if (buy_hover) {
        tradeboy::ui::draw_pill_button(
            dl,
            ImVec2(bx, by),
            ImVec2(bx + btn_w, by + btn_h),
            12.0f,
            buy_pressed ? c.panel_pressed : c.panel2,
            c.text,
            "Buy",
            true);
    } else {
        // normal: text-only, not bold
        ImVec2 ts = ImGui::CalcTextSize("Buy");
        dl->AddText(ImVec2(bx + (btn_w - ts.x) * 0.5f, by + (btn_h - ts.y) * 0.5f - 2.0f), c.text, "Buy");
    }

    float sx = bx + btn_w;
    if (sell_hover) {
        tradeboy::ui::draw_pill_button(
            dl,
            ImVec2(sx, by),
            ImVec2(sx + btn_w, by + btn_h),
            12.0f,
            sell_pressed ? c.panel_pressed : c.panel2,
            c.text,
            "Sell",
            true);
    } else {
        ImVec2 ts = ImGui::CalcTextSize("Sell");
        dl->AddText(ImVec2(sx + (btn_w - ts.x) * 0.5f, by + (btn_h - ts.y) * 0.5f - 2.0f), c.text, "Sell");
    }

    tradeboy::app::App::dec_frame_counter(app.x_press_frames);
    tradeboy::app::App::dec_frame_counter(app.buy_press_frames);
    tradeboy::app::App::dec_frame_counter(app.sell_press_frames);

    if (f_body) ImGui::PopFont();
}

} // namespace tradeboy::spot
