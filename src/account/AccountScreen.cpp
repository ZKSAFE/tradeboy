#include "account/AccountScreen.h"

#include "ui/MatrixTheme.h"

namespace tradeboy::account {

void render_account_screen(ImFont* font_bold) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    if (!dl) return;

    const float padding = 16.0f;
    const float headerH = 54.0f;

    float left = p.x + padding;
    float right = p.x + size.x - padding;
    float y = p.y + padding;

    y += headerH;
    dl->AddLine(ImVec2(left, y - 16), ImVec2(right, y - 16), MatrixTheme::DIM, 2.0f);

    const char* msg = "IN DEVELOPMENT";
    ImVec2 ts = font_bold ? font_bold->CalcTextSizeA(28.0f, FLT_MAX, 0.0f, msg) : ImGui::CalcTextSize(msg);
    float cx = p.x + (size.x - ts.x) * 0.5f;
    float cy = p.y + (size.y - ts.y) * 0.5f;
    if (font_bold) {
        dl->AddText(font_bold, 28.0f, ImVec2(cx, cy), MatrixTheme::DIM, msg);
    } else {
        dl->AddText(ImVec2(cx, cy), MatrixTheme::DIM, msg);
    }
}

} // namespace tradeboy::account
