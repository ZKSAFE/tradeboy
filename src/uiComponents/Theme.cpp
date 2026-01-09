#include "Theme.h"

#include "imgui.h"

#include "../utils/Scale.h"

namespace tradeboy::ui {

void apply_retro_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark();

    s.WindowRounding = 10.0f;
    s.FrameRounding = 8.0f;
    s.ScrollbarRounding = 8.0f;
    s.GrabRounding = 8.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_Border] = ImVec4(0.30f, 0.29f, 0.24f, 1.00f);
    c[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.93f, 1.00f);

    c[ImGuiCol_Button] = ImVec4(0.30f, 0.29f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.44f, 0.35f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.38f, 0.36f, 0.28f, 1.00f);

    c[ImGuiCol_FrameBg] = ImVec4(0.22f, 0.22f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.28f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.32f, 0.32f, 0.26f, 1.00f);
}

const Colors& colors() {
    static const Colors c = {
        .panel = IM_COL32(78, 77, 66, 255),
        .panel2 = IM_COL32(92, 90, 78, 255),
        .panel_pressed = IM_COL32(70, 68, 58, 255),
        .stroke = IM_COL32(125, 123, 106, 255),
        .text = IM_COL32(245, 245, 240, 255),
        .muted = IM_COL32(200, 200, 185, 255),
        .green = IM_COL32(110, 215, 140, 255),
        .red = IM_COL32(235, 110, 110, 255),
        .grid = IM_COL32(60, 60, 54, 255),
    };
    return c;
}

} // namespace tradeboy::ui
