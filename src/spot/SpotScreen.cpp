#include "SpotScreen.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "../app/App.h"
#include "../uiComponents/Primitives.h"
#include "../uiComponents/Button.h"
#include "../uiComponents/Fonts.h"
#include "../utils/Format.h"
#include "../spot/KLineChart.h"

namespace tradeboy::spot {

static std::vector<OHLC> make_demo_ohlc(int candle_count) {
    std::vector<OHLC> ohlc;
    ohlc.reserve((size_t)candle_count);

    uint32_t seed = 11u;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) & 0xFFFFu;
    };

    float cur = 68000.0f;
    for (int i = 0; i < candle_count; i++) {
        float o = cur;
        float hi = o + (float)(60 + (rnd() % 180));
        float lo = o - (float)(60 + (rnd() % 180));
        float c = lo + (float)(rnd() % (int)std::max(1.0f, hi - lo));
        cur = c;
        ohlc.push_back({o, hi, lo, c});
    }

    return ohlc;
}

void render_spot_screen(tradeboy::app::App& app) {
    ImFont* f = tradeboy::ui::fonts().large;
    if (f) ImGui::PushFont(f);

    const ImVec2 win_pos = ImGui::GetWindowPos();
    const ImVec2 win_size = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const float m = 10.0f;
    const float header_h = 56.0f;
    const float bottom_h = 66.0f;
    const float gap = 8.0f;
    const float panel_gap = 12.0f;
    const float left_w = 260.0f;
    const float radius = 14.0f;
    const float stroke_w = 2.0f;

    const ImU32 panel = IM_COL32(78, 77, 66, 255);
    const ImU32 panel2 = IM_COL32(92, 90, 78, 255);
    const ImU32 panel_pressed = IM_COL32(70, 68, 58, 255);
    const ImU32 stroke = IM_COL32(125, 123, 106, 255);
    const ImU32 text = IM_COL32(245, 245, 240, 255);
    const ImU32 muted = IM_COL32(200, 200, 185, 255);
    const ImU32 green = IM_COL32(110, 215, 140, 255);
    const ImU32 red = IM_COL32(235, 110, 110, 255);
    const ImU32 grid = IM_COL32(60, 60, 54, 255);

    tradeboy::ui::Rect hdr(win_pos.x + m, win_pos.y + m, win_pos.x + win_size.x - m, win_pos.y + m + header_h);
    tradeboy::ui::Rect bot(win_pos.x + m, win_pos.y + win_size.y - m - bottom_h, win_pos.x + win_size.x - m, win_pos.y + win_size.y - m);

    float mid_top = hdr.Max.y + gap;
    float mid_bottom = bot.Min.y - gap;

    tradeboy::ui::Rect lp(win_pos.x + m, mid_top, win_pos.x + m + left_w, mid_bottom);
    tradeboy::ui::Rect rp(lp.Max.x + panel_gap, mid_top, win_pos.x + win_size.x - m, mid_bottom);

    dl->AddRectFilled(hdr.Min, hdr.Max, panel, radius);
    dl->AddRect(hdr.Min, hdr.Max, stroke, radius, 0, stroke_w);

    dl->AddRectFilled(lp.Min, lp.Max, panel, radius);
    dl->AddRect(lp.Min, lp.Max, stroke, radius, 0, stroke_w);

    dl->AddRectFilled(rp.Min, rp.Max, panel, radius);
    dl->AddRect(rp.Min, rp.Max, stroke, radius, 0, stroke_w);

    dl->AddRectFilled(bot.Min, bot.Max, panel, radius);
    dl->AddRect(bot.Min, bot.Max, stroke, radius, 0, stroke_w);

    const char* pair = "BTC/USDC";
    const char* price = "68420.5";
    const char* change = "+1320.4 (+3.21%)";
    const ImU32 change_col = green;

    const char* tf = (app.tf_idx == 0) ? "24H" : (app.tf_idx == 1) ? "4H" : "1H";

    ImVec2 hdr_text_base(hdr.Min.x + 18.0f, hdr.Min.y + 14.0f);
    dl->AddText(hdr_text_base, text, pair);
    dl->AddText(ImVec2(hdr.Min.x + 180.0f, hdr_text_base.y), text, price);
    dl->AddText(ImVec2(hdr.Min.x + 310.0f, hdr_text_base.y), change_col, change);

    ImVec2 tf_sz = ImGui::CalcTextSize(tf);
    float x_r = 14.0f;
    ImVec2 x_center(hdr.Max.x - 18.0f - x_r, (hdr.Min.y + hdr.Max.y) * 0.5f);
    ImVec2 tf_pos(x_center.x - x_r - 10.0f - tf_sz.x, hdr_text_base.y);
    dl->AddText(tf_pos, text, tf);

    const bool x_pressed = (app.x_press_frames > 0);
    tradeboy::ui::draw_circle_button(dl, x_center, x_r, x_pressed ? panel_pressed : panel2, text, "X", x_pressed);

    const float row_h = 42.0f;
    const float pad_lr = 22.0f;
    float y = lp.Min.y + 16.0f;

    auto row_text_y = [&](float row_y) {
        ImVec2 ts = ImGui::CalcTextSize("Ag");
        return row_y + (row_h - ts.y) * 0.5f;
    };

    for (int i = 0; i < (int)app.spot_rows.size(); i++) {
        if (y + row_h > lp.Max.y - 10.0f) break;
        const bool selected = (i == app.spot_row_idx);
        const auto& r = app.spot_rows[(size_t)i];

        if (selected) {
            dl->AddRectFilled(ImVec2(lp.Min.x + 10.0f, y - 4.0f), ImVec2(lp.Max.x - 10.0f, y + row_h - 4.0f), panel2, 10.0f);
        }

        std::string p = tradeboy::utils::format_fixed_trunc_sig(r.price, 8, 8);
        const char* pct = (i % 3 == 1) ? "-1.05%" : "+3.21%";
        ImU32 pct_col = (pct[0] == '-') ? red : green;

        const float ty = row_text_y(y);

        dl->AddText(ImVec2(lp.Min.x + pad_lr, ty), text, r.sym.c_str());

        float price_cell_r = lp.Max.x - pad_lr - 78.0f;
        ImVec2 p_sz = ImGui::CalcTextSize(p.c_str());
        dl->AddText(ImVec2(price_cell_r - p_sz.x, ty), text, p.c_str());

        dl->AddText(ImVec2(price_cell_r + 8.0f, ty), pct_col, pct);

        y += row_h;
    }

    tradeboy::ui::Rect chart(rp.Min.x + 16.0f, rp.Min.y + 16.0f, rp.Max.x - 56.0f, rp.Max.y - 16.0f);

    // derive candle count from grid, same logic as before
    float cell_h = chart.h() / 5.0f;
    int num_v = (int)std::round(chart.w() / cell_h);
    num_v = std::max(5, std::min(10, num_v));
    const int candle_count = std::max(16, (num_v - 2) * 3);

    std::vector<OHLC> ohlc = make_demo_ohlc(candle_count);

    KLineStyle ks;
    ks.grid = grid;
    ks.green = green;
    ks.red = red;
    ks.muted = muted;
    render_kline(dl, chart, ohlc, 6, ks);

    const char* hold = "0.0500 BTC";
    const char* val = "$3421.55";
    const char* pnl = "+$65.2";

    ImVec2 bot_base(bot.Min.x + 16.0f, bot.Min.y + 22.0f);
    dl->AddText(bot_base, text, hold);
    dl->AddText(ImVec2(bot.Min.x + 210.0f, bot_base.y), text, val);
    dl->AddText(ImVec2(bot.Min.x + 360.0f, bot_base.y), green, pnl);

    float btn_w = 100.0f;
    float btn_h = 44.0f;
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
            buy_pressed ? panel_pressed : panel2,
            text,
            "Buy",
            true);
    } else {
        // normal: text-only, not bold
        ImVec2 ts = ImGui::CalcTextSize("Buy");
        dl->AddText(ImVec2(bx + (btn_w - ts.x) * 0.5f, by + (btn_h - ts.y) * 0.5f), text, "Buy");
    }

    float sx = bx + btn_w;
    if (sell_hover) {
        tradeboy::ui::draw_pill_button(
            dl,
            ImVec2(sx, by),
            ImVec2(sx + btn_w, by + btn_h),
            12.0f,
            sell_pressed ? panel_pressed : panel2,
            text,
            "Sell",
            true);
    } else {
        ImVec2 ts = ImGui::CalcTextSize("Sell");
        dl->AddText(ImVec2(sx + (btn_w - ts.x) * 0.5f, by + (btn_h - ts.y) * 0.5f), text, "Sell");
    }

    tradeboy::app::App::dec_frame_counter(app.x_press_frames);
    tradeboy::app::App::dec_frame_counter(app.buy_press_frames);
    tradeboy::app::App::dec_frame_counter(app.sell_press_frames);

    if (f) ImGui::PopFont();
}

} // namespace tradeboy::spot
