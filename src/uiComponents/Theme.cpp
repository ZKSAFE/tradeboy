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

} // namespace tradeboy::ui
