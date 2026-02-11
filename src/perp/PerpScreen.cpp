#include "perp/PerpScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

#include "ui/MatrixTheme.h"
#include "utils/Flash.h"
#include "utils/Typewriter.h"

namespace tradeboy::perp {

static std::string format_fixed_round(double v, int decimals) {
    if (!std::isfinite(v)) return std::string("0");
    int d = std::max(0, std::min(10, decimals));
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(d);
    ss << v;
    return ss.str();
}

static int price_decimals_for(double v) {
    if (!std::isfinite(v) || v <= 0.0) return 2;
    if (v >= 1000.0) return 2;
    if (v >= 1.0) return 4;
    return 6;
}

static std::string format_leverage(double v) {
    if (!std::isfinite(v) || v <= 0.0) return "1x";
    double rv = std::round(v);
    char buf[32];
    if (std::fabs(v - rv) < 0.05) {
        std::snprintf(buf, sizeof(buf), "%dx", (int)rv);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1fx", v);
    }
    return buf;
}

void render_perp_screen(const std::vector<tradeboy::model::PerpRow>& rows,
                        int page_start_idx,
                        int selected_row_idx,
                        int action_idx,
                        bool primary_pressed,
                        bool close_pressed,
                        ImFont* font_bold) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (!dl) return;

    if (size.x <= 1.0f || size.y <= 1.0f) return;
    if (rows.empty()) {
        const float padding = 16.0f;
        const float headerH = 54.0f;

        float left = p.x + padding;
        float right = p.x + size.x - padding;
        float y = p.y + padding;
        y += headerH;
        dl->AddLine(ImVec2(left, y - 16), ImVec2(right, y - 16), MatrixTheme::DIM, 2.0f);

        const char* msg = "LOADING DATA...";
        ImVec2 ts = font_bold ? font_bold->CalcTextSizeA(28.0f, FLT_MAX, 0.0f, msg) : ImGui::CalcTextSize(msg);
        float cx = p.x + (size.x - ts.x) * 0.5f;
        float cy = p.y + (size.y - ts.y) * 0.5f;
        if (font_bold) {
            dl->AddText(font_bold, 28.0f, ImVec2(cx, cy), MatrixTheme::DIM, msg);
        } else {
            dl->AddText(ImVec2(cx, cy), MatrixTheme::DIM, msg);
        }
        return;
    }

    page_start_idx = std::max(0, std::min((int)rows.size() - 1, page_start_idx));
    selected_row_idx = std::max(0, std::min((int)rows.size() - 1, selected_row_idx));
    action_idx = std::max(0, std::min(1, action_idx));

    const float padding = 16.0f;
    const float headerH = 54.0f;
    const float footerH = 55.0f;
    const float tableHeaderH = 30.0f;
    const int targetRows = 7;

    float y = p.y + padding;
    float w = size.x - 2 * padding;
    float left = p.x + padding;
    float right = p.x + size.x - padding;

    y += headerH;
    dl->AddLine(ImVec2(left, y - 16), ImVec2(right, y - 16), MatrixTheme::DIM, 2.0f);

    // Table headers
    {
        float col1 = left;
        float col2 = left + w * 0.38f;
        float col3 = right - 180;
        float col4 = right;

        dl->AddText(ImVec2(col1 + 30, y), MatrixTheme::DIM, "CODE");

        const char* h2 = "MARGIN";
        ImVec2 sz2 = ImGui::CalcTextSize(h2);
        dl->AddText(ImVec2(col2 - sz2.x * 0.5f, y), MatrixTheme::DIM, h2);

        const char* h3 = "PRICE";
        ImVec2 sz3 = ImGui::CalcTextSize(h3);
        dl->AddText(ImVec2(col3 - sz3.x, y), MatrixTheme::DIM, h3);

        const char* h4 = "LIQUIDATE";
        ImVec2 sz4 = ImGui::CalcTextSize(h4);
        dl->AddText(ImVec2(col4 - sz4.x, y), MatrixTheme::DIM, h4);

        y += tableHeaderH;
    }

    // List
    {
        float listH = size.y - padding - footerH - y + p.y;
        int startIdx = page_start_idx;
        int maxRows = std::max(1, targetRows);
        float rowH = std::max(1.0f, std::ceil(listH / (float)maxRows));
        if (startIdx + maxRows > (int)rows.size()) {
            startIdx = std::max(0, (int)rows.size() - maxRows);
        }

        float textH = ImGui::CalcTextSize("A").y;

        for (int i = startIdx; i < (int)rows.size() && (i - startIdx) < maxRows; ++i) {
            const auto& row = rows[(size_t)i];
            bool isSelected = (i == selected_row_idx);
            float rowY = y + (i - startIdx) * rowH;

            float rowContentH = rowH - 4.0f;
            float textY = rowY + (rowContentH - textH) * 0.5f;

            if (isSelected) {
                dl->AddRectFilled(ImVec2(left, rowY), ImVec2(right, rowY + rowContentH), MatrixTheme::TEXT, 0.0f);
                bool cursorOn = tradeboy::utils::blink_on_time(ImGui::GetTime(), 3.0);
                if (cursorOn) {
                    float cursorW = 10.0f;
                    float cursorPadY = 2.0f;
                    float cursorH = std::max(1.0f, textH - cursorPadY * 2.0f);
                    dl->AddRectFilled(
                        ImVec2(left + 8, textY + cursorPadY),
                        ImVec2(left + 8 + cursorW, textY + cursorPadY + cursorH),
                        MatrixTheme::BLACK,
                        0.0f);
                }
            }

            ImU32 textCol = isSelected ? MatrixTheme::BLACK : MatrixTheme::TEXT;

            float col1 = left;
            float col2 = left + w * 0.38f;
            float col3 = right - 180;
            float col4 = right;

            const char side = row.is_long ? 'L' : 'S';
            std::string code = row.coin + " " + side + format_leverage(row.leverage);
            dl->AddText(ImVec2(col1 + 30, textY), textCol, code.c_str());

            if (row.margin_used > 0.0) {
                std::string marginStr = format_fixed_round(row.margin_used, 2);
                ImVec2 sz = ImGui::CalcTextSize(marginStr.c_str());
                dl->AddText(ImVec2(col2 - sz.x * 0.5f, textY), textCol, marginStr.c_str());
            }

            int px_decimals = price_decimals_for(row.price);
            std::string priceStr = (row.price > 0.0) ? format_fixed_round(row.price, px_decimals) : std::string("--");
            ImVec2 szP = ImGui::CalcTextSize(priceStr.c_str());
            dl->AddText(ImVec2(col3 - szP.x, textY), textCol, priceStr.c_str());

            if (row.margin_used > 0.0 && row.liquidation_px > 0.0) {
                std::string liqStr = format_fixed_round(row.liquidation_px, px_decimals);
                ImVec2 szL = ImGui::CalcTextSize(liqStr.c_str());
                dl->AddText(ImVec2(col4 - szL.x, textY), textCol, liqStr.c_str());
            }
        }
    }

