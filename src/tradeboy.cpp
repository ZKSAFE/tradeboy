#include <SDL.h>
#include <SDL_opengles2.h>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

static constexpr float UI_SCALE = 2.0f;

static float px(float v) { return v * UI_SCALE; }

static std::string read_text_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) a++;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) b--;
    return s.substr(a, b - a);
}

static std::string normalize_hex_private_key(std::string s) {
    s = trim(s);
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
    return s;
}

static double trunc_to_decimals(double v, int decimals) {
    if (decimals < 0) return v;
    const double p = std::pow(10.0, (double)decimals);
    return std::trunc(v * p) / p;
}

static std::string format_fixed_trunc_sig(double v, int max_sig_digits, int max_decimals) {
    if (!std::isfinite(v)) return "0";

    if (v == 0.0) {
        if (max_decimals > 0) {
            std::ostringstream ss;
            ss.setf(std::ios::fixed);
            ss.precision(std::min(2, max_decimals));
            ss << 0.0;
            return ss.str();
        }
        return "0";
    }

    const double abs_v = std::fabs(v);

    // No scientific notation. If too small for given significant digits, show 0.
    if (abs_v < std::pow(10.0, -(double)max_sig_digits)) {
        if (max_decimals >= 2) return "0.00";
        return "0";
    }

    int int_digits = 1;
    if (abs_v >= 1.0) {
        int_digits = (int)std::floor(std::log10(abs_v)) + 1;
    } else {
        int_digits = 0;
    }

    int decimals = 0;
    if (int_digits < max_sig_digits) {
        decimals = std::min(max_decimals, max_sig_digits - int_digits);
    }

    double tv = trunc_to_decimals(v, decimals);

    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(decimals);
    ss << tv;

    // Trim trailing zeros and dot for non-value fields; keep at least 2 decimals for value is handled by caller.
    std::string out = ss.str();
    if (decimals > 0) {
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
    }
    if (out.empty()) out = "0";
    return out;
}

enum class Tab {
    Spot = 0,
    Long = 1,
    Short = 2,
    Assets = 3,
};

struct SpotRow {
    std::string sym;
    double price;
    double balance;
};

struct InputState {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool a = false;
    bool b = false;
    bool l1 = false;
    bool r1 = false;
};

struct EdgeState {
    InputState prev;
};

static bool pressed(bool now, bool prev) { return now && !prev; }

static int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

struct NumInputModal {
    bool open = false;
    double max_value = 0.0;
    double value = 0.0;
    std::string text = "0";
    int focus_r = 0;
    int focus_c = 0;
    bool is_buy = true;
    std::string sym;

    bool show_error = false;
    std::string error_text;

    void reset(const std::string& in_sym, bool buy, double in_max) {
        open = true;
        max_value = std::max(0.0, in_max);
        value = 0.0;
        text = "0";
        focus_r = 0;
        focus_c = 0;
        is_buy = buy;
        sym = in_sym;
        show_error = false;
        error_text.clear();
    }

    void close() { open = false; }

    bool parse_text() {
        std::string t = text;
        if (t.empty()) t = "0";
        // allow a single '.'
        if (t == ".") t = "0.";
        try {
            value = std::stod(t);
        } catch (...) {
            value = 0.0;
            return false;
        }
        if (!std::isfinite(value) || value < 0.0) value = 0.0;
        return true;
    }

