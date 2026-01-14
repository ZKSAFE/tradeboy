#include "ui/Dialog.h"

#include <cstdio>

#include "ui/MatrixTheme.h"

namespace tradeboy {
namespace ui {

DialogResult render_dialog(const char* id,
                           const char* prompt,
                           const std::string& body,
                           const char* btn_a,
                           const char* btn_b,
                           int* io_selected_btn,
                           int flash_frames,
                           float open_anim_t,
                           ImFont* font_bold,
                           DialogLayout* out_layout) {
    DialogResult res;

    if (out_layout) {
        out_layout->active = false;
        out_layout->rect_uv = ImVec4(0, 0, 0, 0);
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 display = io.DisplaySize;
    if (display.x <= 1.0f || display.y <= 1.0f) return res;

    if (open_anim_t < 0.0f) open_anim_t = 0.0f;
    if (open_anim_t > 1.0f) open_anim_t = 1.0f;
    float ease = open_anim_t;
    ease = 1.0f - (1.0f - ease) * (1.0f - ease);

    // Dim layer: must be ABOVE the underlying UI (including its text), but BELOW the dialog window.
    // Using a full-screen ImGui window ensures correct z-order.
    {
        char dim_id[128];
        std::snprintf(dim_id, sizeof(dim_id), "##DialogDimLayer_%s", id ? id : "Dialog");
        // Make the window slightly larger than the screen to prevent light leakage at edges
        ImGui::SetNextWindowPos(ImVec2(-10.0f, -10.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(display.x + 20.0f, display.y + 20.0f), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 60));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGuiWindowFlags dimFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                                   ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_Tooltip;
        ImGui::Begin(dim_id, nullptr, dimFlags);
        ImGui::End();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(1);
    }

    ImVec2 winSize(display.x * 0.70f, display.y * 0.36f);
    if (winSize.x < 420.0f) winSize.x = 420.0f;
    if (winSize.y < 160.0f) winSize.y = 160.0f;

    ImVec2 basePos((display.x - winSize.x) * 0.5f, (display.y - winSize.y) * 0.42f);
    float scale = 0.1f + 0.9f * ease;
    ImVec2 winPos(basePos.x + (winSize.x * 0.5f) * (1.0f - scale),
                  basePos.y + (winSize.y * 0.5f) * (1.0f - scale));
    ImVec2 animSize(winSize.x * scale, winSize.y * scale);
    ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(animSize, ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, (int)(255.0f * ease)));
    ImGui::PushStyleColor(ImGuiCol_Border, MatrixTheme::TEXT);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 16.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoTitleBar |
                             ImGuiWindowFlags_Tooltip;

    ImGui::Begin(id, nullptr, flags);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetWindowPos();
    ImVec2 sz = ImGui::GetWindowSize();

    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), MatrixTheme::TEXT, 0.0f, 0, 2.0f);

    float contentTop = p.y + 10.0f;
    float footerY = p.y + sz.y - 54.0f;

    static std::string last_body;
    static double start_time = 0.0;
    if (body != last_body) {
        last_body = body;
        start_time = ImGui::GetTime();
    }
    int shown = (int)((ImGui::GetTime() - start_time) * 35.0);
    if (shown < 0) shown = 0;
    if (shown > (int)body.size()) shown = (int)body.size();
    std::string shown_text = body.substr(0, (size_t)shown);

    dl->AddText(ImVec2(p.x + 20.0f, contentTop + 10.0f), MatrixTheme::TEXT, prompt);

