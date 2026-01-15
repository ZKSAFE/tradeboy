#include "MatrixBackground.h"

namespace tradeboy::ui {

void render_matrix_grid(ImDrawList* dl, const ImVec2& p, const ImVec2& size) {
    if (!dl) return;
    if (size.x <= 1.0f || size.y <= 1.0f) return;

    const float gridStep = 40.0f;
    ImU32 gridCol = IM_COL32(0, 255, 65, 20);

    float cx = p.x + size.x * 0.5f;
    float cy = p.y + size.y * 0.5f;

    dl->AddLine(ImVec2(cx, p.y), ImVec2(cx, p.y + size.y), gridCol);
    dl->AddLine(ImVec2(p.x, cy), ImVec2(p.x + size.x, cy), gridCol);

    for (float x = gridStep; x < size.x * 0.5f; x += gridStep) {
        dl->AddLine(ImVec2(cx + x, p.y), ImVec2(cx + x, p.y + size.y), gridCol);
        dl->AddLine(ImVec2(cx - x, p.y), ImVec2(cx - x, p.y + size.y), gridCol);
    }

    for (float y = gridStep; y < size.y * 0.5f; y += gridStep) {
        dl->AddLine(ImVec2(p.x, cy + y), ImVec2(p.x + size.x, cy + y), gridCol);
        dl->AddLine(ImVec2(p.x, cy - y), ImVec2(p.x + size.x, cy - y), gridCol);
    }
}

} // namespace tradeboy::ui
