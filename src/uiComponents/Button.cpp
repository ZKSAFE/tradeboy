#include "Button.h"

namespace tradeboy::ui {

void draw_circle_button(ImDrawList* dl, ImVec2 center, float r, ImU32 bg, ImU32 fg, const char* label, bool pseudo_bold) {
    if (!dl || !label) return;
    dl->AddCircleFilled(center, r, bg);
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImVec2 pos(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f - 3.0f);
    dl->AddText(pos, fg, label);
    if (pseudo_bold) dl->AddText(ImVec2(pos.x + 1.0f, pos.y), fg, label);
}

void draw_pill_button(ImDrawList* dl, ImVec2 min, ImVec2 max, float rounding, ImU32 bg, ImU32 fg, const char* label, bool pseudo_bold) {
    if (!dl || !label) return;
    dl->AddRectFilled(min, max, bg, rounding);
    ImVec2 ts = ImGui::CalcTextSize(label);
    ImVec2 pos(min.x + (max.x - min.x - ts.x) * 0.5f, min.y + (max.y - min.y - ts.y) * 0.5f - 2.0f);
    dl->AddText(pos, fg, label);
    if (pseudo_bold) dl->AddText(ImVec2(pos.x + 1.0f, pos.y), fg, label);
}

} // namespace tradeboy::ui