    // Wrapped, multi-line body text (preserves typewriter reveal by wrapping only the shown substring).
    {
        const float x = p.x + 38.0f;
        float y = contentTop + 10.0f;
        const float max_w = (p.x + sz.x - 20.0f) - x;
        ImFont* font = ImGui::GetFont();
        const float font_size = ImGui::GetFontSize();
        const float line_h = font_size + 4.0f;
        const float scale_a = (font && font->LegacySize > 0.0f) ? (font_size / font->LegacySize) : 1.0f;

        const char* s = shown_text.c_str();
        const char* end = s + shown_text.size();
        while (s < end) {
            // Handle explicit newline.
            const char* nl = (const char*)memchr(s, '\n', (size_t)(end - s));
            const char* seg_end = nl ? nl : end;

            const char* line = s;
            while (line < seg_end) {
                const char* wrap = font->CalcWordWrapPositionA(scale_a, line, seg_end, max_w);
                if (wrap == line) {
                    // Safety: ensure progress even if a single glyph exceeds max width.
                    wrap = line + 1;
                }
                std::string part(line, wrap);
                dl->AddText(ImVec2(x, y), MatrixTheme::TEXT, part.c_str());
                y += line_h;
                line = wrap;
                while (line < seg_end && (*line == ' ' || *line == '\t')) line++;
            }

            if (nl) {
                y += line_h * 0.15f;
                s = nl + 1;
            } else {
                break;
            }
        }
    }

    float btnW = 120.0f;
    float btnH = 40.0f;
    float btnY = footerY;
    float btnBX = p.x + sz.x - 16.0f - btnW;
    float btnAX = btnBX - 16.0f - btnW;

    int sel = 1;
    if (io_selected_btn) sel = *io_selected_btn;
    if (sel < 0) sel = 0;
    if (sel > 1) sel = 1;

    bool flash_on = false;
    if (flash_frames > 0) {
        const int blinkPeriod = 6;
        const int blinkOn = 3;
        flash_on = ((flash_frames % blinkPeriod) < blinkOn);
    }

    auto draw_btn = [&](float x, const char* txt, bool selected) {
        ImU32 fill = MatrixTheme::TEXT;
        ImU32 border = MatrixTheme::TEXT;
        ImU32 text = MatrixTheme::BLACK;
        if (!selected) {
            fill = IM_COL32(0, 0, 0, 0);
            text = MatrixTheme::TEXT;
        }
        if (selected && flash_frames > 0 && flash_on) {
            fill = IM_COL32(0, 0, 0, 0);
            text = MatrixTheme::TEXT;
        }

        if (fill >> 24) {
            dl->AddRectFilled(ImVec2(x, btnY), ImVec2(x + btnW, btnY + btnH), fill, 0.0f);
        }
        dl->AddRect(ImVec2(x, btnY), ImVec2(x + btnW, btnY + btnH), border, 0.0f, 0, 2.0f);

        float btnFontSize = 20.0f;
        if (font_bold) {
            ImVec2 tSz = font_bold->CalcTextSizeA(btnFontSize, FLT_MAX, 0.0f, txt);
            dl->AddText(font_bold,
                        btnFontSize,
                        ImVec2(x + (btnW - tSz.x) * 0.5f, btnY + (btnH - tSz.y) * 0.5f),
                        text,
                        txt);
        } else {
            ImVec2 tSz = ImGui::CalcTextSize(txt);
            dl->AddText(ImVec2(x + (btnW - tSz.x) * 0.5f, btnY + (btnH - tSz.y) * 0.5f), text, txt);
        }
    };

    draw_btn(btnAX, btn_a, sel == 0);
    draw_btn(btnBX, btn_b, sel == 1);

    if (out_layout) {
        out_layout->active = true;
        float x0 = p.x / display.x;
        float y0 = p.y / display.y;
        float x1 = (p.x + sz.x) / display.x;
        float y1 = (p.y + sz.y) / display.y;
        out_layout->rect_uv = ImVec4(x0, y0, x1, y1);
    }

    ImGui::End();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);

    return res;
}

DialogResult render_bottom_dialog(const char* prompt,
                                 const std::string& body,
                                 const char* btn_a,
                                 const char* btn_b,
                                 ImFont* font_bold,
                                 DialogLayout* out_layout) {
    int sel = 1;
    return render_dialog("Dialog", prompt, body, btn_a, btn_b, &sel, 0, 1.0f, font_bold, out_layout);
}

} // namespace ui
} // namespace tradeboy