    void set_from_percent(int percent) {
        percent = clampi(percent, 0, 100);
        double v = (max_value * (double)percent) / 100.0;
        // Keep as plain number; truncation is UI-only.
        value = v;
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(8);
        ss << v;
        std::string s = ss.str();
        while (s.size() > 1 && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        text = s.empty() ? "0" : s;
    }

    int current_percent() const {
        if (max_value <= 0.0) return 0;
        double p = (value / max_value) * 100.0;
        int ip = (int)std::floor(p + 1e-9);
        return clampi(ip, 0, 100);
    }

    void adjust_percent_step(int delta) {
        int p = current_percent();
        p = clampi(p + delta, 0, 100);
        set_from_percent(p);
    }

    void append_char(char ch) {
        if (text == "0" && ch != '.') text.clear();
        if (ch == '.') {
            if (text.find('.') != std::string::npos) return;
            if (text.empty()) text = "0";
        }
        text.push_back(ch);
        parse_text();
    }

    void del() {
        if (text.empty()) {
            text = "0";
            value = 0.0;
            return;
        }
        text.pop_back();
        if (text.empty() || text == "-") text = "0";
        parse_text();
    }

    void ac() {
        text = "0";
        value = 0.0;
    }

    void maxv() {
        value = max_value;
        std::ostringstream ss;
        ss.setf(std::ios::fixed);
        ss.precision(8);
        ss << max_value;
        std::string s = ss.str();
        while (s.size() > 1 && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        text = s.empty() ? "0" : s;
    }

    bool over_max() const { return value > max_value + 1e-12; }
};

struct App {
    Tab tab = Tab::Spot;

    std::vector<SpotRow> spot_rows;
    int spot_row_idx = 0;
    int spot_action_idx = 0; // 0=buy, 1=sell
    bool spot_action_focus = false;

    NumInputModal num_modal;

    // Wallet
    std::string priv_key_hex;

    // Assets placeholders
    double wallet_usdc = 0.0;
    double hl_usdc = 0.0;

    void init_demo_data() {
        spot_rows = {
            {"BTC", 87482.75, 0.0},
            {"ETH", 2962.41, 1.233},
            {"SOL", 124.15, 41.646},
            {"BNB", 842.00, 0.0},
            {"XRP", 1.86, 0.0},
            {"TRX", 0.2843, 0.0},
            {"DOGE", 0.12907, 0.0},
            {"ADA", 0.3599, 0.0},
        };

        wallet_usdc = 10000.0;
        hl_usdc = 0.0;
    }

    void load_private_key() {
        std::string raw = read_text_file("./data/private_key.txt");
        priv_key_hex = normalize_hex_private_key(raw);
    }

    void next_tab(int delta) {
        int t = (int)tab + delta;
        if (t < 0) t = 3;
        if (t > 3) t = 0;
        tab = (Tab)t;
    }

    void open_spot_trade(bool buy) {
        if (spot_rows.empty()) return;
        const auto& row = spot_rows[(size_t)spot_row_idx];
        double maxv = 0.0;
        if (buy) {
            maxv = (row.price > 0.0) ? (hl_usdc / row.price) : 0.0;
        } else {
            maxv = row.balance;
        }
        num_modal.reset(row.sym, buy, maxv);
    }

    void handle_input_edges(const InputState& in, const EdgeState& edges) {
        const bool p_l1 = pressed(in.l1, edges.prev.l1);
        const bool p_r1 = pressed(in.r1, edges.prev.r1);
        if (p_l1) next_tab(-1);
        if (p_r1) next_tab(+1);

        if (num_modal.open) {
            // modal navigation
            if (pressed(in.b, edges.prev.b)) {
                num_modal.close();
                return;
            }

            if (pressed(in.l1, edges.prev.l1)) num_modal.adjust_percent_step(-5);
            if (pressed(in.r1, edges.prev.r1)) num_modal.adjust_percent_step(+5);

            if (pressed(in.up, edges.prev.up)) num_modal.focus_r = clampi(num_modal.focus_r - 1, 0, 3);
            if (pressed(in.down, edges.prev.down)) num_modal.focus_r = clampi(num_modal.focus_r + 1, 0, 3);
            if (pressed(in.left, edges.prev.left)) num_modal.focus_c = clampi(num_modal.focus_c - 1, 0, 3);
            if (pressed(in.right, edges.prev.right)) num_modal.focus_c = clampi(num_modal.focus_c + 1, 0, 3);

            if (pressed(in.a, edges.prev.a)) {
                // keypad layout
                // Row0: 7 8 9 DEL
                // Row1: 4 5 6 AC
                // Row2: 1 2 3 MAX
                // Row3: . 0 (blank) ENTER
                int r = num_modal.focus_r;
                int c = num_modal.focus_c;

                if (r == 0 && c <= 2) num_modal.append_char((char)('7' + c));
                else if (r == 0 && c == 3) num_modal.del();
                else if (r == 1 && c <= 2) num_modal.append_char((char)('4' + c));
                else if (r == 1 && c == 3) num_modal.ac();
                else if (r == 2 && c <= 2) num_modal.append_char((char)('1' + c));
                else if (r == 2 && c == 3) num_modal.maxv();
                else if (r == 3 && c == 0) num_modal.append_char('.');
                else if (r == 3 && c == 1) num_modal.append_char('0');
                else if (r == 3 && c == 3) {
                    // ENTER
                    num_modal.parse_text();
                    if (num_modal.over_max()) {
                        num_modal.show_error = true;
                        num_modal.error_text = "Amount exceeds max";
                    } else {
                        // TODO: submit order to HL
                        num_modal.close();
                    }
                }
            }

            // Dismiss error prompt
            if (num_modal.show_error && pressed(in.a, edges.prev.a)) {
                num_modal.show_error = false;
            }
            if (num_modal.show_error && pressed(in.b, edges.prev.b)) {
                num_modal.show_error = false;
            }

            return;
        }

        if (tab == Tab::Spot) {
            if (pressed(in.up, edges.prev.up)) {
                spot_row_idx = clampi(spot_row_idx - 1, 0, (int)spot_rows.size() - 1);
            }
            if (pressed(in.down, edges.prev.down)) {
                spot_row_idx = clampi(spot_row_idx + 1, 0, (int)spot_rows.size() - 1);
            }

            if (spot_action_focus) {
                if (pressed(in.left, edges.prev.left) || pressed(in.right, edges.prev.right)) {
                    spot_action_idx = 1 - spot_action_idx;
                }
                if (pressed(in.a, edges.prev.a)) {
                    open_spot_trade(spot_action_idx == 0);
                }
                if (pressed(in.b, edges.prev.b)) {
                    spot_action_focus = false;
                }
            } else {
                if (pressed(in.left, edges.prev.left) || pressed(in.right, edges.prev.right)) {
                    spot_action_focus = true;
                    spot_action_idx = 0;
                }
                if (pressed(in.a, edges.prev.a)) {
                    // shortcut: open buy
                    open_spot_trade(true);
                }
            }
        }
    }

    void render_top_tabs() {
        ImGui::BeginGroup();

        auto tab_btn = [&](const char* label, Tab t) {
            const bool selected = (tab == t);
            if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.44f, 0.35f, 1.0f));
            if (selected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            ImGui::Button(label, ImVec2(px(110), px(40)));
            if (selected) ImGui::PopStyleColor(2);
        };

        ImGui::Button("L1", ImVec2(px(60), px(40)));
        ImGui::SameLine();
        tab_btn("Spot", Tab::Spot);
        ImGui::SameLine();
        tab_btn("Long", Tab::Long);
        ImGui::SameLine();
        tab_btn("Short", Tab::Short);
        ImGui::SameLine();
        tab_btn("Assets", Tab::Assets);
        ImGui::SameLine();
        ImGui::Button("R1", ImVec2(px(60), px(40)));

        ImGui::EndGroup();
    }

    void render_spot() {
        ImGui::BeginChild("spot_panel", ImVec2(0, 0), true);

        ImGui::TextUnformatted("price            balance          value");
        ImGui::Separator();

        const float row_h = px(42.0f);
        for (int i = 0; i < (int)spot_rows.size(); i++) {
            const bool selected = (i == spot_row_idx);
            const auto& r = spot_rows[(size_t)i];

            ImGui::PushID(i);
            ImVec4 bg = selected ? ImVec4(0.25f, 0.27f, 0.22f, 1.0f) : ImVec4(0, 0, 0, 0);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);

            ImGui::BeginChild("row", ImVec2(0, row_h), false);

            std::string price_s = format_fixed_trunc_sig(r.price, 8, 8);
            std::string bal_s = format_fixed_trunc_sig(r.balance, 8, 8);
            double value = r.price * r.balance;
            value = trunc_to_decimals(value, 2);
            std::ostringstream vs;
            vs.setf(std::ios::fixed);
            vs.precision(2);
            vs << value;

            ImGui::Text("%-5s  %-14s  %-14s  %-10s", r.sym.c_str(), price_s.c_str(), bal_s.c_str(), vs.str().c_str());
            ImGui::SameLine();

            const bool action_on_row = selected;
            float alpha_low = 0.2f;

            ImGui::SameLine(ImGui::GetWindowWidth() - px(140.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, (action_on_row && spot_action_focus && spot_action_idx == 0) ? 1.0f : (action_on_row ? 1.0f : alpha_low));
            if (spot_action_focus && selected && spot_action_idx == 0) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.44f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            }
            ImGui::Button("buy", ImVec2(px(60), px(32)));
            if (spot_action_focus && selected && spot_action_idx == 0) ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();

            ImGui::SameLine();
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, (action_on_row && spot_action_focus && spot_action_idx == 1) ? 1.0f : (action_on_row ? 1.0f : alpha_low));
            if (spot_action_focus && selected && spot_action_idx == 1) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.44f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            }
            ImGui::Button("sell", ImVec2(px(60), px(32)));
            if (spot_action_focus && selected && spot_action_idx == 1) ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();

            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopID();
        }

        ImGui::EndChild();
    }

    void render_assets() {
        ImGui::BeginChild("assets_panel", ImVec2(0, 0), true);

        std::string wallet_s = format_fixed_trunc_sig(wallet_usdc, 8, 8);
        std::string hl_s = format_fixed_trunc_sig(hl_usdc, 8, 8);

        ImGui::Text("Wallet USDC: %s", wallet_s.c_str());
        ImGui::Text("HL USDC:     %s", hl_s.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted("Deposit/Withdraw will be implemented.");

        ImGui::EndChild();
    }

    void render_coming_soon() {
        ImGui::BeginChild("soon_panel", ImVec2(0, 0), true);
        ImGui::TextUnformatted("Coming Soon");
        ImGui::EndChild();
    }

    void render_num_modal() {
        if (!num_modal.open) return;

        ImGui::SetNextWindowPos(ImVec2(px(140), px(85)), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(px(440), px(320)), ImGuiCond_Always);

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
        ImGui::Begin("Amount", &num_modal.open, flags);

        num_modal.parse_text();
        const bool over = num_modal.over_max();

        ImGui::PushItemWidth(-1);
        if (over) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
        ImGui::Text("%s", num_modal.text.c_str());
        if (over) ImGui::PopStyleColor();

        int p = num_modal.current_percent();
        ImGui::Text("%d%%    Max:%s", p, format_fixed_trunc_sig(num_modal.max_value, 8, 8).c_str());

        const char* labels[4][4] = {
            {"7", "8", "9", "DEL"},
            {"4", "5", "6", "AC"},
            {"1", "2", "3", "MAX"},
            {".", "0", "", "ENTER"},
        };

        for (int r = 0; r < 4; r++) {
            for (int c = 0; c < 4; c++) {
                ImGui::PushID(r * 10 + c);
                const bool focused = (r == num_modal.focus_r && c == num_modal.focus_c);

                if (focused) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.44f, 0.35f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.33f, 0.25f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.9f));
                }

                const char* t = labels[r][c];
                ImVec2 sz = (r == 3 && c == 3) ? ImVec2(px(160), px(48)) : ImVec2(px(80), px(48));
                if (t[0] == '\0') {
                    ImGui::Dummy(sz);
                } else {
                    ImGui::Button(t, sz);
                }

                ImGui::PopStyleColor(2);
                ImGui::PopID();

                if (c < 3) ImGui::SameLine();
            }
        }

        if (num_modal.show_error) {
            ImGui::OpenPopup("Error");
        }

        if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(num_modal.error_text.c_str());
            ImGui::Separator();
            ImGui::TextUnformatted("A: OK   B: Close");
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    void render_bottom_hint() {
        ImGui::Separator();
        ImGui::TextUnformatted("A Select/Confirm   B Back   L1/R1 Tabs");
    }

    void render() {
        render_top_tabs();
        ImGui::Separator();

        switch (tab) {
            case Tab::Spot:
                render_spot();
                break;
            case Tab::Assets:
                render_assets();
                break;
            case Tab::Long:
            case Tab::Short:
                render_coming_soon();
                break;
        }

        render_num_modal();
        render_bottom_hint();
    }
};

