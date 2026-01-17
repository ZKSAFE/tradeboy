#include "ui/MainUI.h"

#include "ui/MatrixTheme.h"

namespace tradeboy::ui {

static const char* tab_title(tradeboy::app::Tab t) {
    switch (t) {
        case tradeboy::app::Tab::Spot: return "#SPOT";
        case tradeboy::app::Tab::Perp: return "#PERP";
        case tradeboy::app::Tab::Account: return "#ACCOUNT";
    }
    return "#SPOT";
}

void render_main_header(tradeboy::app::Tab tab, bool l1_flash, bool r1_flash, ImFont* font_bold) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (!dl) return;
    if (size.x <= 1.0f || size.y <= 1.0f) return;

    const float padding = 16.0f;
    float left = p.x + padding;
    float right = p.x + size.x - padding;
    float y = p.y + padding;

    float headerDrawY = y - 10.0f;
    float titleSize = 42.0f;

    ImU32 glowCol = (MatrixTheme::TEXT & 0x00FFFFFF) | 0x40000000;
    const char* title = tab_title(tab);
    if (font_bold) {
        dl->AddText(font_bold, titleSize, ImVec2(left + 1, headerDrawY + 1), glowCol, title);
        dl->AddText(font_bold, titleSize, ImVec2(left - 1, headerDrawY - 1), glowCol, title);
        dl->AddText(font_bold, titleSize, ImVec2(left, headerDrawY), MatrixTheme::TEXT, title);
    } else {
        dl->AddText(ImVec2(left + 1, headerDrawY + 1), glowCol, title);
        dl->AddText(ImVec2(left - 1, headerDrawY - 1), glowCol, title);
        dl->AddText(ImVec2(left, headerDrawY), MatrixTheme::TEXT, title);
    }

    // Right nav: "SPOT | PERP | ACCOUNT" but current page shows as "*"
    const char* seg0 = (tab == tradeboy::app::Tab::Spot) ? "*" : "SPOT";
    const char* seg1 = (tab == tradeboy::app::Tab::Perp) ? "*" : "PERP";
    const char* seg2 = (tab == tradeboy::app::Tab::Account) ? "*" : "ACCOUNT";
    const char* sep = " | ";

    ImGui::SetWindowFontScale(1.0f);
    ImVec2 s0 = ImGui::CalcTextSize(seg0);
    ImVec2 s1 = ImGui::CalcTextSize(seg1);
    ImVec2 s2 = ImGui::CalcTextSize(seg2);
    ImVec2 sSep = ImGui::CalcTextSize(sep);

    // L1/R1 tags at far right
    ImGui::SetWindowFontScale(0.6f);
    float tagW = 24.0f;
    float tagH = 18.0f;
    float tagGap = 4.0f;
    float tagsW = tagW * 2.0f + tagGap;

    ImGui::SetWindowFontScale(1.0f);
    float navW = s0.x + sSep.x + s1.x + sSep.x + s2.x;

    float navY = headerDrawY + 8.0f;
    float navX = right - tagsW - 12.0f - navW;
    float x = navX;

    ImGui::SetWindowFontScale(1.0f);
    ImVec2 navBaselineSz = ImGui::CalcTextSize("SPOT");
    const float starYOffset = 4.0f;
    auto draw_seg = [&](const char* t, ImU32 col) {
        ImVec2 tsz = ImGui::CalcTextSize(t);
        float yy = navY;
        if (t[0] == '*' && t[1] == '\0') {
            yy = navY + (navBaselineSz.y - tsz.y) * 0.5f + starYOffset;
        }
        dl->AddText(ImVec2(x, yy), col, t);
        x += tsz.x;
    };

    draw_seg(seg0, (tab == tradeboy::app::Tab::Spot) ? MatrixTheme::TEXT : MatrixTheme::DIM);
    draw_seg(sep, MatrixTheme::DIM);
    draw_seg(seg1, (tab == tradeboy::app::Tab::Perp) ? MatrixTheme::TEXT : MatrixTheme::DIM);
    draw_seg(sep, MatrixTheme::DIM);
    draw_seg(seg2, (tab == tradeboy::app::Tab::Account) ? MatrixTheme::TEXT : MatrixTheme::DIM);

    // L1/R1 hints (vertically centered to nav text)
    ImGui::SetWindowFontScale(1.0f);
    ImVec2 navSz = ImGui::CalcTextSize("SPOT");
    float tagY = navY + (navSz.y - tagH) * 0.5f;

    ImU32 r1Bg = r1_flash ? MatrixTheme::TEXT : MatrixTheme::DIM;
    float r1X = right - tagW;
    dl->AddRectFilled(ImVec2(r1X, tagY), ImVec2(r1X + tagW, tagY + tagH), r1Bg, 0.0f);
    ImGui::SetWindowFontScale(0.6f);
    ImVec2 r1Sz = ImGui::CalcTextSize("R1");
    dl->AddText(ImVec2(r1X + (tagW - r1Sz.x) * 0.5f, tagY + (tagH - r1Sz.y) * 0.5f), MatrixTheme::BLACK, "R1");

    ImU32 l1Bg = l1_flash ? MatrixTheme::TEXT : MatrixTheme::DIM;
    float l1X = r1X - tagW - tagGap;
    dl->AddRectFilled(ImVec2(l1X, tagY), ImVec2(l1X + tagW, tagY + tagH), l1Bg, 0.0f);
    ImVec2 l1Sz = ImGui::CalcTextSize("L1");
    dl->AddText(ImVec2(l1X + (tagW - l1Sz.x) * 0.5f, tagY + (tagH - l1Sz.y) * 0.5f), MatrixTheme::BLACK, "L1");

    ImGui::SetWindowFontScale(1.0f);
}

} // namespace tradeboy::ui
