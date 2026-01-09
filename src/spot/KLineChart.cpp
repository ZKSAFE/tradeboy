#include "KLineChart.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../utils/Math.h"

namespace tradeboy::spot {

void render_kline(ImDrawList* dl, const tradeboy::ui::Rect& chart, const std::vector<OHLC>& ohlc, int num_h, const KLineStyle& style) {
    if (!dl) return;
    if (ohlc.empty()) return;

    const float cw = chart.w();
    const float ch = chart.h();

    // horizontal grid
    for (int j = 0; j < num_h; j++) {
        float yy = chart.Min.y + (ch * (float)j / (float)(num_h - 1));
        dl->AddLine(ImVec2(chart.Min.x, yy), ImVec2(chart.Max.x, yy), style.grid, 1.0f);
    }

    float cell_h = ch / (float)(num_h - 1);
    int num_v = (int)std::round(cw / cell_h);
    num_v = std::max(5, std::min(10, num_v));
    for (int j = 0; j < num_v; j++) {
        float xx = chart.Min.x + (cw * (float)j / (float)(num_v - 1));
        dl->AddLine(ImVec2(xx, chart.Min.y), ImVec2(xx, chart.Max.y), style.grid, 1.0f);
    }

    float max_h = ohlc[0].h;
    float min_l = ohlc[0].l;
    int idx_h = 0;
    int idx_l = 0;
    for (int i = 0; i < (int)ohlc.size(); i++) {
        max_h = std::max(max_h, ohlc[(size_t)i].h);
        min_l = std::min(min_l, ohlc[(size_t)i].l);
    }
    for (int i = 0; i < (int)ohlc.size(); i++) {
        if (ohlc[(size_t)i].h >= max_h - 1e-6f) idx_h = i;
        if (ohlc[(size_t)i].l <= min_l + 1e-6f) idx_l = i;
    }

    float p_pad = (max_h > min_l) ? ((max_h - min_l) * 0.12f) : 1.0f;
    float pmax = max_h + p_pad;
    float pmin = min_l - p_pad;

    auto py = [&](float v) {
        float t = (pmax != pmin) ? ((v - pmin) / (pmax - pmin)) : 0.5f;
        t = tradeboy::utils::clampf(t, 0.0f, 1.0f);
        return chart.Max.y - t * ch;
    };

    float side_pad = std::max(8.0f, cw * 0.04f);
    float usable_w = std::max(1.0f, cw - side_pad * 2.0f);
    float step = usable_w / (float)((int)ohlc.size() + 1);
    float candle_w = std::max(6.0f, step * 0.65f);

    for (int i = 0; i < (int)ohlc.size(); i++) {
        const auto& v = ohlc[(size_t)i];
        float x = chart.Min.x + side_pad + step * (float)(i + 1);
        ImU32 col = (v.c >= v.o) ? style.green : style.red;
        float y_hi = tradeboy::utils::clampf(py(v.h), chart.Min.y + 2.0f, chart.Max.y - 2.0f);
        float y_lo = tradeboy::utils::clampf(py(v.l), chart.Min.y + 2.0f, chart.Max.y - 2.0f);
        dl->AddLine(ImVec2(x, y_hi), ImVec2(x, y_lo), col, 2.0f);
        float y_o = py(v.o);
        float y_c = py(v.c);
        float top = std::max(chart.Min.y + 2.0f, std::min(y_o, y_c));
        float boty = std::min(chart.Max.y - 2.0f, std::max(y_o, y_c));
        if (boty - top < 4.0f) boty = std::min(chart.Max.y - 2.0f, top + 4.0f);
        dl->AddRectFilled(ImVec2(x - candle_w * 0.5f, top), ImVec2(x + candle_w * 0.5f, boty), col);
    }

    auto draw_hilo = [&](int idx, float price_v, bool is_high) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f", price_v);
        ImVec2 ts = ImGui::CalcTextSize(buf);
        float x = chart.Min.x + side_pad + step * (float)(idx + 1);
        float y = py(price_v);
        float pad = 4.0f;
        float tx = tradeboy::utils::clampf(x - ts.x * 0.5f, chart.Min.x + pad, chart.Max.x - ts.x - pad);
        float ty = is_high ? (y - ts.y - 4.0f) : (y + 4.0f);
        ty = tradeboy::utils::clampf(ty, chart.Min.y + pad, chart.Max.y - ts.y - pad);
        dl->AddText(ImVec2(tx, ty), style.muted, buf);
    };

    draw_hilo(idx_h, max_h, true);
    draw_hilo(idx_l, min_l, false);

    for (int j = 0; j < num_h; j++) {
        float yy = chart.Min.y + (ch * (float)j / (float)(num_h - 1));
        float t = (chart.Max.y - yy) / ch;
        float pv = pmin + t * (pmax - pmin);
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f", pv);
        dl->AddText(ImVec2(chart.Max.x + 8.0f, yy - 8.0f), style.muted, buf);
    }
}

} // namespace tradeboy::spot
