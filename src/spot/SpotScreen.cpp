#include "SpotScreen.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace tradeboy::spot {

struct Coin {
    std::string id;
    std::string symbol;
    std::string name;
    double price;
    double change24h;
    double holdings;
};

static std::vector<Coin> MOCK_COINS = {
    {"1", "BTC", "Bitcoin", 64230.50, 2.4, 0.15},
    {"2", "ETH", "Ethereum", 3450.12, -1.2, 2.5},
    {"3", "SOL", "Solana", 145.60, 5.8, 100.0},
    {"4", "DOGE", "Dogecoin", 0.12, 0.5, 5000.0},
    {"5", "ADA", "Cardano", 0.45, -3.4, 0.0},
    {"6", "XRP", "Ripple", 0.60, 1.1, 0.0},
    {"7", "DOT", "Polkadot", 7.20, -0.8, 0.0},
};

namespace MatrixTheme {
static const ImU32 BG = IM_COL32(0, 0, 0, 255);
static const ImU32 TEXT = IM_COL32(0, 255, 65, 255);
static const ImU32 DIM = IM_COL32(0, 143, 17, 255);
static const ImU32 DARK = IM_COL32(0, 59, 0, 255);
static const ImU32 ALERT = IM_COL32(255, 0, 85, 255);
static const ImU32 BLACK = IM_COL32(0, 0, 0, 255);
}

static void DrawGlowText(ImDrawList* dl, const ImVec2& pos, const char* text, ImU32 color) {
    if (!dl || !text) return;
    ImU32 glowCol = (color & 0x00FFFFFF) | 0x40000000;
    dl->AddText(ImVec2(pos.x + 1, pos.y + 1), glowCol, text);
    dl->AddText(ImVec2(pos.x - 1, pos.y - 1), glowCol, text);
    dl->AddText(pos, color, text);
}

