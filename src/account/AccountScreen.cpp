#include "account/AccountScreen.h"
#include "ui/MatrixTheme.h"
#include "utils/Flash.h"
#include <algorithm>
#include <cstdio>
#include <string>

namespace tradeboy::account {

// Mock data to match design
static const char* MOCK_HL_TOTAL_ASSET = "$42,904.32";

void render_account_screen(int focused_col,
                           int flash_btn,
                           int flash_timer,
                           ImFont* font_bold,
                           const char* hl_usdc,
                           const char* arb_address_short,
                           const char* arb_eth,
                           const char* arb_usdc,
                           const char* arb_gas,
                           const char* arb_fee) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (!dl) return;
    if (size.x <= 1.0f || size.y <= 1.0f) return;

    ImFont* font_reg = ImGui::GetFont();

    const float padding = 16.0f;
    const float headerH = 54.0f;
    
    // Header rendered by MainUI, just skip space and draw divider
    float y = p.y + padding + headerH;
    float left = p.x + padding;
    float right = p.x + size.x - padding;
    float w = size.x - 2 * padding;
    
    dl->AddLine(ImVec2(left, y - 16), ImVec2(right, y - 16), MatrixTheme::DIM, 2.0f);

    // Grid layout: 2 Columns
    float colGap = 24.0f;
    float colW = (w - colGap) * 0.5f;
    float col1X = left;
    float col2X = left + colW + colGap;
    float contentH = size.y - padding - (y - p.y); // Remaining height
    
