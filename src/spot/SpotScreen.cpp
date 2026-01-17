#include "SpotScreen.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "ui/MatrixTheme.h"
#include "utils/Flash.h"
#include "utils/Typewriter.h"

namespace tradeboy::spot {

struct Coin {
    std::string id;
    std::string symbol;
    std::string name;
    double price;
    double change24h;
    double holdings;
};

static const Coin MOCK_COINS_ARR[] = {
    {"1", "BTC", "Bitcoin", 64230.50, 2.4, 0.15},
    {"2", "ETH", "Ethereum", 3450.12, -1.2, 2.5},
    {"3", "SOL", "Solana", 145.60, 5.8, 100.0},
    {"4", "DOGE", "Dogecoin", 0.12, 0.5, 5000.0},
    {"5", "ADA", "Cardano", 0.45, -3.4, 0.0},
    {"6", "XRP", "Ripple", 0.60, 1.1, 0.0},
    {"7", "DOT", "Polkadot", 7.20, -0.8, 0.0},
};

static std::vector<Coin> MOCK_COINS(
    MOCK_COINS_ARR,
    MOCK_COINS_ARR + (sizeof(MOCK_COINS_ARR) / sizeof(MOCK_COINS_ARR[0])));

void render_spot_screen(int selected_row_idx, int action_idx, bool buy_pressed, bool sell_pressed, ImFont* font_bold, bool action_btn_held, bool l1_btn_held, bool r1_btn_held) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();

    (void)action_btn_held;
    (void)l1_btn_held;
    (void)r1_btn_held;

    if (size.x <= 1.0f || size.y <= 1.0f) return;
    if (!dl) return;
    if (MOCK_COINS.empty()) return;

    selected_row_idx = std::max(0, std::min((int)MOCK_COINS.size() - 1, selected_row_idx));
    action_idx = std::max(0, std::min(1, action_idx));

    const float padding = 16.0f;
    const float headerH = 54.0f;
    const float footerH = 55.0f; // Increased to move footer up
    const float tableHeaderH = 30.0f;
    const float rowH = 44.0f;

    float y = p.y + padding;
    float w = size.x - 2 * padding;
    float left = p.x + padding;
    float right = p.x + size.x - padding;

    // Header is rendered by MainUI. Keep spacing and divider.
    y += headerH;
    dl->AddLine(ImVec2(left, y - 16), ImVec2(right, y - 16), MatrixTheme::DIM, 2.0f);

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
        
        float textH = ImGui::CalcTextSize("A").y;

