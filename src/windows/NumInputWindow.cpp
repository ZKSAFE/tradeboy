#include "NumInputWindow.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "../utils/Math.h"
#include "../utils/Scale.h"
#include "../utils/Format.h"
#include "../uiComponents/Fonts.h"

namespace tradeboy::windows {

void NumInputState::reset(const std::string& in_sym, bool buy, double in_max) {
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

void NumInputState::close() { open = false; }

bool NumInputState::parse_text() {
    std::string t = text;
    if (t.empty()) t = "0";
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

void NumInputState::set_from_percent(int percent) {
    percent = tradeboy::utils::clampi(percent, 0, 100);
    double v = (max_value * (double)percent) / 100.0;
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

int NumInputState::current_percent() const {
    if (max_value <= 0.0) return 0;
    double p = (value / max_value) * 100.0;
    int ip = (int)std::floor(p + 1e-9);
    return tradeboy::utils::clampi(ip, 0, 100);
}

void NumInputState::adjust_percent_step(int delta) {
    int p = current_percent();
    p = tradeboy::utils::clampi(p + delta, 0, 100);
    set_from_percent(p);
}

void NumInputState::append_char(char ch) {
    if (text == "0" && ch != '.') text.clear();
    if (ch == '.') {
        if (text.find('.') != std::string::npos) return;
        if (text.empty()) text = "0";
    }
    text.push_back(ch);
    parse_text();
}

void NumInputState::del() {
    if (text.empty()) {
        text = "0";
        value = 0.0;
        return;
    }
    text.pop_back();
    if (text.empty() || text == "-") text = "0";
    parse_text();
}

void NumInputState::ac() {
    text = "0";
    value = 0.0;
}

void NumInputState::maxv() {
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

bool NumInputState::over_max() const { return value > max_value + 1e-12; }

bool handle_input(NumInputState& st, const tradeboy::app::InputState& in, const tradeboy::app::EdgeState& edges) {
    if (!st.open) return false;

    if (tradeboy::utils::pressed(in.b, edges.prev.b)) {
        st.close();
        return true;
    }

    if (tradeboy::utils::pressed(in.l1, edges.prev.l1)) st.adjust_percent_step(-5);
    if (tradeboy::utils::pressed(in.r1, edges.prev.r1)) st.adjust_percent_step(+5);

    if (tradeboy::utils::pressed(in.up, edges.prev.up)) st.focus_r = tradeboy::utils::clampi(st.focus_r - 1, 0, 3);
    if (tradeboy::utils::pressed(in.down, edges.prev.down)) st.focus_r = tradeboy::utils::clampi(st.focus_r + 1, 0, 3);
    if (tradeboy::utils::pressed(in.left, edges.prev.left)) st.focus_c = tradeboy::utils::clampi(st.focus_c - 1, 0, 3);
    if (tradeboy::utils::pressed(in.right, edges.prev.right)) st.focus_c = tradeboy::utils::clampi(st.focus_c + 1, 0, 3);

    if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
        int r = st.focus_r;
        int c = st.focus_c;

        if (r == 0 && c <= 2) st.append_char((char)('7' + c));
        else if (r == 0 && c == 3) st.del();
        else if (r == 1 && c <= 2) st.append_char((char)('4' + c));
        else if (r == 1 && c == 3) st.ac();
        else if (r == 2 && c <= 2) st.append_char((char)('1' + c));
        else if (r == 2 && c == 3) st.maxv();
        else if (r == 3 && c == 0) st.append_char('.');
        else if (r == 3 && c == 1) st.append_char('0');
        else if (r == 3 && c == 3) {
            st.parse_text();
            if (st.over_max()) {
                st.show_error = true;
                st.error_text = "Amount exceeds max";
            } else {
                st.close();
            }
        }
    }

    if (st.show_error && tradeboy::utils::pressed(in.a, edges.prev.a)) st.show_error = false;
    if (st.show_error && tradeboy::utils::pressed(in.b, edges.prev.b)) st.show_error = false;

    return true;
}

void render(NumInputState& st) {
    if (!st.open) return;

    ImFont* f = tradeboy::ui::fonts().large;
    if (f) ImGui::PushFont(f);

    ImGui::SetNextWindowPos(ImVec2(tradeboy::utils::px(140), tradeboy::utils::px(85)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(tradeboy::utils::px(440), tradeboy::utils::px(320)), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("Amount", &st.open, flags);

    st.parse_text();
    const bool over = st.over_max();

    ImGui::PushItemWidth(-1);
    if (over) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
    ImGui::Text("%s", st.text.c_str());
    if (over) ImGui::PopStyleColor();

    int p = st.current_percent();
    ImGui::Text("%d%%    Max:%s", p, tradeboy::utils::format_fixed_trunc_sig(st.max_value, 8, 8).c_str());

    const char* labels[4][4] = {
        {"7", "8", "9", "DEL"},
        {"4", "5", "6", "AC"},
        {"1", "2", "3", "MAX"},
        {".", "0", "", "ENTER"},
    };

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            ImGui::PushID(r * 10 + c);
            const bool focused = (r == st.focus_r && c == st.focus_c);

            if (focused) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.44f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f, 0.33f, 0.25f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 0.9f));
            }

            const char* t = labels[r][c];
            ImVec2 sz = (r == 3 && c == 3) ? ImVec2(tradeboy::utils::px(160), tradeboy::utils::px(48)) : ImVec2(tradeboy::utils::px(80), tradeboy::utils::px(48));
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

    if (st.show_error) {
        ImGui::OpenPopup("Error");
    }

    if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(st.error_text.c_str());
        ImGui::Separator();
        ImGui::TextUnformatted("A: OK   B: Close");
        ImGui::EndPopup();
    }

    ImGui::End();

    if (f) ImGui::PopFont();
}

} // namespace tradeboy::windows