    // Footer
    {
        float footerTop = p.y + size.y - footerH;
        dl->AddLine(ImVec2(left, footerTop), ImVec2(right, footerTop), MatrixTheme::DIM, 2.0f);

        const auto& sel = rows[(size_t)selected_row_idx];
        char body[128];
        if (sel.margin_used > 0.0) {
            std::snprintf(body, sizeof(body), "It worth $%.2f", sel.margin_used);
        } else {
            std::snprintf(body, sizeof(body), "No %s", sel.coin.c_str());
        }

        static tradeboy::utils::TypewriterState tw;
        std::string shown_text = tradeboy::utils::typewriter_shown(tw, body, ImGui::GetTime(), 35.0);

        dl->AddText(ImVec2(left, footerTop + 20), MatrixTheme::TEXT, "> ");
        dl->AddText(ImVec2(left + 18, footerTop + 20), MatrixTheme::TEXT, shown_text.c_str());

        float btnW = 110.0f;
        float btnH = 40.0f;
        float btnY = footerTop + 15;
        float closeX = right - btnW;
        float primaryX = closeX - btnW - 20;

        bool primaryFocus = (action_idx == 0);
        bool closeFocus = (action_idx == 1);

        const char* primaryLabel = sel.is_long ? "LONG" : "SHORT";

        float btnFontSize = 20.0f;

        ImU32 primaryBg = primaryFocus ? MatrixTheme::TEXT : IM_COL32(0,0,0,0);
        ImU32 primaryFg = primaryFocus ? MatrixTheme::BLACK : MatrixTheme::DIM;
        ImU32 primaryBorder = primaryFocus ? MatrixTheme::TEXT : MatrixTheme::DIM;
        if (primary_pressed && primaryFocus) {
            primaryBg = IM_COL32(0,0,0,0);
            primaryFg = MatrixTheme::TEXT;
            primaryBorder = MatrixTheme::TEXT;
        }
        dl->AddRectFilled(ImVec2(primaryX, btnY), ImVec2(primaryX + btnW, btnY + btnH), primaryBg, 0.0f);
        dl->AddRect(ImVec2(primaryX, btnY), ImVec2(primaryX + btnW, btnY + btnH), primaryBorder, 0.0f, 0, 2.0f);
        ImVec2 pSz = font_bold ? font_bold->CalcTextSizeA(btnFontSize, FLT_MAX, 0.0f, primaryLabel) : ImGui::CalcTextSize(primaryLabel);
        if (font_bold) {
            dl->AddText(font_bold, btnFontSize, ImVec2(primaryX + (btnW - pSz.x) * 0.5f, btnY + (btnH - pSz.y) * 0.5f), primaryFg, primaryLabel);
        } else {
            dl->AddText(ImVec2(primaryX + (btnW - pSz.x) * 0.5f, btnY + (btnH - pSz.y) * 0.5f), primaryFg, primaryLabel);
        }

        ImU32 closeBg = closeFocus ? MatrixTheme::TEXT : IM_COL32(0,0,0,0);
        ImU32 closeFg = closeFocus ? MatrixTheme::BLACK : MatrixTheme::DIM;
        ImU32 closeBorder = closeFocus ? MatrixTheme::TEXT : MatrixTheme::DIM;
        if (close_pressed && closeFocus) {
            closeBg = IM_COL32(0,0,0,0);
            closeFg = MatrixTheme::TEXT;
            closeBorder = MatrixTheme::TEXT;
        }
        dl->AddRectFilled(ImVec2(closeX, btnY), ImVec2(closeX + btnW, btnY + btnH), closeBg, 0.0f);
        dl->AddRect(ImVec2(closeX, btnY), ImVec2(closeX + btnW, btnY + btnH), closeBorder, 0.0f, 0, 2.0f);
        ImVec2 cSz = font_bold ? font_bold->CalcTextSizeA(btnFontSize, FLT_MAX, 0.0f, "CLOSE") : ImGui::CalcTextSize("CLOSE");
        if (font_bold) {
            dl->AddText(font_bold, btnFontSize, ImVec2(closeX + (btnW - cSz.x) * 0.5f, btnY + (btnH - cSz.y) * 0.5f), closeFg, "CLOSE");
        } else {
            dl->AddText(ImVec2(closeX + (btnW - cSz.x) * 0.5f, btnY + (btnH - cSz.y) * 0.5f), closeFg, "CLOSE");
        }
    }
}

} // namespace tradeboy::perp
