#include "App.h"

#include <algorithm>
#include <sstream>
#include <chrono>

#include <SDL.h>

#include "imgui.h"

#include "../spot/SpotScreen.h"
#include "../spot/SpotUiEvents.h"
#include "../market/MarketDataService.h"
#include "../market/HyperliquidWgetDataSource.h"
#include "../market/HyperliquidWsDataSource.h"
#include "../model/TradeModel.h"
#include "../utils/File.h"
#include "../utils/Math.h"

extern void log_to_file(const char* fmt, ...);

namespace tradeboy::app {

void App::init_demo_data() {
    std::vector<tradeboy::model::SpotRow> rows = {
        {"BTC", 87482.75, 87482.75, 0.0, 87482.75},
        {"ETH", 2962.41, 2962.41, 0.0, 2962.41},
        {"SOL", 124.15, 124.15, 0.0, 124.15},
        {"BNB", 842.00, 842.00, 0.0, 842.00},
        {"XRP", 1.86, 1.86, 0.0, 1.86},
        {"TRX", 0.2843, 0.2843, 0.0, 0.2843},
        {"DOGE", 0.12907, 0.12907, 0.0, 0.12907},
        {"ADA", 0.3599, 0.3599, 0.0, 0.3599},
    };

    uint32_t seed = (uint32_t)(SDL_GetTicks() ^ 0xA53C9E17u);
    auto rnd01 = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return (double)((seed >> 8) & 0xFFFFu) / 65535.0;
    };
    for (auto& r : rows) {
        double bal = 0.0;
        if (r.sym == "BTC") bal = 0.01 + rnd01() * 0.05;
        else if (r.sym == "ETH") bal = 0.2 + rnd01() * 1.5;
        else bal = rnd01() * 50.0;
        r.balance = bal;

        double entry_mul = 0.90 + rnd01() * 0.20;
        r.entry_price = r.price * entry_mul;
    }

    wallet_usdc = 10000.0;
    hl_usdc = 0.0;

    model.set_spot_rows(std::move(rows));
    model.set_spot_row_idx(spot_row_idx);
}

void App::startup() {
    if (!market_src) {
        log_to_file("[App] Market source: WS (forced)\n");
        market_src.reset(new tradeboy::market::HyperliquidWsDataSource());
    }
    if (!market_service) {
        market_service.reset(new tradeboy::market::MarketDataService(model, *market_src));
    }
    market_service->start();
}

void App::shutdown() {
    log_to_file("[App] shutdown()\n");
    if (market_service) {
        market_service->stop();
        market_service.reset();
    }
    market_src.reset();
}

void App::load_private_key() {
    const char* path = "./data/private_key.txt";
    std::string raw = tradeboy::utils::read_text_file(path);
    if (raw.empty()) {
        log_to_file("[App] private key missing or empty: %s\n", path);
        priv_key_hex.clear();
        return;
    }
    log_to_file("[App] private key raw_len=%d\n", (int)raw.size());
    priv_key_hex = tradeboy::utils::normalize_hex_private_key(raw);
    log_to_file("[App] private key normalized_len=%d\n", (int)priv_key_hex.size());
}

void App::dec_frame_counter(int& v) {
    if (v > 0) v--;
}

void App::open_spot_trade(bool buy) {
    tradeboy::model::TradeModelSnapshot snap = model.snapshot();
    if (snap.spot_rows.empty()) return;
    if (spot_row_idx < 0 || spot_row_idx >= (int)snap.spot_rows.size()) return;
    const auto& row = snap.spot_rows[(size_t)spot_row_idx];
    double maxv = 0.0;
    if (buy) {
        maxv = (row.price > 0.0) ? (hl_usdc / row.price) : 0.0;
    } else {
        maxv = row.balance;
    }
    num_input.reset(row.sym, buy, maxv);
}

void App::apply_spot_ui_events(const std::vector<tradeboy::spot::SpotUiEvent>& ev) {
    for (const auto& e : ev) {
        switch (e.type) {
            case tradeboy::spot::SpotUiEventType::RowDelta:
                spot_row_idx += e.value;
                model.set_spot_row_idx(spot_row_idx);
                break;
            case tradeboy::spot::SpotUiEventType::EnterActionFocus:
                spot_action_focus = true;
                spot_action_idx = e.value;
                break;
            case tradeboy::spot::SpotUiEventType::ExitActionFocus:
                spot_action_focus = false;
                break;
            case tradeboy::spot::SpotUiEventType::SetActionIdx:
                spot_action_idx = e.value;
                break;
            case tradeboy::spot::SpotUiEventType::TriggerAction:
                if (e.flag) buy_press_frames = 2;
                else sell_press_frames = 2;
                open_spot_trade(e.flag);
                break;
        }
    }
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
        tradeboy::spot::SpotUiState ui;
        ui.spot_action_focus = spot_action_focus;
        ui.spot_action_idx = spot_action_idx;
        ui.buy_press_frames = buy_press_frames;
        ui.sell_press_frames = sell_press_frames;

        std::vector<tradeboy::spot::SpotUiEvent> ev = tradeboy::spot::collect_spot_ui_events(in, edges, ui);
        apply_spot_ui_events(ev);
    }
}

void App::render() {
    tradeboy::spot::SpotUiState ui;
    ui.spot_action_focus = spot_action_focus;
    ui.spot_action_idx = spot_action_idx;
    ui.buy_press_frames = buy_press_frames;
    ui.sell_press_frames = sell_press_frames;

    // Spot page now uses the new UI demo layout. Data layer is intentionally
    // not connected yet (render uses mock data only).
    if (tab == Tab::Spot) {
        tradeboy::spot::render_spot_screen(
            spot_row_idx,
            spot_action_idx,
            buy_press_frames > 0,
            sell_press_frames > 0);
    }

    dec_frame_counter(buy_press_frames);
    dec_frame_counter(sell_press_frames);
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