static InputState poll_input_state_from_events(const std::vector<SDL_Event>& events) {
    InputState in;

    auto set_key = [&](SDL_Keycode key, bool down) {
        (void)down;
        switch (key) {
            case SDLK_UP: in.up = true; break;
            case SDLK_DOWN: in.down = true; break;
            case SDLK_LEFT: in.left = true; break;
            case SDLK_RIGHT: in.right = true; break;
            case SDLK_z: in.a = true; break;
            case SDLK_x: in.b = true; break;
            case SDLK_q: in.l1 = true; break;
            case SDLK_w: in.r1 = true; break;
            default: break;
        }
    };

    for (const auto& e : events) {
        if (e.type == SDL_KEYDOWN) {
            set_key(e.key.keysym.sym, true);
        }
        if (e.type == SDL_JOYHATMOTION) {
            // D-pad hat. Common: value has bitmask.
            Uint8 v = e.jhat.value;
            if (v & SDL_HAT_UP) in.up = true;
            if (v & SDL_HAT_DOWN) in.down = true;
            if (v & SDL_HAT_LEFT) in.left = true;
            if (v & SDL_HAT_RIGHT) in.right = true;
        }
        if (e.type == SDL_JOYBUTTONDOWN) {
            // Best-effort mapping (may need adjust after testing on device)
            // Typical: 0=A, 1=B, 4=L1, 5=R1
            switch (e.jbutton.button) {
                case 0: in.a = true; break;
                case 1: in.b = true; break;
                case 4: in.l1 = true; break;
                case 5: in.r1 = true; break;
                default: break;
            }
        }
    }

    return in;
}

