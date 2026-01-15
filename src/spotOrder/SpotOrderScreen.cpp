#include "SpotOrderScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../utils/Math.h"
#include "../ui/MatrixTheme.h"

namespace tradeboy::spotOrder {

static const char* side_label(Side s) {
    return (s == Side::Buy) ? "BUY" : "SELL";
}

void SpotOrderState::open_with(const tradeboy::model::SpotRow& row, Side in_side, double in_max_possible) {
    open = true;
    side = in_side;
    sym = row.sym;
    price = row.price;
    max_possible = std::max(0.0, in_max_possible);

    input = "0";
    grid_r = 0;
    grid_c = 1;
    footer_idx = -1;
}

void SpotOrderState::close() {
    open = false;
}

static double parse_amount(const std::string& s) {
    if (s.empty() || s == ".") return 0.0;
    try {
        double v = std::stod(s);
        if (!std::isfinite(v) || v < 0.0) return 0.0;
        return v;
    } catch (...) {
        return 0.0;
    }
}

static void del_char(std::string& s) {
    if (s.size() <= 1) {
        s = "0";
        return;
    }
    s.pop_back();
    if (s.empty() || s == "-") s = "0";
}

static void append_char(std::string& s, char ch) {
    if (s == "0" && ch != '.') s.clear();
    if (ch == '.') {
        if (s.find('.') != std::string::npos) return;
        if (s.empty()) s = "0";
    }
    if (s.size() >= 10) return;
    s.push_back(ch);
}

static void set_amount_percent(SpotOrderState& st, int percent) {
    percent = tradeboy::utils::clampi(percent, 0, 100);
    double v = (st.max_possible * (double)percent) / 100.0;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    std::string out = buf;
    while (out.size() > 1 && out.back() == '0') out.pop_back();
    if (!out.empty() && out.back() == '.') out.pop_back();
    st.input = out.empty() ? "0" : out;
}

static int current_percent(const SpotOrderState& st) {
    if (st.max_possible <= 0.0) return 0;
    double v = parse_amount(st.input);
    double p = (v / st.max_possible) * 100.0;
    int ip = (int)std::floor(p + 1e-9);
    return tradeboy::utils::clampi(ip, 0, 100);
}

static void adjust_percent_step(SpotOrderState& st, int delta) {
    int p = current_percent(st);
    p = tradeboy::utils::clampi(p + delta, 0, 100);
    set_amount_percent(st, p);
}

bool handle_input(SpotOrderState& st, const tradeboy::app::InputState& in, const tradeboy::app::EdgeState& edges) {
    if (!st.open) return false;

    if (tradeboy::utils::pressed(in.b, edges.prev.b)) {
        st.close();
        return true;
    }

    if (tradeboy::utils::pressed(in.l1, edges.prev.l1)) adjust_percent_step(st, -5);
    if (tradeboy::utils::pressed(in.r1, edges.prev.r1)) adjust_percent_step(st, +5);

    if (tradeboy::utils::pressed(in.up, edges.prev.up)) {
        if (st.footer_idx != -1) {
            st.footer_idx = -1;
        } else {
            st.grid_r = tradeboy::utils::clampi(st.grid_r - 1, 0, 3);
        }
    }
    if (tradeboy::utils::pressed(in.down, edges.prev.down)) {
        if (st.grid_r == 3) {
            st.footer_idx = 0;
        } else {
            st.grid_r = tradeboy::utils::clampi(st.grid_r + 1, 0, 3);
        }
    }

    if (tradeboy::utils::pressed(in.left, edges.prev.left)) {
        if (st.footer_idx == 1) st.footer_idx = 0;
        else if (st.footer_idx == -1) st.grid_c = tradeboy::utils::clampi(st.grid_c - 1, 0, 2);
    }
    if (tradeboy::utils::pressed(in.right, edges.prev.right)) {
        if (st.footer_idx == 0) st.footer_idx = 1;
        else if (st.footer_idx == -1) st.grid_c = tradeboy::utils::clampi(st.grid_c + 1, 0, 2);
    }

    if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
        static const char* keypad[4][3] = {
            {"1", "2", "3"},
            {"4", "5", "6"},
            {"7", "8", "9"},
            {".", "0", "DEL"},
        };

        if (st.footer_idx == 0) {
            st.close();
        } else if (st.footer_idx == 1) {
            st.close();
        } else {
            const char* key = keypad[st.grid_r][st.grid_c];
            if (key[0] == 'D') {
                del_char(st.input);
            } else {
                append_char(st.input, key[0]);
            }
        }
        return true;
    }