        for (int i = startIdx; i < (int)MOCK_COINS.size() && (i - startIdx) < maxRows; ++i) {
            const auto& coin = MOCK_COINS[i];
            bool isSelected = (i == selected_row_idx);
            float rowY = y + (i - startIdx) * rowH;

            float rowContentH = rowH - 4.0f; // Height of the background rect
            float textY = rowY + (rowContentH - textH) * 0.5f;

            // 2. Selected item background: Rectangular (0.0f rounding)
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
            ImU32 numCol = isSelected ? MatrixTheme::BLACK : MatrixTheme::TEXT;
            ImU32 changeCol = isSelected ? MatrixTheme::BLACK : (coin.change24h >= 0 ? MatrixTheme::TEXT : MatrixTheme::ALERT);

            float col1 = left;
            float col2 = left + w * 0.35f;
            float col3 = right - 130;
            float col4 = right;

            dl->AddText(ImVec2(col1 + 30, textY), textCol, coin.symbol.c_str());

            if (coin.holdings > 0) {
                char holdBuf[32];
                std::snprintf(holdBuf, sizeof(holdBuf), "%.2f", coin.holdings);
                ImVec2 sz = ImGui::CalcTextSize(holdBuf);
                dl->AddText(ImVec2(col2 - sz.x * 0.5f, textY), textCol, holdBuf);
            }

            char priceBuf[32];
            std::snprintf(priceBuf, sizeof(priceBuf), "%.2f", coin.price);
            ImVec2 szP = ImGui::CalcTextSize(priceBuf);
            dl->AddText(ImVec2(col3 - szP.x, textY), numCol, priceBuf);

            char chgBuf[32];
            std::snprintf(chgBuf, sizeof(chgBuf), "%+.1f%%", coin.change24h);
            ImVec2 szC = ImGui::CalcTextSize(chgBuf);
            dl->AddText(ImVec2(col4 - szC.x, textY), changeCol, chgBuf);
        }
    }

    // Footer
    {
        float footerTop = p.y + size.y - footerH;
        dl->AddLine(ImVec2(left, footerTop), ImVec2(right, footerTop), MatrixTheme::DIM, 2.0f);

        const auto& selCoin = MOCK_COINS[selected_row_idx];
        char body[128];
        double val = selCoin.holdings * selCoin.price;
        if (selCoin.holdings > 0)
            std::snprintf(body, sizeof(body), "It worth $%.2f", val);
        else
            std::snprintf(body, sizeof(body), "No %s", selCoin.symbol.c_str());

        static tradeboy::utils::TypewriterState tw;
        std::string full_body = body;
        std::string shown_text = tradeboy::utils::typewriter_shown(tw, full_body, ImGui::GetTime(), 35.0);

        // Prompt is always visible; only body is typed.
        dl->AddText(ImVec2(left, footerTop + 20), MatrixTheme::TEXT, "> ");
        dl->AddText(ImVec2(left + 18, footerTop + 20), MatrixTheme::TEXT, shown_text.c_str());

        float btnW = 100.0f;
        float btnH = 40.0f;
        float btnY = footerTop + 15;
        float sellX = right - btnW;
        float buyX = sellX - btnW - 20;

        bool buyFocus = (action_idx == 0);
        bool sellFocus = (action_idx == 1);
        
        float btnFontSize = 20.0f;

        // BUY
        ImU32 buyBg = buyFocus ? MatrixTheme::TEXT : IM_COL32(0,0,0,0);
        ImU32 buyFg = buyFocus ? MatrixTheme::BLACK : MatrixTheme::DIM;
        ImU32 buyBorder = buyFocus ? MatrixTheme::TEXT : MatrixTheme::DIM;

        // Flash effect matches Dialog cancel button: transparent fill + bright text while flashing.
        if (buy_pressed && buyFocus) {
            buyBg = IM_COL32(0,0,0,0);
            buyFg = MatrixTheme::TEXT;
            buyBorder = MatrixTheme::TEXT;
        }
        
        dl->AddRectFilled(ImVec2(buyX, btnY), ImVec2(buyX + btnW, btnY + btnH), buyBg, 0.0f);
        dl->AddRect(ImVec2(buyX, btnY), ImVec2(buyX + btnW, btnY + btnH), buyBorder, 0.0f, 0, 2.0f);
        
        ImVec2 bSz;
        if (font_bold) {
            bSz = font_bold->CalcTextSizeA(btnFontSize, FLT_MAX, 0.0f, "BUY");
            dl->AddText(font_bold, btnFontSize, ImVec2(buyX + (btnW - bSz.x) * 0.5f, btnY + (btnH - bSz.y) * 0.5f), buyFg, "BUY");
        } else {
            // No custom size A for default font easily accessible without push/pop or scaling
            // Assuming default font size is 28, scaling 22/28 approx 0.8
            // But we can just use AddText with default if bold missing.
            bSz = ImGui::CalcTextSize("BUY");
            dl->AddText(ImVec2(buyX + (btnW - bSz.x) * 0.5f, btnY + (btnH - bSz.y) * 0.5f), buyFg, "BUY");
        }
        
        // SELL
        ImU32 sellBg = sellFocus ? MatrixTheme::TEXT : IM_COL32(0,0,0,0);
        ImU32 sellFg = sellFocus ? MatrixTheme::BLACK : MatrixTheme::DIM;
        ImU32 sellBorder = sellFocus ? MatrixTheme::TEXT : MatrixTheme::DIM;

        // Flash effect matches Dialog cancel button.
        if (sell_pressed && sellFocus) {
            sellBg = IM_COL32(0,0,0,0);
            sellFg = MatrixTheme::TEXT;
            sellBorder = MatrixTheme::TEXT;
        }
        
        dl->AddRectFilled(ImVec2(sellX, btnY), ImVec2(sellX + btnW, btnY + btnH), sellBg, 0.0f);
        dl->AddRect(ImVec2(sellX, btnY), ImVec2(sellX + btnW, btnY + btnH), sellBorder, 0.0f, 0, 2.0f);
        
        ImVec2 sSz;
        if (font_bold) {
            sSz = font_bold->CalcTextSizeA(btnFontSize, FLT_MAX, 0.0f, "SELL");
            dl->AddText(font_bold, btnFontSize, ImVec2(sellX + (btnW - sSz.x) * 0.5f, btnY + (btnH - sSz.y) * 0.5f), sellFg, "SELL");
        } else {
            sSz = ImGui::CalcTextSize("SELL");
            dl->AddText(ImVec2(sellX + (btnW - sSz.x) * 0.5f, btnY + (btnH - sSz.y) * 0.5f), sellFg, "SELL");
        }
    }
}

} // namespace tradeboy::spot
