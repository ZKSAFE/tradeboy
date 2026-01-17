#include "SpotOrderScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../utils/Math.h"
#include "../ui/MatrixTheme.h"
#include "../utils/Flash.h"

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

    flash_timer = 0;
    flash_btn_idx = -1;

    l1_flash_timer = 0;
    r1_flash_timer = 0;

    b_flash_timer = 0;
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

    if (st.l1_flash_timer > 0) st.l1_flash_timer--;
    if (st.r1_flash_timer > 0) st.r1_flash_timer--;
    if (st.b_flash_timer > 0) st.b_flash_timer--;

    // Handle flash animation
    if (st.flash_timer > 0) {
        st.flash_timer--;
        if (st.flash_timer == 0) {
            // Action trigger
            if (st.flash_btn_idx == 0) {
                // CONFIRM action
                st.close();
            } else if (st.flash_btn_idx == 1) {
                // X (Abort) action
                st.close();
            }
            st.flash_btn_idx = -1;
        }
        return true; // Block input during flash
    }

    if (tradeboy::utils::pressed(in.b, edges.prev.b)) {
        del_char(st.input);
        st.b_flash_timer = 6;
        return true;
    }

    if (tradeboy::utils::pressed(in.l1, edges.prev.l1)) {
        adjust_percent_step(st, -5);
        st.l1_flash_timer = 6;
    }
    if (tradeboy::utils::pressed(in.r1, edges.prev.r1)) {
        adjust_percent_step(st, +5);
        st.r1_flash_timer = 6;
    }

    if (tradeboy::utils::pressed(in.up, edges.prev.up)) {
        // Footer does not move up/down.
        if (st.footer_idx == -1) {
            st.grid_r = tradeboy::utils::clampi(st.grid_r - 1, 0, 3);
        }
    }
    if (tradeboy::utils::pressed(in.down, edges.prev.down)) {
        // Footer does not move up/down. DEL down should not enter footer.
        if (st.footer_idx == -1) {
            st.grid_r = tradeboy::utils::clampi(st.grid_r + 1, 0, 3);
        }
    }

    if (tradeboy::utils::pressed(in.left, edges.prev.left)) {
        if (st.footer_idx == 1) {
            // X can only move left to CONFIRM
            st.footer_idx = 0;
        } else if (st.footer_idx == 0) {
            // CONFIRM moves left into DEL
            st.footer_idx = -1;
            st.grid_r = 3;
            st.grid_c = 2;
        } else if (st.footer_idx == -1) {
            st.grid_c = tradeboy::utils::clampi(st.grid_c - 1, 0, 2);
        }
    }
    if (tradeboy::utils::pressed(in.right, edges.prev.right)) {
        if (st.footer_idx == 0) {
            // CONFIRM moves right to X
            st.footer_idx = 1;
        } else if (st.footer_idx == -1) {
            // Footer sits to the right of DEL.
            if (st.grid_r == 3 && st.grid_c == 2) {
                st.footer_idx = 0;
            } else {
                st.grid_c = tradeboy::utils::clampi(st.grid_c + 1, 0, 2);
            }
        }
    }

    if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
        static const char* keypad[4][3] = {
            {"1", "2", "3"},
            {"4", "5", "6"},
            {"7", "8", "9"},
            {".", "0", "DEL"},
        };

        if (st.footer_idx == 0) {
            st.flash_btn_idx = 0;
            st.flash_timer = 18; // Flash for 18 frames (3 * 6)
        } else if (st.footer_idx == 1) {
            st.flash_btn_idx = 1;
            st.flash_timer = 18;
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

    // --- Header ---
    float headerH = 54.0f;
    float x0 = p.x + pad;
    float x1 = p.x + size.x - pad;
    float y0 = p.y + pad;
    
    float headerDrawY = y0 - 10.0f;

    char title[64];
    std::snprintf(title, sizeof(title), "%s_%s", side_label(st.side), st.sym.c_str());
    float title_size = 42.0f; 
    
    ImU32 glowCol = (side_col & 0x00FFFFFF) | 0x40000000;
    if (font_bold) {
        dl->AddText(font_bold, title_size, ImVec2(x0 + 1, headerDrawY + 1), glowCol, title);
        dl->AddText(font_bold, title_size, ImVec2(x0 - 1, headerDrawY - 1), glowCol, title);
        dl->AddText(font_bold, title_size, ImVec2(x0, headerDrawY), side_col, title);
    } else {
        dl->AddText(ImVec2(x0 + 1, headerDrawY + 1), glowCol, title);
        dl->AddText(ImVec2(x0 - 1, headerDrawY - 1), glowCol, title);
        dl->AddText(ImVec2(x0, headerDrawY), side_col, title);
    }

    // Header Right: B DEL
    ImGui::SetWindowFontScale(0.6f);
    float backW = 24.0f;
    float backH = 18.0f; 
    const char* backLabel = "DEL";
    
    ImVec2 bSz = ImGui::CalcTextSize("B");
    
    ImGui::SetWindowFontScale(1.0f);
    ImVec2 blSz = ImGui::CalcTextSize(backLabel);
    
    float backX = x1 - backW - 8.0f - blSz.x;
    // Match SpotScreen header L1/R1 tag Y
    float navY = headerDrawY + 8.0f;
    float navSzY = ImGui::CalcTextSize("PERP | ACCOUNT").y;
    float tagH = 18.0f;
    float backY = navY + (navSzY - tagH) * 0.5f; 

    ImU32 backBg = (st.b_flash_timer > 0) ? text : dim;
    dl->AddRectFilled(ImVec2(backX, backY), ImVec2(backX + backW, backY + backH), backBg, 0.0f);
    
    ImGui::SetWindowFontScale(0.6f);
    dl->AddText(ImVec2(backX + (backW - bSz.x)*0.5f, backY + (backH - bSz.y)*0.5f), black, "B");
    ImGui::SetWindowFontScale(1.0f);
    
    dl->AddText(ImVec2(backX + backW + 6.0f, backY + (backH - blSz.y)*0.5f - 1.0f), dim, backLabel);

    float current_y = y0 + headerH; 
    dl->AddLine(ImVec2(x0, current_y - 16), ImVec2(x1, current_y - 16), dim, 2.0f);

    float y_main = current_y + 8.0f;

    // --- Layout Calcs ---
    float btnH = 40.0f;
    float footer_bottom_pad = 0.0f; 
    float footer_y = size.y - footer_bottom_pad - btnH; 
    
    float footer_gap = 10.0f; 
    
    float main_h = footer_y - footer_gap - y_main;
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
    
    char input_label[64];
    std::snprintf(input_label, sizeof(input_label), "PRICE: $%.2f", st.price);
    dl->AddText(ImVec2(left_x + 8.0f, input_y + 4.0f), dim, input_label);

    float input_font = 48.0f;
    ImVec2 in_sz = font_bold ? font_bold->CalcTextSizeA(input_font, FLT_MAX, 0.0f, st.input.c_str()) : ImGui::CalcTextSize(st.input.c_str());
    
    const char* cursor_char = "_";
    ImVec2 cs = font_bold ? font_bold->CalcTextSizeA(input_font, FLT_MAX, 0.0f, cursor_char) : ImGui::CalcTextSize(cursor_char);
    
    float total_w = in_sz.x + cs.x + 2.0f;
    float in_x = left_x + left_w - input_pad - total_w; 
    float in_y = input_y + (input_h - in_sz.y) * 0.5f;

    if (font_bold) {
        dl->AddText(font_bold, input_font, ImVec2(in_x, in_y), text, st.input.c_str());
    } else {
        dl->AddText(ImVec2(in_x, in_y), text, st.input.c_str());
    }

    {
        const bool blink_on = (std::fmod(ImGui::GetTime(), 1.0) < 0.5);
        if (blink_on) {
            float cx = in_x + in_sz.x + 2.0f;
            float cy = in_y;
            if (font_bold) {
                dl->AddText(font_bold, input_font, ImVec2(cx, cy), dim, cursor_char);
            } else {
                dl->AddText(ImVec2(cx, cy), dim, cursor_char);
            }
        }
    }

    double cur = parse_amount(st.input);
    char approx_usd[96];
    std::snprintf(approx_usd, sizeof(approx_usd), "\xE2\x89\x88 $%.2f USD", cur * st.price);
    dl->AddText(ImVec2(left_x + left_w - input_pad - ImGui::CalcTextSize(approx_usd).x, input_y + input_h - 28.0f), dim, approx_usd);

    // Left: keypad grid (grid-cols-3 gap-2)
    float keypad_gap = 8.0f;
    float keypad_y = input_y + input_h + 16.0f;
    float keypad_bottom = footer_y + btnH;
    float keypad_h = keypad_bottom - keypad_y; 
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

            // Removed outer glow/rect for focused key

            dl->AddRectFilled(ImVec2(cx0, cy0), ImVec2(cx0 + cell_w, cy0 + cell_h), bg);
            dl->AddRect(ImVec2(cx0, cy0), ImVec2(cx0 + cell_w, cy0 + cell_h), border, 0.0f, 0, 2.0f);

            const char* t = keypad[r][c];
            float key_font = 30.0f; // text-3xl
            ImVec2 ts = font_bold ? font_bold->CalcTextSizeA(key_font, FLT_MAX, 0.0f, t) : ImGui::CalcTextSize(t);
            float tx = cx0 + (cell_w - ts.x) * 0.5f;
            float ty = cy0 + (cell_h - ts.y) * 0.5f + 4.0f; // Center vertically (offset down)
            if (font_bold) {
                dl->AddText(font_bold, key_font, ImVec2(tx, ty), fg, t);
            } else {
                dl->AddText(ImVec2(tx, ty), fg, t);
            }
        }
    }

    // Right: progress panel + guide + buttons
    float right_gap = 12.0f;
    float guide_h = 48.0f;
    
    // Buttons are at bottom: footer_y
    // Guide is above buttons
    float guide_y = footer_y - right_gap - guide_h;
    
    // Allocation panel is from y_main to guide_y - gap
    float progress_y = y_main;
    float progress_h = guide_y - right_gap - progress_y;
    if (progress_h < 0) progress_h = 0;

    // Progress panel
    dl->AddRectFilled(ImVec2(right_x, progress_y), ImVec2(right_x + right_w, progress_y + progress_h), panel_bg);
    dl->AddRect(ImVec2(right_x, progress_y), ImVec2(right_x + right_w, progress_y + progress_h), dim, 0.0f, 0, 2.0f);

    const char* meter_title = "AVAILABLE";
    // Smaller font for allocation title
    float alloc_font = 16.0f;
    
    // Draw Title
    ImVec2 mt_sz = font_bold ? font_bold->CalcTextSizeA(alloc_font, FLT_MAX, 0.0f, meter_title) : ImGui::CalcTextSize(meter_title);
    float mt_x = right_x + (right_w - mt_sz.x) * 0.5f;
    float mt_y = progress_y + 14.0f;
    
    if (font_bold) {
        dl->AddText(font_bold, alloc_font, ImVec2(mt_x, mt_y), dim, meter_title);
    } else {
        dl->AddText(ImVec2(mt_x, mt_y), dim, meter_title);
    }

    // Draw Available Value
    char alloc_val[64];
    const char* alloc_ccy = (st.side == Side::Buy) ? "USDC" : st.sym.c_str();
    std::snprintf(alloc_val, sizeof(alloc_val), "%.2f %s", st.max_possible, alloc_ccy);
    // Centered below title
    ImVec2 av_sz = font_bold ? font_bold->CalcTextSizeA(alloc_font, FLT_MAX, 0.0f, alloc_val) : ImGui::CalcTextSize(alloc_val);
    if (font_bold) {
        dl->AddText(font_bold, alloc_font, ImVec2(right_x + (right_w - av_sz.x) * 0.5f, mt_y + mt_sz.y + 4.0f), text, alloc_val);
    } else {
        dl->AddText(ImVec2(right_x + (right_w - av_sz.x) * 0.5f, mt_y + mt_sz.y + 4.0f), text, alloc_val);
    }

    // Vertical bar (w-16)
    float bar_w = 64.0f;
    float bar_x = right_x + (right_w - bar_w) * 0.5f;
    float bar_y = mt_y + mt_sz.y + 4.0f + av_sz.y + 16.0f; // Space from text
    float bar_h = std::max(0.0f, progress_h - (bar_y - progress_y) - 44.0f);
    
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
    // guide_y already calculated
    dl->AddRectFilled(ImVec2(right_x, guide_y), ImVec2(right_x + right_w, guide_y + guide_h), input_panel_bg);
    dl->AddRect(ImVec2(right_x, guide_y), ImVec2(right_x + right_w, guide_y + guide_h), dim, 0.0f, 0, 2.0f);

    // L1/R1 Style - One Line
    float gy = guide_y + (guide_h - 18.0f) * 0.5f; // Center vertically
    
    // Helper to draw tag like SpotScreen
    auto draw_spot_tag = [&](float x_pos, float y_pos, const char* tag, const char* label, bool flash_on) {
        float tagW = 24.0f;
        float tagH = 18.0f;

        ImU32 tagBg = flash_on ? text : dim;
        dl->AddRectFilled(ImVec2(x_pos, y_pos), ImVec2(x_pos + tagW, y_pos + tagH), tagBg, 0.0f);
        
        ImVec2 tsz = ImGui::CalcTextSize(tag);
        float ttx = x_pos + (tagW - tsz.x) * 0.5f;
        float tty = y_pos + (tagH - tsz.y) * 0.5f;
        dl->AddText(ImVec2(ttx, tty), black, tag);
        
        dl->AddText(ImVec2(x_pos + tagW + 6.0f, y_pos + 1.0f), text, label);
        return x_pos + tagW + 6.0f + ImGui::CalcTextSize(label).x; 
    };

    ImGui::SetWindowFontScale(0.6f);
    // Calculate total width to center
    // L1 -5%  R1 +5%
    // Width ~ 24 + 6 + 20 + 10(gap) + 24 + 6 + 20
    float l1_w = 24 + 6 + ImGui::CalcTextSize("-5%").x;
    float r1_w = 24 + 6 + ImGui::CalcTextSize("+5%").x;
    float gap_lr = 48.0f;
    float total_lr_w = l1_w + gap_lr + r1_w;
    
    float start_x = right_x + (right_w - total_lr_w) * 0.5f;

    draw_spot_tag(start_x, gy, "L1", "-5%", st.l1_flash_timer > 0);
    draw_spot_tag(start_x + l1_w + gap_lr, gy, "R1", "+5%", st.r1_flash_timer > 0);
    ImGui::SetWindowFontScale(1.0f);

    // --- Footer actions ---
    // Buttons right aligned: [  CONFIRM  ] [ X ]
    // CONFIRM left aligns with Right Panel left edge (right_x)
    float btnFontSize = 20.0f;
    float btn_gap = 12.0f;
    float abort_w = 40.0f; // Small X button
    
    // confirm_x starts at right_x.
    // confirm_w reduced slightly, so gap between CONFIRM and X increases
    float confirm_x = right_x;
    float confirm_w = right_w - btn_gap - abort_w; // Revert width reduction
    float abort_x = x1 - abort_w; // X right aligned to panel end

        // Confirm
    {
        bool focused = (st.footer_idx == 0);
        bool flashing = (st.flash_btn_idx == 0 && st.flash_timer > 0);

        const int blinkPeriod = 6;
        const int blinkOn = 3;
        bool flash_on = false;
        if (flashing) {
            flash_on = tradeboy::utils::blink_on(st.flash_timer, blinkPeriod, blinkOn);
        }
        
        ImU32 bg, fg, border;
        
        if (focused && !flashing) {
            bg = text;
            fg = black;
            border = text;
        } else if (flashing && !flash_on) {
            bg = IM_COL32(0,0,0,0);
            fg = text;
            border = text;
        } else {
            bg = flashing ? text : IM_COL32(0,0,0,0);
            fg = flashing ? black : (focused ? text : dim);
            border = focused ? text : dim;
        }
        
        dl->AddRectFilled(ImVec2(confirm_x, footer_y), ImVec2(confirm_x + confirm_w, footer_y + btnH), bg, 0.0f);
        dl->AddRect(ImVec2(confirm_x, footer_y), ImVec2(confirm_x + confirm_w, footer_y + btnH), border, 0.0f, 0, 2.0f);

        const char* label = "CONFIRM";
        ImVec2 ts = font_bold ? font_bold->CalcTextSizeA(btnFontSize, FLT_MAX, 0.0f, label) : ImGui::CalcTextSize(label);
        float tx = confirm_x + (confirm_w - ts.x) * 0.5f;
        float ty = footer_y + (btnH - ts.y) * 0.5f;
        if (font_bold) {
            dl->AddText(font_bold, btnFontSize, ImVec2(tx, ty), fg, label);
        } else {
            dl->AddText(ImVec2(tx, ty), fg, label);
        }
    }

    // Abort (X)
    {
        bool focused = (st.footer_idx == 1);
        bool flashing = (st.flash_btn_idx == 1 && st.flash_timer > 0);

        const int blinkPeriod = 6;
        const int blinkOn = 3;
        bool flash_on = false;
        if (flashing) {
            flash_on = tradeboy::utils::blink_on(st.flash_timer, blinkPeriod, blinkOn);
        }
        
        ImU32 bg, fg, border;
        
        if (focused && !flashing) {
            bg = alert;
            fg = black;
            border = alert;
        } else if (flashing && !flash_on) {
            bg = IM_COL32(0,0,0,0);
            fg = alert;
            border = alert;
        } else {
            bg = flashing ? alert : IM_COL32(0,0,0,0);
            fg = flashing ? black : (focused ? alert : dim);
            border = focused ? alert : dim;
        }
        
        dl->AddRectFilled(ImVec2(abort_x, footer_y), ImVec2(abort_x + abort_w, footer_y + btnH), bg, 0.0f);
        dl->AddRect(ImVec2(abort_x, footer_y), ImVec2(abort_x + abort_w, footer_y + btnH), border, 0.0f, 0, 2.0f);

        const char* label = "X";
        ImVec2 ts = font_bold ? font_bold->CalcTextSizeA(btnFontSize, FLT_MAX, 0.0f, label) : ImGui::CalcTextSize(label);
        float tx = abort_x + (abort_w - ts.x) * 0.5f;
        float ty = footer_y + (btnH - ts.y) * 0.5f;
        if (font_bold) {
            dl->AddText(font_bold, btnFontSize, ImVec2(tx, ty), fg, label);
        } else {
            dl->AddText(ImVec2(tx, ty), fg, label);
        }
    }

    // No B: BACK text

    ImGui::End();
}

} // namespace tradeboy::spotOrder
