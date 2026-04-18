#include "ui/Message.h"

#include "ui/MatrixTheme.h"

#include <cfloat>

namespace tradeboy::ui {

static ImVec2 calc_text_size_with_font(ImFont* font, const std::string& text) {
    if (text.empty()) {
        return ImVec2(0.0f, 0.0f);
    }

    ImFont* prev_font = ImGui::GetFont();
    if (font && font != prev_font) {
        ImGui::PushFont(font);
    }

    ImVec2 size = ImGui::CalcTextSize(text.c_str());

    if (font && font != prev_font) {
        ImGui::PopFont();
    }

    return size;
}

void render_message(const char* id,
                    const MessageState& state,
                    ImFont* font_bold) {
    if (!state.open) return;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 display = io.DisplaySize;
    if (display.x <= 1.0f || display.y <= 1.0f) return;

    float open_t = state.get_open_t();
    if (open_t < 0.0f) open_t = 0.0f;
    if (open_t > 1.0f) open_t = 1.0f;
    float ease = 1.0f - (1.0f - open_t) * (1.0f - open_t);

    tradeboy::utils::TypewriterState& tw = const_cast<tradeboy::utils::TypewriterState&>(state.tw);
    std::string shown_text;
    if (open_t >= 1.0f && !state.closing) {
        shown_text = tradeboy::utils::typewriter_shown(tw, state.body, ImGui::GetTime(), 35.0);
    } else if (state.closing) {
        shown_text = tradeboy::utils::typewriter_shown(tw, state.body, tw.start_time > 0.0 ? tw.start_time + 9999.0 : ImGui::GetTime() + 9999.0, 35.0);
    }

    ImFont* font = font_bold ? font_bold : ImGui::GetFont();
    ImVec2 blank_size = calc_text_size_with_font(font, " ");
    ImVec2 text_size = shown_text.empty()
        ? ImVec2(blank_size.x * 6.0f, ImGui::GetTextLineHeight())
        : calc_text_size_with_font(font, shown_text);

    float target_width = text_size.x + 56.0f;
    float min_width = 140.0f;
    float max_width = display.x - 48.0f;
    if (target_width < min_width) target_width = min_width;
    if (target_width > max_width) target_width = max_width;

    float target_height = ImGui::GetTextLineHeight() + 30.0f;
    float width = min_width + (target_width - min_width) * ease;
    float height = 22.0f + (target_height - 22.0f) * ease;
    float scale = 0.1f + 0.9f * ease;

    ImVec2 base_pos((display.x - width) * 0.5f, 20.0f);
    ImVec2 pos(base_pos.x + (width * 0.5f) * (1.0f - scale),
               base_pos.y + (height * 0.5f) * (1.0f - scale));
    ImVec2 anim_size(width * scale, height * scale);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(anim_size, ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, (int)(235.0f * ease)));
    ImGui::PushStyleColor(ImGuiCol_Border, MatrixTheme::TEXT);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 12.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_NoNav |
                             ImGuiWindowFlags_NoFocusOnAppearing |
                             ImGuiWindowFlags_NoInputs |
                             ImGuiWindowFlags_Tooltip;

    ImGui::Begin(id ? id : "Message", nullptr, flags);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetWindowPos();
    ImVec2 sz = ImGui::GetWindowSize();
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), MatrixTheme::TEXT, 0.0f, 0, 2.0f);
    if (font) {
        ImGui::PushFont(font);
    }
    ImVec2 draw_text_size = shown_text.empty()
        ? ImVec2(0.0f, ImGui::GetTextLineHeight())
        : ImGui::CalcTextSize(shown_text.c_str());
    float text_x = (sz.x - draw_text_size.x) * 0.5f;
    if (text_x < 16.0f) text_x = 16.0f;
    float text_y = (sz.y - ImGui::GetTextLineHeight()) * 0.5f;
    if (text_y < 10.0f) text_y = 10.0f;
    ImGui::SetCursorPos(ImVec2(text_x, text_y));
    ImGui::TextColored(ImColor(MatrixTheme::TEXT), "%s", shown_text.c_str());
    if (font) {
        ImGui::PopFont();
    }

    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
}

} // namespace tradeboy::ui