void render_spot_screen(int selected_row_idx, int action_idx, bool buy_pressed, bool sell_pressed) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();

    if (size.x <= 1.0f || size.y <= 1.0f) return;
    if (!dl) return;
    if (MOCK_COINS.empty()) return;

    selected_row_idx = std::max(0, std::min((int)MOCK_COINS.size() - 1, selected_row_idx));
    action_idx = std::max(0, std::min(1, action_idx));

    // Background grid
    const float gridStep = 40.0f;
    ImU32 gridCol = IM_COL32(0, 255, 65, 20);
    for (float x = 0; x < size.x; x += gridStep)
        dl->AddLine(ImVec2(p.x + x, p.y), ImVec2(p.x + x, p.y + size.y), gridCol);
    for (float y = 0; y < size.y; y += gridStep)
        dl->AddLine(ImVec2(p.x, p.y + y), ImVec2(p.x + size.x, p.y + y), gridCol);

    const float padding = 16.0f;
    const float headerH = 60.0f;
    const float footerH = 70.0f;
    const float tableHeaderH = 30.0f;
    const float rowH = 44.0f;

    float y = p.y + padding;
    float w = size.x - 2 * padding;
    float left = p.x + padding;
    float right = p.x + size.x - padding;

    // Header
    {
        ImGui::SetWindowFontScale(1.5f);
        DrawGlowText(dl, ImVec2(left, y), "> SPOT_TRADE", MatrixTheme::TEXT);
        ImGui::SetWindowFontScale(1.0f);

        const char* nav = "PERP | ASSET | PROFILE";
        ImVec2 navSz = ImGui::CalcTextSize(nav);
        dl->AddText(ImVec2(right - navSz.x - 70, y + 8), MatrixTheme::DIM, nav);

        dl->AddRectFilled(ImVec2(right - 50, y), ImVec2(right - 30, y + 20), MatrixTheme::DIM, 2.0f);
        dl->AddText(ImVec2(right - 48, y + 2), MatrixTheme::BLACK, "L1");
        dl->AddRectFilled(ImVec2(right - 25, y), ImVec2(right - 5, y + 20), MatrixTheme::DIM, 2.0f);
        dl->AddText(ImVec2(right - 23, y + 2), MatrixTheme::BLACK, "R1");

        y += headerH;
        dl->AddLine(ImVec2(left, y - 10), ImVec2(right, y - 10), MatrixTheme::DIM, 2.0f);
    }

    // Table headers
    {
        float col1 = left;
        float col2 = left + w * 0.35f;
        float col3 = right - 130;
        float col4 = right;

        dl->AddText(ImVec2(col1 + 30, y), MatrixTheme::DIM, "CODE");

        const char* h2 = "HOLDINGS";
        ImVec2 sz2 = ImGui::CalcTextSize(h2);
        dl->AddText(ImVec2(col2 - sz2.x * 0.5f, y), MatrixTheme::DIM, h2);

        const char* h3 = "PRICE";
        ImVec2 sz3 = ImGui::CalcTextSize(h3);
        dl->AddText(ImVec2(col3 - sz3.x, y), MatrixTheme::DIM, h3);

        const char* h4 = "24H";
        ImVec2 sz4 = ImGui::CalcTextSize(h4);
        dl->AddText(ImVec2(col4 - sz4.x, y), MatrixTheme::DIM, h4);

        y += tableHeaderH;
    }

    // List
    {
        float listH = size.y - padding - footerH - y + p.y;
        int startIdx = 0;
        int maxRows = std::max(1, (int)(listH / rowH));
        if (selected_row_idx >= maxRows) startIdx = selected_row_idx - maxRows + 1;

        for (int i = startIdx; i < (int)MOCK_COINS.size() && (i - startIdx) < maxRows; ++i) {
            const auto& coin = MOCK_COINS[i];
            bool isSelected = (i == selected_row_idx);
            float rowY = y + (i - startIdx) * rowH;

            if (isSelected) {
                dl->AddRectFilled(ImVec2(left, rowY), ImVec2(right, rowY + rowH - 4), MatrixTheme::TEXT, 4.0f);
            } else {
                dl->AddRectFilled(ImVec2(left, rowY), ImVec2(right, rowY + rowH - 4), IM_COL32(0, 59, 0, 40), 4.0f);
            }

            ImU32 textCol = isSelected ? MatrixTheme::BLACK : MatrixTheme::TEXT;
            ImU32 numCol = isSelected ? MatrixTheme::BLACK : MatrixTheme::TEXT;
            ImU32 changeCol = isSelected ? MatrixTheme::BLACK : (coin.change24h >= 0 ? MatrixTheme::TEXT : MatrixTheme::ALERT);

            float col1 = left;
            float col2 = left + w * 0.35f;
            float col3 = right - 130;
            float col4 = right;

            if (isSelected) {
                if ((ImGui::GetTime() * 2.0 - (int)(ImGui::GetTime() * 2.0)) < 0.7)
                    dl->AddText(ImVec2(col1 + 5, rowY + 8), textCol, ">");
            }

            dl->AddText(ImVec2(col1 + 30, rowY + 8), textCol, coin.symbol.c_str());

            if (coin.holdings > 0) {
                char holdBuf[32];
                std::snprintf(holdBuf, sizeof(holdBuf), "%.2f", coin.holdings);
                ImVec2 sz = ImGui::CalcTextSize(holdBuf);
                dl->AddText(ImVec2(col2 - sz.x * 0.5f, rowY + 8), textCol, holdBuf);
            }

            char priceBuf[32];
            std::snprintf(priceBuf, sizeof(priceBuf), "%.2f", coin.price);
            ImVec2 szP = ImGui::CalcTextSize(priceBuf);
            dl->AddText(ImVec2(col3 - szP.x, rowY + 8), numCol, priceBuf);

            char chgBuf[32];
            std::snprintf(chgBuf, sizeof(chgBuf), "%+.1f%%", coin.change24h);
            ImVec2 szC = ImGui::CalcTextSize(chgBuf);
            dl->AddText(ImVec2(col4 - szC.x, rowY + 8), changeCol, chgBuf);
        }
    }

    // Footer
    {
        float footerTop = p.y + size.y - footerH;
        dl->AddLine(ImVec2(left, footerTop), ImVec2(right, footerTop), MatrixTheme::DIM, 4.0f);

        const auto& selCoin = MOCK_COINS[selected_row_idx];
        char summary[128];
        double val = selCoin.holdings * selCoin.price;
        if (selCoin.holdings > 0)
            std::snprintf(summary, sizeof(summary), "Holding %.2f %s worth $%.2f", selCoin.holdings, selCoin.symbol.c_str(), val);
        else
            std::snprintf(summary, sizeof(summary), "No %s in wallet", selCoin.symbol.c_str());
        dl->AddText(ImVec2(left, footerTop + 20), MatrixTheme::TEXT, summary);

        float btnW = 100.0f;
        float btnH = 40.0f;
        float btnY = footerTop + 15;
        float sellX = right - btnW;
        float buyX = sellX - btnW - 20;

        bool buyFocus = (action_idx == 0);
        bool sellFocus = (action_idx == 1);

        ImU32 buyBg = buyFocus ? MatrixTheme::TEXT : MatrixTheme::BG;
        ImU32 buyFg = buyFocus ? MatrixTheme::BLACK : MatrixTheme::DIM;
        ImU32 buyBorder = buyFocus ? MatrixTheme::TEXT : MatrixTheme::DIM;
        if (buy_pressed && buyFocus) buyBg = MatrixTheme::DIM;
        dl->AddRectFilled(ImVec2(buyX, btnY), ImVec2(buyX + btnW, btnY + btnH), buyBg, 4.0f);
        dl->AddRect(ImVec2(buyX, btnY), ImVec2(buyX + btnW, btnY + btnH), buyBorder, 4.0f, 0, 2.0f);
        ImVec2 bSz = ImGui::CalcTextSize("BUY");
        dl->AddText(ImVec2(buyX + (btnW - bSz.x) * 0.5f, btnY + (btnH - bSz.y) * 0.5f), buyFg, "BUY");

        ImU32 sellBg = sellFocus ? MatrixTheme::ALERT : MatrixTheme::BG;
        ImU32 sellFg = sellFocus ? MatrixTheme::BLACK : MatrixTheme::DIM;
        ImU32 sellBorder = sellFocus ? MatrixTheme::ALERT : MatrixTheme::DIM;
        if (sell_pressed && sellFocus) sellBg = MatrixTheme::DIM;
        dl->AddRectFilled(ImVec2(sellX, btnY), ImVec2(sellX + btnW, btnY + btnH), sellBg, 4.0f);
        dl->AddRect(ImVec2(sellX, btnY), ImVec2(sellX + btnW, btnY + btnH), sellBorder, 4.0f, 0, 2.0f);
        ImVec2 sSz = ImGui::CalcTextSize("SELL");
        dl->AddText(ImVec2(sellX + (btnW - sSz.x) * 0.5f, btnY + (btnH - sSz.y) * 0.5f), sellFg, "SELL");
    }
}

} // namespace tradeboy::spot