static void apply_retro_style() {
    ImGuiStyle& s = ImGui::GetStyle();
    ImGui::StyleColorsDark();

    s.ScaleAllSizes(UI_SCALE);

    s.WindowRounding = 10.0f;
    s.FrameRounding = 8.0f;
    s.ScrollbarRounding = 8.0f;
    s.GrabRounding = 8.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.13f, 1.00f);
    c[ImGuiCol_ChildBg] = ImVec4(0.18f, 0.18f, 0.15f, 1.00f);
    c[ImGuiCol_Border] = ImVec4(0.30f, 0.29f, 0.24f, 1.00f);
    c[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.93f, 1.00f);

    c[ImGuiCol_Button] = ImVec4(0.30f, 0.29f, 0.24f, 1.00f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.45f, 0.44f, 0.35f, 1.00f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.38f, 0.36f, 0.28f, 1.00f);

    c[ImGuiCol_FrameBg] = ImVec4(0.22f, 0.22f, 0.18f, 1.00f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.28f, 0.28f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.32f, 0.32f, 0.26f, 1.00f);
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_JoystickEventState(SDL_ENABLE);

    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) != 0) {
        mode.w = 720;
        mode.h = 480;
        mode.refresh_rate = 60;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    SDL_Window* window = SDL_CreateWindow(
        "tradeboy",
        SDL_WINDOWPOS_UNDEFINED_DISPLAY(0),
        SDL_WINDOWPOS_UNDEFINED_DISPLAY(0),
        mode.w,
        mode.h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);

    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glctx = SDL_GL_CreateContext(window);
    if (!glctx) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_MakeCurrent(window, glctx);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    apply_retro_style();

    const char* glsl_version = "#version 100";
    ImGui_ImplSDL2_InitForOpenGL(window, glctx);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Try load font if present in current dir
    {
        std::ifstream f("NotoSansCJK-Regular.ttc");
        if (f.good()) {
            io.Fonts->AddFontFromFileTTF("NotoSansCJK-Regular.ttc", 44.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
        }
    }

    // Open joystick 0 if available
    SDL_Joystick* joy0 = nullptr;
    if (SDL_NumJoysticks() > 0) {
        joy0 = SDL_JoystickOpen(0);
    }

    App app;
    app.init_demo_data();
    app.load_private_key();

    EdgeState edges;

    bool running = true;
    while (running) {
        std::vector<SDL_Event> events;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            events.push_back(e);
            if (e.type == SDL_QUIT) running = false;
        }

        InputState in = poll_input_state_from_events(events);
        app.handle_input_edges(in, edges);
        edges.prev = in;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(px(12), px(12)), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)mode.w - px(24.0f), (float)mode.h - px(24.0f)), ImGuiCond_Always);

        ImGui::Begin("TradeBoy", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
        app.render();
        ImGui::End();

        ImGui::Render();

        glViewport(0, 0, mode.w, mode.h);
        glClearColor(0.10f, 0.11f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    if (joy0) SDL_JoystickClose(joy0);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(glctx);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