    // Column 1: HYPER_LIQUID
    {
        bool isFocused = (focused_col == 0);
        float cx = col1X;
        float cy = y;
        
        // Background & Border
        ImU32 bgCol = isFocused ? IM_COL32(0, 40, 0, 100) : IM_COL32(0, 0, 0, 100);
        ImU32 borderCol = isFocused ? MatrixTheme::TEXT : MatrixTheme::DIM;
        dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + colW, cy + contentH), bgCol, 0.0f);
        dl->AddRect(ImVec2(cx, cy), ImVec2(cx + colW, cy + contentH), borderCol, 0.0f, 0, 4.0f);

        float innerP = 16.0f;
        float currY = cy + innerP;
        float innerW = colW - 2 * innerP;

        // Title
        if (isFocused && tradeboy::utils::blink_on_time(ImGui::GetTime(), 2.0)) {
            dl->AddRectFilled(ImVec2(cx + innerP, currY + 4), ImVec2(cx + innerP + 10, currY + 24), MatrixTheme::TEXT);
        }
        
        ImU32 titleCol = isFocused ? MatrixTheme::TEXT : MatrixTheme::DIM;
        const char* title = ">> HYPER_LIQUID";
        float titleX = cx + innerP + (isFocused ? 16.0f : 0.0f);
        dl->AddText(font_reg, 26.0f, ImVec2(titleX, currY), titleCol, title);
        
        currY += 40.0f;

        // Total Asset Value
        dl->AddText(font_reg, 20.0f, ImVec2(cx + innerP, currY), MatrixTheme::DIM, "TOTAL_ASSET_VALUE");
        currY += 24.0f;
        
        // Value
        dl->AddText(font_reg, 40.0f, ImVec2(cx + innerP, currY), MatrixTheme::TEXT, MOCK_HL_TOTAL_ASSET);
        currY += 60.0f;

        // 24h PnL Flux Box
        {
            float boxH = 72.0f;
            dl->AddRectFilled(ImVec2(cx + innerP, currY), ImVec2(cx + innerP + innerW, currY + boxH), IM_COL32(0, 50, 0, 60), 0.0f);
            dl->AddRectFilled(ImVec2(cx + innerP, currY), ImVec2(cx + innerP + 4, currY + boxH), MatrixTheme::TEXT, 0.0f); // Left green bar
            
            float bx = cx + innerP + 12.0f;
            
            // Label: text-sm -> 14px
            const float lblSz = 16.0f;
            const float vSz = 26.0f;
            const float gap = 2.0f;
            const float blockH = lblSz + gap + vSz + gap + vSz;
            float blockY = currY + (boxH - blockH) * 0.5f;

            dl->AddText(font_reg, lblSz, ImVec2(bx, blockY), MatrixTheme::DIM, "24H_PNL_FLUX");
            dl->AddText(font_reg, vSz, ImVec2(bx, blockY + lblSz + gap), MatrixTheme::TEXT, "+$1,240.50");
            dl->AddText(font_reg, vSz, ImVec2(bx, blockY + lblSz + gap + vSz + gap), MatrixTheme::TEXT, "(+2.8%)");
            
            currY += boxH + 20.0f;
        }

        // USDC Row
        {
            dl->AddLine(ImVec2(cx + innerP, currY + 35), ImVec2(cx + innerP + innerW, currY + 35), IM_COL32(0, 50, 0, 255), 1.0f);
            
            // Label: text-3xl -> 30px
            dl->AddText(font_reg, 26.0f, ImVec2(cx + innerP, currY), MatrixTheme::DIM, "USDC(SPOT)");
            
            // Value: text-3xl -> 30px
            const char* v = (hl_usdc && hl_usdc[0]) ? hl_usdc : "UNKNOWN";
            ImVec2 valSz = font_reg->CalcTextSizeA(26.0f, FLT_MAX, 0.0f, v);
            dl->AddText(font_reg, 26.0f, ImVec2(cx + innerP + innerW - valSz.x, currY), MatrixTheme::TEXT, v);
        }

        // Bottom Button: WITHDRAW USDC ->
        {
            float btnH = 50.0f;
            float btnY = cy + contentH - innerP - btnH;
            
            ImU32 btnBg = isFocused ? MatrixTheme::TEXT : IM_COL32(0,0,0,0);
            ImU32 btnFg = isFocused ? MatrixTheme::BLACK : MatrixTheme::DIM;
            ImU32 btnBorder = isFocused ? MatrixTheme::TEXT : MatrixTheme::DIM;

            bool flashing = (flash_btn == 0 && flash_timer > 0);
            if (flashing && tradeboy::utils::blink_on(flash_timer, 6, 3)) {
                btnBg = IM_COL32(0,0,0,0);
                btnFg = MatrixTheme::TEXT;
                btnBorder = MatrixTheme::TEXT;
            }

            dl->AddRectFilled(ImVec2(cx + innerP, btnY), ImVec2(cx + innerP + innerW, btnY + btnH), btnBg, 0.0f);
            dl->AddRect(ImVec2(cx + innerP, btnY), ImVec2(cx + innerP + innerW, btnY + btnH), btnBorder, 0.0f, 0, 2.0f);
            
            const char* lbl = "WITHDRAW USDC ->";
            // Button Text (only button uses bold-italic font)
            ImVec2 sz = font_bold ? font_bold->CalcTextSizeA(20.0f, FLT_MAX, 0.0f, lbl)
                                  : (font_reg ? font_reg->CalcTextSizeA(20.0f, FLT_MAX, 0.0f, lbl) : ImGui::CalcTextSize(lbl));
            
            float tx = cx + innerP + (innerW - sz.x) * 0.5f;
            float ty = btnY + (btnH - sz.y) * 0.5f;
            if (font_bold) dl->AddText(font_bold, 20.0f, ImVec2(tx, ty), btnFg, lbl);
            else dl->AddText(font_reg, 20.0f, ImVec2(tx, ty), btnFg, lbl);
        }
    }

    // Column 2: ARBITRUM L2
    {
        bool isFocused = (focused_col == 1);
        float cx = col2X;
        float cy = y;
        
        ImU32 bgCol = isFocused ? IM_COL32(0, 40, 0, 100) : IM_COL32(0, 0, 0, 100);
        ImU32 borderCol = isFocused ? MatrixTheme::TEXT : MatrixTheme::DIM;
        dl->AddRectFilled(ImVec2(cx, cy), ImVec2(cx + colW, cy + contentH), bgCol, 0.0f);
        dl->AddRect(ImVec2(cx, cy), ImVec2(cx + colW, cy + contentH), borderCol, 0.0f, 0, 4.0f);

        float innerP = 16.0f;
        float currY = cy + innerP;
        float innerW = colW - 2 * innerP;

        // Title
        if (isFocused && tradeboy::utils::blink_on_time(ImGui::GetTime(), 2.0)) {
            dl->AddRectFilled(ImVec2(cx + innerP, currY + 4), ImVec2(cx + innerP + 10, currY + 24), MatrixTheme::TEXT);
        }
        
        ImU32 titleCol = isFocused ? MatrixTheme::TEXT : MatrixTheme::DIM;
        const char* title = ">> ARBITRUM_L2";
        float titleX = cx + innerP + (isFocused ? 16.0f : 0.0f);
        dl->AddText(font_reg, 26.0f, ImVec2(titleX, currY), titleCol, title);
        
        currY += 40.0f;

        // Address Box
        {
            float boxH = 50.0f;
            dl->AddRectFilled(ImVec2(cx + innerP, currY), ImVec2(cx + innerP + innerW, currY + boxH), IM_COL32(0, 40, 0, 60), 0.0f);
            dl->AddRect(ImVec2(cx + innerP, currY), ImVec2(cx + innerP + innerW, currY + boxH), IM_COL32(0, 80, 0, 100), 0.0f, 0, 1.0f);
            
            float bx = cx + innerP + 8.0f;
            float by = currY + (boxH - 18.0f) * 0.5f;
            
            // Label: text-lg -> 18px
            dl->AddText(font_reg, 20.0f, ImVec2(bx, by), MatrixTheme::DIM, "ADDRESS");
            
            float rightEdge = cx + innerP + innerW - 8.0f;
            
            // X button hint
            float btnSize = 24.0f;
            dl->AddRectFilled(ImVec2(rightEdge - btnSize, currY + (boxH-btnSize)*0.5f), 
                              ImVec2(rightEdge, currY + (boxH+btnSize)*0.5f), 
                              MatrixTheme::DIM, 2.0f);
            ImVec2 xSz = ImGui::CalcTextSize("X");
            dl->AddText(ImVec2(rightEdge - btnSize + (btnSize-xSz.x)*0.5f, currY + (boxH-xSz.y)*0.5f), MatrixTheme::BLACK, "X");
            
            const char* addr = (arb_address_short && arb_address_short[0]) ? arb_address_short : "UNKNOWN";
            ImVec2 addrSz = font_reg ? font_reg->CalcTextSizeA(20.0f, FLT_MAX, 0.0f, addr) : ImGui::CalcTextSize(addr);
            float addrX = rightEdge - btnSize - 8.0f - addrSz.x;
            dl->AddText(font_reg, 20.0f, ImVec2(addrX, currY + (boxH-addrSz.y)*0.5f), MatrixTheme::TEXT, addr);
            
            currY += boxH + 20.0f;
        }

        // Assets
        auto draw_row = [&](const char* label, const char* val) {
            dl->AddText(font_reg, 26.0f, ImVec2(cx + innerP, currY), MatrixTheme::DIM, label);

            ImVec2 valSz = font_reg ? font_reg->CalcTextSizeA(26.0f, FLT_MAX, 0.0f, val) : ImGui::CalcTextSize(val);
            dl->AddText(font_reg, 26.0f, ImVec2(cx + innerP + innerW - valSz.x, currY), MatrixTheme::TEXT, val);
            
            currY += 40.0f;
        };
        
        draw_row("ETH", (arb_eth && arb_eth[0]) ? arb_eth : "UNKNOWN");
        draw_row("USDC", (arb_usdc && arb_usdc[0]) ? arb_usdc : "UNKNOWN");
        
        // GAS: default size (~14px)
        currY += 10.0f;
        const float subFont = 20.0f;
        const char* gas = (arb_gas && arb_gas[0]) ? arb_gas : "GAS: UNKNOWN";
        ImVec2 gasSz = font_reg ? font_reg->CalcTextSizeA(subFont, FLT_MAX, 0.0f, gas) : ImGui::CalcTextSize(gas);
        dl->AddText(font_reg, subFont, ImVec2(cx + innerP + (innerW - gasSz.x) * 0.5f, currY), MatrixTheme::DIM, gas);

        currY += 22.0f;
        const char* fee = (arb_fee && arb_fee[0]) ? arb_fee : "TRANSATION FEE: $UNKNOWN";
        ImVec2 feeSz = font_reg ? font_reg->CalcTextSizeA(subFont, FLT_MAX, 0.0f, fee) : ImGui::CalcTextSize(fee);
        dl->AddText(font_reg, subFont, ImVec2(cx + innerP + (innerW - feeSz.x) * 0.5f, currY), MatrixTheme::DIM, fee);

        // Bottom Button: <- DEPOSIT USDC
        {
            float btnH = 50.0f;
            float btnY = cy + contentH - innerP - btnH;
            
            ImU32 btnBg = isFocused ? MatrixTheme::TEXT : IM_COL32(0,0,0,0);
            ImU32 btnFg = isFocused ? MatrixTheme::BLACK : MatrixTheme::DIM;
            ImU32 btnBorder = isFocused ? MatrixTheme::TEXT : MatrixTheme::DIM;

            bool flashing = (flash_btn == 1 && flash_timer > 0);
            if (flashing && tradeboy::utils::blink_on(flash_timer, 6, 3)) {
                btnBg = IM_COL32(0,0,0,0);
                btnFg = MatrixTheme::TEXT;
                btnBorder = MatrixTheme::TEXT;
            }

            dl->AddRectFilled(ImVec2(cx + innerP, btnY), ImVec2(cx + innerP + innerW, btnY + btnH), btnBg, 0.0f);
            dl->AddRect(ImVec2(cx + innerP, btnY), ImVec2(cx + innerP + innerW, btnY + btnH), btnBorder, 0.0f, 0, 2.0f);
            
            const char* lbl = "<- DEPOSIT USDC";
            // Button Text (only button uses bold-italic font)
            ImVec2 sz = font_bold ? font_bold->CalcTextSizeA(20.0f, FLT_MAX, 0.0f, lbl)
                                  : (font_reg ? font_reg->CalcTextSizeA(20.0f, FLT_MAX, 0.0f, lbl) : ImGui::CalcTextSize(lbl));
            
            float tx = cx + innerP + (innerW - sz.x) * 0.5f;
            float ty = btnY + (btnH - sz.y) * 0.5f;
            if (font_bold) dl->AddText(font_bold, 20.0f, ImVec2(tx, ty), btnFg, lbl);
            else dl->AddText(font_reg, 20.0f, ImVec2(tx, ty), btnFg, lbl);
        }
    }
 }

} // namespace tradeboy::account
