#include "ui/Dialog.h"

#include <cstdio>
#include <unordered_map>

#include "ui/MatrixTheme.h"
#include "utils/Flash.h"
#include "utils/Typewriter.h"

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

    struct DialogTwEntry {
        tradeboy::utils::TypewriterState tw;
        float last_open_t = 0.0f;
        bool started_when_open = false;
        bool has_last = false;
    };
    static std::unordered_map<std::string, DialogTwEntry> tw_map;

    const char* key_c = id ? id : "Dialog";
    DialogTwEntry& ent = tw_map[std::string(key_c)];
    if (!ent.has_last) {
        ent.has_last = true;
        ent.last_open_t = open_anim_t;
    }
    if (open_anim_t < ent.last_open_t) {
        ent.tw.last_text.clear();
        ent.tw.start_time = 0.0;
        ent.started_when_open = false;
    }
    ent.last_open_t = open_anim_t;

    std::string shown_text;
    if (open_anim_t >= 1.0f) {
        if (!ent.started_when_open) {
            ent.tw.last_text.clear();
            ent.tw.start_time = ImGui::GetTime();
            ent.started_when_open = true;
        }
        shown_text = tradeboy::utils::typewriter_shown(ent.tw, body, ImGui::GetTime(), 35.0);
    } else {
        ent.started_when_open = false;
        shown_text.clear();
    }

    // Dim layer: must be ABOVE the underlying UI (including its text), but BELOW the dialog window.
    // Using a full-screen ImGui window ensures correct z-order.
    {
        // Keep a stable window id to avoid flicker when switching between dialogs.
        const char* dim_id = "##DialogDimLayer";
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

    {
        ImFont* font = ImGui::GetFont();
        const float font_size = ImGui::GetFontSize();
        const float line_h = ImGui::GetTextLineHeight();
        const float scale_a = (font && font->LegacySize > 0.0f) ? (font_size / font->LegacySize) : 1.0f;

        // Older ImGui builds used in this project may not expose ImFont::Descent/FontSize.
        // Use a conservative approximation to avoid descender clipping during dynamic growth.
        float descent_px = font_size * 0.25f;
        if (descent_px < 3.0f) descent_px = 3.0f;

        const char* pr = prompt ? prompt : "";
        ImVec2 pSz = font ? font->CalcTextSizeA(scale_a, FLT_MAX, 0.0f, pr) : ImGui::CalcTextSize(pr);

        float space_w = ImGui::CalcTextSize(" ").x;
        const float body_pad_x = 12.0f + space_w;
        const float x_body_rel_first = 20.0f + pSz.x + body_pad_x;
        float max_w_first = (winSize.x - 20.0f) - x_body_rel_first;
        float max_w_next = (winSize.x - 20.0f) - 20.0f;
        if (max_w_first < 40.0f) max_w_first = 40.0f;
        if (max_w_next < 40.0f) max_w_next = 40.0f;

        int lines = 0;
        bool first_line = true;
        const char* s = shown_text.c_str();
        const char* end = s + shown_text.size();
        while (s < end) {
            const char* nl = (const char*)memchr(s, '\n', (size_t)(end - s));
            const char* seg_end = nl ? nl : end;
            const char* line = s;
            while (line < seg_end) {
                const float w = first_line ? max_w_first : max_w_next;
                const char* wrap = font->CalcWordWrapPositionA(scale_a, line, seg_end, w);
                if (wrap == line) wrap = line + 1;
                lines++;
                first_line = false;
                line = wrap;
                while (line < seg_end && (*line == ' ' || *line == '\t')) line++;
            }
            if (nl) s = nl + 1;
            else break;
        }

        if (lines < 1) lines = 1;

        // Include a small descent buffer so the last visible line doesn't get clipped during dynamic growth.
        float desired_h = 96.0f + (float)lines * line_h + descent_px + 2.0f;
        if (desired_h < 160.0f) desired_h = 160.0f;
        if (desired_h > 450.0f) desired_h = 450.0f;
        winSize.y = desired_h;
    }

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

    const float prompt_y = contentTop + 10.0f;
    const float x_prompt = p.x + 20.0f;
    const float y_text = prompt_y;
    const char* pr = prompt ? prompt : "";
    dl->AddText(ImVec2(x_prompt, y_text), MatrixTheme::TEXT, pr);

    // Wrapped, multi-line body text (preserves typewriter reveal by wrapping only the shown substring).
    {
        ImFont* font = ImGui::GetFont();
        const float font_size = ImGui::GetFontSize();
        const float line_h = ImGui::GetTextLineHeight();
        const float scale_a = (font && font->LegacySize > 0.0f) ? (font_size / font->LegacySize) : 1.0f;
        ImVec2 pSz = font ? font->CalcTextSizeA(scale_a, FLT_MAX, 0.0f, pr) : ImGui::CalcTextSize(pr);

        float space_w = ImGui::CalcTextSize(" ").x;
        const float body_pad_x = 12.0f + space_w;
        const float x_body_first = x_prompt + pSz.x + body_pad_x;
        const float x_body_next = x_prompt;
        float y = y_text;
        float max_w_first = (p.x + sz.x - 20.0f) - x_body_first;
        float max_w_next = (p.x + sz.x - 20.0f) - x_body_next;
        if (max_w_first < 40.0f) max_w_first = 40.0f;
        if (max_w_next < 40.0f) max_w_next = 40.0f;

        // Add bottom padding so glyph descenders don't get clipped.
        // (Can't use ImFont::Descent in this ImGui version.)
        float descent_px = font_size * 0.25f;
        if (descent_px < 3.0f) descent_px = 3.0f;
        const float min_body_to_buttons = 14.0f;
        const float body_max_y = footerY - min_body_to_buttons;
        const ImVec2 clip_min(x_prompt, y - 2.0f);
        const ImVec2 clip_max(p.x + sz.x - 20.0f, body_max_y + descent_px + 8.0f);
        int max_lines = (int)((clip_max.y - y) / line_h);
        if (max_lines < 1) max_lines = 1;
        int drawn_lines = 0;
        bool truncated = false;

        bool first_line = true;

        dl->PushClipRect(clip_min, clip_max, true);

        const char* s = shown_text.c_str();
        const char* end = s + shown_text.size();
        while (s < end) {
            // Handle explicit newline.
            const char* nl = (const char*)memchr(s, '\n', (size_t)(end - s));
            const char* seg_end = nl ? nl : end;

            const char* line = s;
            while (line < seg_end) {
                const float w = first_line ? max_w_first : max_w_next;
                const char* wrap = font->CalcWordWrapPositionA(scale_a, line, seg_end, w);
                if (wrap == line) {
                    // Safety: ensure progress even if a single glyph exceeds max width.
                    wrap = line + 1;
                }

                if (drawn_lines >= max_lines) {
                    truncated = true;
                    line = seg_end;
                    s = end;
                    break;
                }
                if (drawn_lines == max_lines - 1) {
                    if (wrap < seg_end || nl) {
                        truncated = true;
                        line = seg_end;
                        s = end;
                        break;
                    }
                }

                std::string part(line, wrap);
                dl->AddText(ImVec2(first_line ? x_body_first : x_body_next, y), MatrixTheme::TEXT, part.c_str());
                y += line_h;
                drawn_lines++;
                first_line = false;
                line = wrap;
                while (line < seg_end && (*line == ' ' || *line == '\t')) line++;
            }

            if (nl) {
                // Newline: move to next line.
                s = nl + 1;
            } else {
                break;
            }
        }

        dl->PopClipRect();

        (void)truncated;
    }

    const float btnH = 40.0f;
    const float btnGap = 16.0f;
    const float btnPadX = 18.0f;
    const float btnMinW = 96.0f;

    auto measure_btn_w = [&](const char* txt) -> float {
        if (!txt) return btnMinW;
        float btnFontSize = 20.0f;
        float tw = 0.0f;
        if (font_bold) {
            ImVec2 tSz = font_bold->CalcTextSizeA(btnFontSize, FLT_MAX, 0.0f, txt);
            tw = tSz.x;
        } else {
            ImVec2 tSz = ImGui::CalcTextSize(txt);
            tw = tSz.x;
        }
        float w = tw + btnPadX * 2.0f;
        if (w < btnMinW) w = btnMinW;
        return w;
    };

    float btnY = footerY;

    const bool single_btn = (!btn_a || btn_a[0] == 0);
    float btnWA = single_btn ? 0.0f : measure_btn_w(btn_a);
    float btnWB = measure_btn_w(btn_b);

    const float avail_w = sz.x - 32.0f;
    if (!single_btn) {
        float total = btnWA + btnWB + btnGap;
        if (total > avail_w) {
            float half = (avail_w - btnGap) * 0.5f;
            if (btnWA > half) btnWA = half;
            if (btnWB > half) btnWB = half;
        }
    } else {
        if (btnWB > avail_w) btnWB = avail_w;
    }

    float btnBX = p.x + sz.x - 16.0f - btnWB;
    float btnAX = btnBX - btnGap - btnWA;
    if (single_btn) {
        btnBX = p.x + (sz.x - btnWB) * 0.5f;
        btnAX = btnBX;
    }

    int sel = 1;
    if (io_selected_btn) sel = *io_selected_btn;
    if (single_btn) {
        sel = 1;
    } else {
        if (sel < 0) sel = 0;
        if (sel > 1) sel = 1;
    }

    bool flash_on = false;
    if (flash_frames > 0) {
        flash_on = tradeboy::utils::blink_on(flash_frames, 6, 3);
    }

    auto draw_btn = [&](float x, float w, const char* txt, bool selected) {
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
            dl->AddRectFilled(ImVec2(x, btnY), ImVec2(x + w, btnY + btnH), fill, 0.0f);
        }
        dl->AddRect(ImVec2(x, btnY), ImVec2(x + w, btnY + btnH), border, 0.0f, 0, 2.0f);

        float btnFontSize = 20.0f;
        if (font_bold) {
            ImVec2 tSz = font_bold->CalcTextSizeA(btnFontSize, FLT_MAX, 0.0f, txt);
            dl->AddText(font_bold,
                        btnFontSize,
                        ImVec2(x + (w - tSz.x) * 0.5f, btnY + (btnH - tSz.y) * 0.5f),
                        text,
                        txt);
        } else {
            ImVec2 tSz = ImGui::CalcTextSize(txt);
            dl->AddText(ImVec2(x + (w - tSz.x) * 0.5f, btnY + (btnH - tSz.y) * 0.5f), text, txt);
        }
    };

    if (!single_btn) {
        draw_btn(btnAX, btnWA, btn_a, sel == 0);
    }
    draw_btn(btnBX, btnWB, btn_b, sel == 1);

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