    return true;
}

void render(SpotOrderState& st, ImFont* font_bold) {
    if (!st.open) return;

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize, ImGuiCond_Always);

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoBackground;

    ImGui::Begin("SpotOrder", nullptr, wflags);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();

    const float pad = 16.0f;   // p-4
    const float gap = 24.0f;   // gap-6

    ImU32 dim = MatrixTheme::DIM;
    ImU32 text = MatrixTheme::TEXT;
    ImU32 alert = MatrixTheme::ALERT;
    ImU32 black = MatrixTheme::BLACK;
    ImU32 white = IM_COL32(255, 255, 255, 255);

    ImU32 side_col = (st.side == Side::Buy) ? text : alert;
    ImU32 input_panel_bg = IM_COL32(0, 59, 0, 102);   // MatrixTheme::DARK @ 40%
    ImU32 panel_bg = IM_COL32(0, 0, 0, 153);          // black @ 60%
    ImU32 keypad_bg = IM_COL32(0, 0, 0, 204);         // black @ 80%

    // --- Header (border-b-4 pb-2 mb-4) ---
    float x0 = p.x + pad;
    float x1 = p.x + size.x - pad;
    float y0 = p.y + pad;

    char title[64];
    std::snprintf(title, sizeof(title), "%s_ORDER: %s", side_label(st.side), st.sym.c_str());
    float title_size = 36.0f; // approx text-4xl
    if (font_bold) {
        dl->AddText(font_bold, title_size, ImVec2(x0, y0), side_col, title);
    } else {
        dl->AddText(ImVec2(x0, y0), side_col, title);
    }

    char price_line[96];
    std::snprintf(price_line, sizeof(price_line), "CURR_PRICE: $%.2f", st.price);
    dl->AddText(ImVec2(x0, y0 + 44), dim, price_line);

    // Right aligned funds
    const float funds_w = 220.0f;
    float fx = x1 - funds_w;
    dl->AddText(ImVec2(fx, y0 + 4), dim, "AVAILABLE_FUNDS");
    char avail[96];
    std::snprintf(avail, sizeof(avail), "%.4f %s", st.max_possible, (st.side == Side::Buy) ? "USD" : st.sym.c_str());
    if (font_bold) {
        dl->AddText(font_bold, 22.0f, ImVec2(fx, y0 + 26), text, avail);
    } else {
        dl->AddText(ImVec2(fx, y0 + 26), text, avail);
    }

    float header_div_y = y0 + 68.0f; // border line position
    dl->AddLine(ImVec2(x0, header_div_y), ImVec2(x1, header_div_y), dim, 4.0f);

    // Main content starts after header + mb-4
    float y_main = header_div_y + 16.0f;

    // --- Main grid: 3 cols, gap-6 ---
    float main_h = size.y - pad - y_main - 16.0f - 56.0f; // leave mt-4 + footer h-14
    if (main_h < 0) main_h = 0;

    float col_w = (size.x - 2 * pad - 2 * gap) / 3.0f;
    float left_w = col_w * 2.0f + gap; // col-span-2
    float right_w = col_w;             // col-span-1

    float left_x = x0;
    float right_x = x0 + left_w + gap;

    // Left: input display (h-32, p-3, mb-4)
    float input_h = 128.0f;
    float input_pad = 12.0f;
    float input_y = y_main;

    dl->AddRectFilled(ImVec2(left_x, input_y), ImVec2(left_x + left_w, input_y + input_h), input_panel_bg);
    dl->AddRect(ImVec2(left_x, input_y), ImVec2(left_x + left_w, input_y + input_h), dim, 0.0f, 0, 2.0f);
    dl->AddText(ImVec2(left_x + 8.0f, input_y + 4.0f), dim, "INPUT_BUFFER");

    // Big input (text-6xl) right aligned
    float input_font = 48.0f;
    ImVec2 in_sz = font_bold ? font_bold->CalcTextSizeA(input_font, FLT_MAX, 0.0f, st.input.c_str()) : ImGui::CalcTextSize(st.input.c_str());
    float in_x = left_x + left_w - input_pad - in_sz.x;
    float in_y = input_y + 26.0f;
    if (font_bold) {
        dl->AddText(font_bold, input_font, ImVec2(in_x, in_y), text, st.input.c_str());
    } else {
        dl->AddText(ImVec2(in_x, in_y), text, st.input.c_str());
    }

    // Blinking cursor underscore
    {
        const bool blink_on = (std::fmod(ImGui::GetTime(), 1.0) < 0.5);
        if (blink_on) {
            const char* cursor = "_";
            ImVec2 cs = font_bold ? font_bold->CalcTextSizeA(input_font, FLT_MAX, 0.0f, cursor) : ImGui::CalcTextSize(cursor);
            float cx = in_x + in_sz.x + 2.0f;
            float cy = in_y;
            if (font_bold) {
                dl->AddText(font_bold, input_font, ImVec2(cx, cy), dim, cursor);
            } else {
                dl->AddText(ImVec2(cx, cy), dim, cursor);
            }
        }
    }

    // ≈ USD line
    double cur = parse_amount(st.input);
    char approx_usd[96];
    std::snprintf(approx_usd, sizeof(approx_usd), "\xE2\x89\x88 $%.2f USD", cur * st.price);
    dl->AddText(ImVec2(left_x + left_w - input_pad - ImGui::CalcTextSize(approx_usd).x, input_y + input_h - 28.0f), dim, approx_usd);

    // Left: keypad grid (grid-cols-3 gap-2)
    float keypad_gap = 8.0f;
    float keypad_y = input_y + input_h + 16.0f;
    float keypad_h = y_main + main_h - keypad_y;
    if (keypad_h < 0) keypad_h = 0;

    float cell_w = (left_w - 2 * keypad_gap) / 3.0f;
    float cell_h = (keypad_h - 3 * keypad_gap) / 4.0f;

    static const char* keypad[4][3] = {
        {"1", "2", "3"},
        {"4", "5", "6"},
        {"7", "8", "9"},
        {".", "0", "DEL"},
    };

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 3; c++) {
            float cx0 = left_x + c * (cell_w + keypad_gap);
            float cy0 = keypad_y + r * (cell_h + keypad_gap);
            bool focused = (st.footer_idx == -1 && st.grid_r == r && st.grid_c == c);

            ImU32 bg = focused ? text : keypad_bg;
            ImU32 fg = focused ? black : text;
            ImU32 border = focused ? white : dim;

            if (focused) {
                ImU32 glow = IM_COL32(0, 255, 65, 80);
                dl->AddRect(ImVec2(cx0 - 3, cy0 - 3), ImVec2(cx0 + cell_w + 3, cy0 + cell_h + 3), glow, 0.0f, 0, 2.0f);
            }

            dl->AddRectFilled(ImVec2(cx0, cy0), ImVec2(cx0 + cell_w, cy0 + cell_h), bg);
            dl->AddRect(ImVec2(cx0, cy0), ImVec2(cx0 + cell_w, cy0 + cell_h), border, 0.0f, 0, 2.0f);

            const char* t = keypad[r][c];
            float key_font = 30.0f; // text-3xl
            ImVec2 ts = font_bold ? font_bold->CalcTextSizeA(key_font, FLT_MAX, 0.0f, t) : ImGui::CalcTextSize(t);
            float tx = cx0 + (cell_w - ts.x) * 0.5f;
            float ty = cy0 + (cell_h - ts.y) * 0.5f + 6.0f; // pt-[6px]
            if (font_bold) {
                dl->AddText(font_bold, key_font, ImVec2(tx, ty), fg, t);
            } else {
                dl->AddText(ImVec2(tx, ty), fg, t);
            }
        }
    }

    // Right: progress panel + guide (gap-4)
    float right_gap = 16.0f;
    float guide_h = 72.0f;
    float progress_y = y_main;
    float progress_h = main_h - right_gap - guide_h;
    if (progress_h < 0) progress_h = 0;

    // Progress panel
    dl->AddRectFilled(ImVec2(right_x, progress_y), ImVec2(right_x + right_w, progress_y + progress_h), panel_bg);
    dl->AddRect(ImVec2(right_x, progress_y), ImVec2(right_x + right_w, progress_y + progress_h), dim, 0.0f, 0, 2.0f);

    const char* meter_title = "ALLOCATION_METER";
    ImVec2 mt_sz = ImGui::CalcTextSize(meter_title);
    dl->AddText(ImVec2(right_x + (right_w - mt_sz.x) * 0.5f, progress_y + 14.0f), dim, meter_title);

    // Vertical bar (w-16)
    float bar_w = 64.0f;
    float bar_x = right_x + (right_w - bar_w) * 0.5f;
    float bar_y = progress_y + 40.0f;
    float bar_h = std::max(0.0f, progress_h - 40.0f - 44.0f);
    dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h), MatrixTheme::BLACK);
    dl->AddRect(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h), dim, 0.0f, 0, 2.0f);

    float pct = 0.0f;
    if (st.max_possible > 0.0) pct = (float)std::min(1.0, cur / st.max_possible);
    float fill_h = bar_h * pct;
    dl->AddRectFilled(ImVec2(bar_x, bar_y + (bar_h - fill_h)), ImVec2(bar_x + bar_w, bar_y + bar_h), text);

    // Tick lines (approx like design)
    for (int i = 0; i <= 4; i++) {
        float ty = bar_y + (bar_h * (float)i) / 4.0f;
        float thick = (i % 2 == 0) ? 2.0f : 1.0f;
        dl->AddLine(ImVec2(bar_x, ty), ImVec2(bar_x + bar_w, ty), IM_COL32(0, 255, 65, 100), thick);
    }

    char pct_s[16];
    std::snprintf(pct_s, sizeof(pct_s), "%d%%", (int)std::round(pct * 100.0f));
    float pct_font = 30.0f;
    ImVec2 ps = font_bold ? font_bold->CalcTextSizeA(pct_font, FLT_MAX, 0.0f, pct_s) : ImGui::CalcTextSize(pct_s);
    float pct_x = right_x + (right_w - ps.x) * 0.5f;
    float pct_y = progress_y + progress_h - 34.0f;
    if (font_bold) {
        dl->AddText(font_bold, pct_font, ImVec2(pct_x, pct_y), text, pct_s);
    } else {
        dl->AddText(ImVec2(pct_x, pct_y), text, pct_s);
    }

    // Guide panel (p-2, text-[11px])
    float guide_y = progress_y + progress_h + right_gap;
    dl->AddRectFilled(ImVec2(right_x, guide_y), ImVec2(right_x + right_w, guide_y + guide_h), input_panel_bg);
    dl->AddRect(ImVec2(right_x, guide_y), ImVec2(right_x + right_w, guide_y + guide_h), dim, 0.0f, 0, 2.0f);

    float gy = guide_y + 10.0f;
    auto draw_lr = [&](float y, const char* tag, const char* label) {
        float tag_w = 24.0f;
        float tag_h = 16.0f;
        float tx = right_x + 10.0f;
        dl->AddRectFilled(ImVec2(tx, y), ImVec2(tx + tag_w, y + tag_h), dim, 2.0f);

        ImVec2 tsz = ImGui::CalcTextSize(tag);
        float ttx = tx + (tag_w - tsz.x) * 0.5f;
        float tty = y + (tag_h - tsz.y) * 0.5f;
        dl->AddText(ImVec2(ttx, tty), black, tag);

        dl->AddText(ImVec2(tx + tag_w + 10.0f, y + 0.0f), text, label);
    };
    draw_lr(gy, "L1", "DECREASE 5%");
    draw_lr(gy + 28.0f, "R1", "INCREASE 5%");

    // --- Footer actions (mt-4, grid-cols-3 gap-6, h-14) ---
    float footer_h = 56.0f;
    float footer_y = p.y + size.y - pad - footer_h;

    float confirm_w = left_w;
    float abort_w = right_w;

    // Confirm (col-span-2)
    {
        bool focused = (st.footer_idx == 0);
        ImU32 bg = focused ? text : MatrixTheme::BLACK;
        ImU32 fg = focused ? black : text;
        ImU32 border = focused ? text : dim;

        if (focused) {
            ImU32 glow = IM_COL32(0, 255, 65, 80);
            dl->AddRect(ImVec2(left_x - 3, footer_y - 3), ImVec2(left_x + confirm_w + 3, footer_y + footer_h + 3), glow, 0.0f, 0, 2.0f);
        }

        dl->AddRectFilled(ImVec2(left_x, footer_y), ImVec2(left_x + confirm_w, footer_y + footer_h), bg);
        dl->AddRect(ImVec2(left_x, footer_y), ImVec2(left_x + confirm_w, footer_y + footer_h), border, 0.0f, 0, 2.0f);

        const char* label = "CONFIRM_EXEC";
        float font_sz = 20.0f;
        const bool blink_on = (std::fmod(ImGui::GetTime(), 1.0) < 0.5);
        const char* prefix = (focused && blink_on) ? "> " : "";

        char full_label[64];
        std::snprintf(full_label, sizeof(full_label), "%s%s", prefix, label);

        ImVec2 ts = font_bold ? font_bold->CalcTextSizeA(font_sz, FLT_MAX, 0.0f, full_label) : ImGui::CalcTextSize(full_label);
        float tx = left_x + (confirm_w - ts.x) * 0.5f;
        float ty = footer_y + (footer_h - ts.y) * 0.5f + 7.0f;
        if (font_bold) {
            dl->AddText(font_bold, font_sz, ImVec2(tx, ty), fg, full_label);
        } else {
            dl->AddText(ImVec2(tx, ty), fg, full_label);
        }
    }

    // Abort (col-span-1)
    {
        bool focused = (st.footer_idx == 1);
        ImU32 bg = focused ? white : MatrixTheme::BLACK;
        ImU32 fg = focused ? black : alert;
        ImU32 border = focused ? white : alert;
        if (!focused) {
            bg = IM_COL32(0, 0, 0, 255);
            fg = IM_COL32(255, 0, 85, 160);
            border = IM_COL32(255, 0, 85, 160);
        }

        if (focused) {
            ImU32 glow = IM_COL32(255, 0, 85, 80);
            dl->AddRect(ImVec2(right_x - 3, footer_y - 3), ImVec2(right_x + abort_w + 3, footer_y + footer_h + 3), glow, 0.0f, 0, 2.0f);
        }

        dl->AddRectFilled(ImVec2(right_x, footer_y), ImVec2(right_x + abort_w, footer_y + footer_h), bg);
        dl->AddRect(ImVec2(right_x, footer_y), ImVec2(right_x + abort_w, footer_y + footer_h), border, 0.0f, 0, 2.0f);

        const char* label = "ABORT";
        float font_sz = 20.0f;
        const bool blink_on = (std::fmod(ImGui::GetTime(), 1.0) < 0.5);
        const char* prefix = (focused && blink_on) ? "> " : "";

        char full_label[32];
        std::snprintf(full_label, sizeof(full_label), "%s%s", prefix, label);

        ImVec2 ts = font_bold ? font_bold->CalcTextSizeA(font_sz, FLT_MAX, 0.0f, full_label) : ImGui::CalcTextSize(full_label);
        float tx = right_x + (abort_w - ts.x) * 0.5f;
        float ty = footer_y + (footer_h - ts.y) * 0.5f + 7.0f;
        if (font_bold) {
            dl->AddText(font_bold, font_sz, ImVec2(tx, ty), fg, full_label);
        } else {
            dl->AddText(ImVec2(tx, ty), fg, full_label);
        }
    }

    dl->AddText(ImVec2(x1 - 80.0f, footer_y - 18.0f), dim, "B: BACK");

    ImGui::End();
}

} // namespace tradeboy::spotOrder
