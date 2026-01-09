#include "App.h"

#include <algorithm>
#include <sstream>

#include <SDL.h>

#include "imgui.h"

#include "../spot/SpotScreen.h"
#include "../utils/File.h"
#include "../utils/Math.h"

namespace tradeboy::app {

void App::init_demo_data() {
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

    regenerate_kline();
}

void App::regenerate_kline() {
    kline_data.clear();
    // Default approx 60 candles for 720px width (adjust as needed)
    int candle_count = 60;
    kline_data.reserve(candle_count);

    uint32_t seed = (uint32_t)(tf_idx * 1000 + spot_row_idx * 100 + SDL_GetTicks());
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) & 0xFFFFu;
    };

    float cur = 68000.0f;
    if (spot_rows.size() > (size_t)spot_row_idx) {
        cur = (float)spot_rows[spot_row_idx].price;
    }

    for (int i = 0; i < candle_count; i++) {
        float o = cur;
        float volatility = cur * 0.005f; 
        float hi = o + (float)(rnd() % 100) / 100.0f * volatility;
        float lo = o - (float)(rnd() % 100) / 100.0f * volatility;
        float c = lo + (float)(rnd() % 100) / 100.0f * (hi - lo);
        
        // Ensure High is highest and Low is lowest
        if (c > hi) hi = c;
        if (c < lo) lo = c;
        if (o > hi) hi = o;
        if (o < lo) lo = o;

        cur = c;
        kline_data.push_back({o, hi, lo, c});
    }
}

void App::load_private_key() {
    std::string raw = tradeboy::utils::read_text_file("./data/private_key.txt");
    priv_key_hex = tradeboy::utils::normalize_hex_private_key(raw);
}

void App::next_timeframe() { 
    tf_idx = (tf_idx + 1) % 3;
    regenerate_kline();
}

void App::dec_frame_counter(int& v) {
    if (v > 0) v--;
}

void App::open_spot_trade(bool buy) {
    if (spot_rows.empty()) return;
    const auto& row = spot_rows[(size_t)spot_row_idx];
    double maxv = 0.0;
    if (buy) {
        maxv = (row.price > 0.0) ? (hl_usdc / row.price) : 0.0;
    } else {
        maxv = row.balance;
    }
    num_input.reset(row.sym, buy, maxv);
}

void App::handle_input_edges(const tradeboy::app::InputState& in, const tradeboy::app::EdgeState& edges) {
    if (quit_requested) return;

    // Exit modal has highest priority.
    if (exit_modal_open) {
        if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
            quit_requested = true;
            exit_modal_open = false;
        }
        if (tradeboy::utils::pressed(in.b, edges.prev.b)) {
            exit_modal_open = false;
        }
        return;
    }

    if (tradeboy::utils::pressed(in.m, edges.prev.m)) {
        exit_modal_open = true;
        return;
    }

    if (tradeboy::windows::handle_input(num_input, in, edges)) {
        return;
    }

    if (tab == Tab::Spot) {
        if (tradeboy::utils::pressed(in.x, edges.prev.x)) {
            next_timeframe();
            x_press_frames = 8;
        }
        if (tradeboy::utils::pressed(in.up, edges.prev.up)) {
            spot_row_idx = tradeboy::utils::clampi(spot_row_idx - 1, 0, (int)spot_rows.size() - 1);
        }
        if (tradeboy::utils::pressed(in.down, edges.prev.down)) {
            spot_row_idx = tradeboy::utils::clampi(spot_row_idx + 1, 0, (int)spot_rows.size() - 1);
        }

        if (spot_action_focus) {
            if (tradeboy::utils::pressed(in.left, edges.prev.left)) {
                if (spot_action_idx == 1) spot_action_idx = 0;
            }
            if (tradeboy::utils::pressed(in.right, edges.prev.right)) {
                if (spot_action_idx == 0) spot_action_idx = 1;
            }
            if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
                if (spot_action_idx == 0) buy_press_frames = 2;
                else sell_press_frames = 2;
                open_spot_trade(spot_action_idx == 0);
            }
            if (tradeboy::utils::pressed(in.b, edges.prev.b)) {
                spot_action_focus = false;
            }
        } else {
            if (tradeboy::utils::pressed(in.left, edges.prev.left)) {
                spot_action_focus = true;
                spot_action_idx = 0;
            }
            if (tradeboy::utils::pressed(in.right, edges.prev.right)) {
                spot_action_focus = true;
                spot_action_idx = 1;
            }
            if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
                buy_press_frames = 2;
                open_spot_trade(true);
            }
        }
    }
}

void App::render() {
    tradeboy::spot::render_spot_screen(*this);
    tradeboy::windows::render(num_input);

    if (exit_modal_open) {
        ImGui::OpenPopup("Exit");
    }
    if (ImGui::BeginPopupModal("Exit", &exit_modal_open, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Exit TradeBoy?");
        ImGui::Separator();
        ImGui::TextUnformatted("A: Exit   B: Cancel");
        ImGui::EndPopup();
    }
}

} // namespace tradeboy::app
