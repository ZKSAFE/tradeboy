#include "NumberInputModal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../utils/Math.h"
#include "../ui/MatrixTheme.h"
#include "../ui/Dialog.h"
#include "../utils/Flash.h"

namespace tradeboy::ui {

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

static void set_amount_percent(NumberInputState& st, int percent) {
    percent = tradeboy::utils::clampi(percent, 0, 100);
    double v = (st.config.max_value * (double)percent) / 100.0;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    std::string out = buf;
    while (out.size() > 1 && out.back() == '0') out.pop_back();
    if (!out.empty() && out.back() == '.') out.pop_back();
    st.input = out.empty() ? "0" : out;
}

static int current_percent(const NumberInputState& st) {
    if (st.config.max_value <= 0.0) return 0;
    double v = parse_amount(st.input);
    double p = (v / st.config.max_value) * 100.0;
    int ip = (int)std::floor(p + 1e-9);
    return tradeboy::utils::clampi(ip, 0, 100);
}

static void adjust_percent_step(NumberInputState& st, int delta) {
    int p = current_percent(st);
    p = tradeboy::utils::clampi(p + delta, 0, 100);
    set_amount_percent(st, p);
}

void NumberInputState::open_with(const NumberInputConfig& cfg) {
    open = true;
    open_frames = 0;
    closing = false;
    close_frames = 0;
    config = cfg;

    out_of_range_dialog.reset();
    
    input = "0";
    grid_r = 0;
    grid_c = 1;
    footer_idx = -1;
    
    flash_timer = 0;
    flash_btn_idx = -1;
    
    l1_flash_timer = 0;
    r1_flash_timer = 0;
    b_flash_timer = 0;
    
    result = NumberInputResult::None;
    result_value = 0.0;

    pending_result = NumberInputResult::None;
    pending_result_value = 0.0;
}

void NumberInputState::close() {
    close_with_result(NumberInputResult::Cancelled, 0.0);
}

void NumberInputState::close_with_result(NumberInputResult res, double value) {
    result = res;
    result_value = value;
    pending_result = res;
    pending_result_value = value;
    closing = true;
    close_frames = 0;
}

float NumberInputState::get_open_t() const {
    if (!closing) {
        return (open_frames < 18) ? (float)open_frames / 18.0f : 1.0f;
    } else {
        const int close_dur = 18;
        return 1.0f - (float)close_frames / (float)close_dur;
    }
}

bool NumberInputState::tick_open_anim() {
    if (!closing && open_frames < 18) {
        open_frames++;
        return true;
    }
    return false;
}

bool NumberInputState::tick_close_anim(int close_dur) {
    if (closing) {
        close_frames++;
        if (close_frames >= close_dur) {
            open = false;
            closing = false;
            close_frames = 0;
            return true;
        }
    }
    return false;
}

double NumberInputState::get_input_value() const {
    return parse_amount(input);
}

bool NumberInputState::is_in_range() const {
    double v = get_input_value();
    return v >= config.min_value && v <= config.max_value;
}

bool handle_input(NumberInputState& st, const tradeboy::app::InputState& in, const tradeboy::app::EdgeState& edges) {
    if (!st.open) return false;

    if (st.out_of_range_dialog.open) {
        if (st.out_of_range_dialog.closing) return true;
        if (st.out_of_range_dialog.flash_frames <= 0) {
            if (tradeboy::utils::pressed(in.a, edges.prev.a) || tradeboy::utils::pressed(in.b, edges.prev.b)) {
                st.out_of_range_dialog.start_flash(1);
            }
        }
        return true;
    }

    if (st.closing) {
        return true;
    }

    if (st.l1_flash_timer > 0) st.l1_flash_timer--;
    if (st.r1_flash_timer > 0) st.r1_flash_timer--;
    if (st.b_flash_timer > 0) st.b_flash_timer--;

    if (st.flash_timer > 0) {
        st.flash_timer--;
        if (st.flash_timer == 0) {
            double value = st.get_input_value();
            if (st.flash_btn_idx == 0) {
                // CONFIRM
                if (value < st.config.min_value || value > st.config.max_value) {
                    char msg[192];
                    std::snprintf(msg, sizeof(msg), "OUT_OF_RANGE\nMAX: %.8f %s", st.config.max_value, st.config.available_label.c_str());
                    st.out_of_range_dialog.open_dialog(msg, 1);
                } else {
                    st.close_with_result(NumberInputResult::Confirmed, value);
                }
            } else if (st.flash_btn_idx == 1) {
                // X (Cancel)
                st.close_with_result(NumberInputResult::Cancelled, 0.0);
            }
            st.flash_btn_idx = -1;
        }
        return true;
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
        if (st.footer_idx == -1) {
            st.grid_r = tradeboy::utils::clampi(st.grid_r - 1, 0, 3);
        }
    }
    if (tradeboy::utils::pressed(in.down, edges.prev.down)) {
        if (st.footer_idx == -1) {
            st.grid_r = tradeboy::utils::clampi(st.grid_r + 1, 0, 3);
        }
    }

    if (tradeboy::utils::pressed(in.left, edges.prev.left)) {
        if (st.footer_idx == 1) {
            st.footer_idx = 0;
        } else if (st.footer_idx == 0) {
            st.footer_idx = -1;
            st.grid_r = 3;
            st.grid_c = 2;
        } else if (st.footer_idx == -1) {
            st.grid_c = tradeboy::utils::clampi(st.grid_c - 1, 0, 2);
        }
    }
    if (tradeboy::utils::pressed(in.right, edges.prev.right)) {
        if (st.footer_idx == 0) {
            st.footer_idx = 1;
        } else if (st.footer_idx == -1) {
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
            st.flash_timer = 18;
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

void render(NumberInputState& st, ImFont* font_bold) {
    if (!st.open) return;

    st.tick_open_anim();
    float open_t = st.get_open_t();
    if (st.closing) {
        (void)st.tick_close_anim();
    }

    if (open_t < 0.0f) open_t = 0.0f;
    if (open_t > 1.0f) open_t = 1.0f;
    float ease = open_t;
    ease = 1.0f - (1.0f - ease) * (1.0f - ease);

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 display = io.DisplaySize;

    if (display.x <= 1.0f || display.y <= 1.0f) return;

    float scale = 0.1f + 0.9f * ease;
    ImVec2 winSize(display.x * scale, display.y * scale);
    ImVec2 winPos((display.x - winSize.x) * 0.5f, (display.y - winSize.y) * 0.5f);

    ImGui::SetNextWindowPos(winPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(winSize, ImGuiCond_Always);

    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                              ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoBackground;

    ImGui::Begin("NumberInput", nullptr, wflags);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();

    const float pad = 16.0f;
    const float gap = 24.0f;

    ImU32 dim = MatrixTheme::DIM;
    ImU32 text = MatrixTheme::TEXT;
    ImU32 alert = MatrixTheme::ALERT;
    ImU32 black = MatrixTheme::BLACK;
    ImU32 white = IM_COL32(255, 255, 255, 255);

    ImU32 title_col = st.config.title_color;
    ImU32 input_panel_bg = IM_COL32(0, 59, 0, 102);
    ImU32 panel_bg = IM_COL32(0, 0, 0, 153);
    ImU32 keypad_bg = IM_COL32(0, 0, 0, 204);

    // --- Header ---
    float headerH = 54.0f;
    float x0 = p.x + pad;
    float x1 = p.x + size.x - pad;
    float y0 = p.y + pad;
    
    float headerDrawY = y0 - 10.0f;

    float title_size = 42.0f; 
    
    ImU32 glowCol = (title_col & 0x00FFFFFF) | 0x40000000;
    if (font_bold) {
        dl->AddText(font_bold, title_size, ImVec2(x0 + 1, headerDrawY + 1), glowCol, st.config.title.c_str());
        dl->AddText(font_bold, title_size, ImVec2(x0 - 1, headerDrawY - 1), glowCol, st.config.title.c_str());
        dl->AddText(font_bold, title_size, ImVec2(x0, headerDrawY), title_col, st.config.title.c_str());
    } else {
        dl->AddText(ImVec2(x0 + 1, headerDrawY + 1), glowCol, st.config.title.c_str());
        dl->AddText(ImVec2(x0 - 1, headerDrawY - 1), glowCol, st.config.title.c_str());
        dl->AddText(ImVec2(x0, headerDrawY), title_col, st.config.title.c_str());
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
    float left_w = col_w * 2.0f + gap;
    float right_w = col_w;

    float left_x = x0;
    float right_x = x0 + left_w + gap;

    // Left: input display
    float input_h = 128.0f;
    float input_pad = 12.0f;
    float input_y = y_main;

    dl->AddRectFilled(ImVec2(left_x, input_y), ImVec2(left_x + left_w, input_y + input_h), input_panel_bg);
    dl->AddRect(ImVec2(left_x, input_y), ImVec2(left_x + left_w, input_y + input_h), dim, 0.0f, 0, 2.0f);
    
    if (!st.config.price_label.empty()) {
        dl->AddText(ImVec2(left_x + 8.0f, input_y + 4.0f), dim, st.config.price_label.c_str());
    }

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
    if (st.config.price > 0.0) {
        char approx_usd[96];
        std::snprintf(approx_usd, sizeof(approx_usd), "\xE2\x89\x88 $%.2f USD", cur * st.config.price);
        dl->AddText(ImVec2(left_x + left_w - input_pad - ImGui::CalcTextSize(approx_usd).x, input_y + input_h - 28.0f), dim, approx_usd);
    }

    // Left: keypad grid
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

            dl->AddRectFilled(ImVec2(cx0, cy0), ImVec2(cx0 + cell_w, cy0 + cell_h), bg);
            dl->AddRect(ImVec2(cx0, cy0), ImVec2(cx0 + cell_w, cy0 + cell_h), border, 0.0f, 0, 2.0f);

            const char* t = keypad[r][c];
            float key_font = 30.0f;
            ImVec2 ts = font_bold ? font_bold->CalcTextSizeA(key_font, FLT_MAX, 0.0f, t) : ImGui::CalcTextSize(t);
            float tx = cx0 + (cell_w - ts.x) * 0.5f;
            float ty = cy0 + (cell_h - ts.y) * 0.5f + 4.0f;
            if (font_bold) {
                dl->AddText(font_bold, key_font, ImVec2(tx, ty), fg, t);
            } else {
                dl->AddText(ImVec2(tx, ty), fg, t);
            }
        }
    }

    // Right: progress panel + guide + buttons
    if (st.config.show_available_panel) {
        float right_gap = 12.0f;
        float guide_h = 48.0f;
        
        float guide_y = footer_y - right_gap - guide_h;
        
        float progress_y = y_main;
        float progress_h = guide_y - right_gap - progress_y;
        if (progress_h < 0) progress_h = 0;

        // Progress panel
        dl->AddRectFilled(ImVec2(right_x, progress_y), ImVec2(right_x + right_w, progress_y + progress_h), panel_bg);
        dl->AddRect(ImVec2(right_x, progress_y), ImVec2(right_x + right_w, progress_y + progress_h), dim, 0.0f, 0, 2.0f);

        const char* meter_title = "AVAILABLE";
        float alloc_font = 16.0f;
        
        ImVec2 mt_sz = font_bold ? font_bold->CalcTextSizeA(alloc_font, FLT_MAX, 0.0f, meter_title) : ImGui::CalcTextSize(meter_title);
        float mt_x = right_x + (right_w - mt_sz.x) * 0.5f;
        float mt_y = progress_y + 14.0f;
        
        if (font_bold) {
            dl->AddText(font_bold, alloc_font, ImVec2(mt_x, mt_y), dim, meter_title);
        } else {
            dl->AddText(ImVec2(mt_x, mt_y), dim, meter_title);
        }

        char alloc_val[64];
        std::snprintf(alloc_val, sizeof(alloc_val), "%.2f %s", st.config.max_value, st.config.available_label.c_str());
        ImVec2 av_sz = font_bold ? font_bold->CalcTextSizeA(alloc_font, FLT_MAX, 0.0f, alloc_val) : ImGui::CalcTextSize(alloc_val);
        if (font_bold) {
            dl->AddText(font_bold, alloc_font, ImVec2(right_x + (right_w - av_sz.x) * 0.5f, mt_y + mt_sz.y + 4.0f), text, alloc_val);
        } else {
            dl->AddText(ImVec2(right_x + (right_w - av_sz.x) * 0.5f, mt_y + mt_sz.y + 4.0f), text, alloc_val);
        }

        // Vertical bar
        float bar_w = 64.0f;
        float bar_x = right_x + (right_w - bar_w) * 0.5f;
        float bar_y = mt_y + mt_sz.y + 4.0f + av_sz.y + 16.0f;
        float bar_h = std::max(0.0f, progress_h - (bar_y - progress_y) - 44.0f);
        
        dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h), MatrixTheme::BLACK);
        dl->AddRect(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h), dim, 0.0f, 0, 2.0f);

        float pct = 0.0f;
        if (st.config.max_value > 0.0) pct = (float)std::min(1.0, cur / st.config.max_value);
        float fill_h = bar_h * pct;
        
        // Color based on range
        ImU32 fill_col = (cur > st.config.max_value) ? alert : text;
        dl->AddRectFilled(ImVec2(bar_x, bar_y + (bar_h - fill_h)), ImVec2(bar_x + bar_w, bar_y + bar_h), fill_col);

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
        
        ImU32 pct_col = (cur > st.config.max_value) ? alert : text;
        if (font_bold) {
            dl->AddText(font_bold, pct_font, ImVec2(pct_x, pct_y), pct_col, pct_s);
        } else {
            dl->AddText(ImVec2(pct_x, pct_y), pct_col, pct_s);
        }

        // Guide panel
        dl->AddRectFilled(ImVec2(right_x, guide_y), ImVec2(right_x + right_w, guide_y + guide_h), input_panel_bg);
        dl->AddRect(ImVec2(right_x, guide_y), ImVec2(right_x + right_w, guide_y + guide_h), dim, 0.0f, 0, 2.0f);

        float gy = guide_y + (guide_h - 18.0f) * 0.5f;
        
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
        float l1_w = 24 + 6 + ImGui::CalcTextSize("-5%").x;
        float r1_w = 24 + 6 + ImGui::CalcTextSize("+5%").x;
        float gap_lr = 48.0f;
        float total_lr_w = l1_w + gap_lr + r1_w;
        
        float start_x = right_x + (right_w - total_lr_w) * 0.5f;

        draw_spot_tag(start_x, gy, "L1", "-5%", st.l1_flash_timer > 0);
        draw_spot_tag(start_x + l1_w + gap_lr, gy, "R1", "+5%", st.r1_flash_timer > 0);
        ImGui::SetWindowFontScale(1.0f);
    }

    // --- Footer actions ---
    float btnFontSize = 20.0f;
    float btn_gap = 12.0f;
    float abort_w = 40.0f;
    
    float confirm_x = right_x;
    float confirm_w = right_w - btn_gap - abort_w;
    float abort_x = x1 - abort_w;

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

    ImGui::End();

    if (st.out_of_range_dialog.open) {
        st.out_of_range_dialog.tick_open_anim();
        float d_t = st.out_of_range_dialog.get_open_t();
        int sel = 1;
        tradeboy::ui::render_dialog("NumberInputOutOfRange",
                                    "> ",
                                    st.out_of_range_dialog.body,
                                    "",
                                    "OK",
                                    &sel,
                                    st.out_of_range_dialog.flash_frames,
                                    d_t,
                                    font_bold,
                                    nullptr);

        if (!st.out_of_range_dialog.closing) {
            if (st.out_of_range_dialog.tick_flash()) {
                st.out_of_range_dialog.start_close();
            }
        } else {
            if (st.out_of_range_dialog.tick_close_anim()) {
                st.out_of_range_dialog.reset();
            }
        }
    }
}

} // namespace tradeboy::ui
