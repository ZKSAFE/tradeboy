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

#include "../ui/Dialog.h"

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
    log_to_file("[App] load_private_key start, this=%p\n", this);
    const char* path = "./data/private_key.txt";
    
    log_to_file("[App] reading file: %s\n", path);
    std::string raw = tradeboy::utils::read_text_file(path);
    log_to_file("[App] read_text_file done, size=%d\n", (int)raw.size());

    if (raw.empty()) {
        log_to_file("[App] private key missing or empty: %s\n", path);
        
        log_to_file("[App] clearing priv_key_hex...\n");
        priv_key_hex = ""; // Try assignment instead of clear()
        log_to_file("[App] priv_key_hex cleared\n");
        
        log_to_file("[App] load_private_key early return\n");
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
                if (e.flag) {
                    buy_trigger_frames = 24;
                } else {
                    sell_trigger_frames = 24;
                }
                break;
        }
    }
}

void App::handle_input_edges(const tradeboy::app::InputState& in, const tradeboy::app::EdgeState& edges) {
    if (quit_requested) return;

    // Exit modal has highest priority.
    if (exit_modal_open) {
        if (exit_dialog_closing) {
            return;
        }

        if (exit_dialog_flash_frames <= 0) {
            if (tradeboy::utils::pressed(in.left, edges.prev.left) || tradeboy::utils::pressed(in.right, edges.prev.right)) {
                exit_dialog_selected_btn = 1 - exit_dialog_selected_btn;
            }
            if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
                exit_dialog_pending_action = exit_dialog_selected_btn;
                exit_dialog_flash_frames = 24;
            }
            if (tradeboy::utils::pressed(in.b, edges.prev.b)) {
                exit_dialog_closing = true;
                exit_dialog_close_frames = 0;
                exit_dialog_quit_after_close = false;
                exit_dialog_flash_frames = 0;
                exit_dialog_pending_action = -1;
            }
        }
        return;
    }

    if (tradeboy::utils::pressed(in.m, edges.prev.m)) {
        exit_modal_open = true;
        exit_dialog_selected_btn = 1;
        exit_dialog_open_frames = 0;
        exit_dialog_flash_frames = 0;
        exit_dialog_pending_action = -1;
        exit_dialog_closing = false;
        exit_dialog_close_frames = 0;
        exit_dialog_quit_after_close = false;
        return;
    }

    if (tradeboy::windows::handle_input(num_input, in, edges)) {
        return;
    }
    
    // Block spot input if triggering action
    if (buy_trigger_frames > 0 || sell_trigger_frames > 0) {
        return;
    }

    // Update held states for UI feedback
    action_btn_held = in.a;
    l1_btn_held = in.l1;
    r1_btn_held = in.r1;

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
    // Process triggers
    if (buy_trigger_frames > 0) {
        buy_trigger_frames--;
        if (buy_trigger_frames == 0) open_spot_trade(true);
    }
    if (sell_trigger_frames > 0) {
        sell_trigger_frames--;
        if (sell_trigger_frames == 0) open_spot_trade(false);
    }

    // Flash logic: faster blink while trigger is active.
    // Total trigger duration is still 45 frames, but blink period is shorter.
    const int blinkPeriod = 6;
    const int blinkOnFrames = 3;
    bool buy_flash = (buy_trigger_frames > 0) && ((buy_trigger_frames % blinkPeriod) < blinkOnFrames);
    bool sell_flash = (sell_trigger_frames > 0) && ((sell_trigger_frames % blinkPeriod) < blinkOnFrames);

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
            buy_flash,
            sell_flash,
            font_bold,
            action_btn_held,
            l1_btn_held,
            r1_btn_held);
    }

    dec_frame_counter(buy_press_frames);
    dec_frame_counter(sell_press_frames);
    tradeboy::windows::render(num_input);

    // Process exit dialog flash -> trigger closing when finished.
    if (exit_modal_open && !exit_dialog_closing && exit_dialog_flash_frames > 0) {
        exit_dialog_flash_frames--;
        if (exit_dialog_flash_frames == 0 && exit_dialog_pending_action >= 0) {
            exit_dialog_closing = true;
            exit_dialog_close_frames = 0;
            exit_dialog_quit_after_close = (exit_dialog_pending_action == 0);
            exit_dialog_pending_action = -1;
        }
    }

    // Close animation
    if (exit_modal_open && exit_dialog_closing) {
        const int close_dur = 18;
        exit_dialog_close_frames++;
        if (exit_dialog_close_frames >= close_dur) {
            exit_modal_open = false;
            exit_dialog_closing = false;
            exit_dialog_close_frames = 0;
            if (exit_dialog_quit_after_close) {
                quit_requested = true;
            }
            exit_dialog_quit_after_close = false;
        }
    }

    overlay_rect_active = false;
    overlay_rect_uv = ImVec4(0, 0, 0, 0);

    if (exit_modal_open) {
        float open_t = 1.0f;
        if (!exit_dialog_closing) {
            if (exit_dialog_open_frames < 18) exit_dialog_open_frames++;
            open_t = (float)exit_dialog_open_frames / 18.0f;
        } else {
            const int close_dur = 18;
            open_t = 1.0f - (float)exit_dialog_close_frames / (float)close_dur;
        }

        tradeboy::ui::render_dialog("ExitDialog",
                                    "> ",
                                    "Exit TradeBoy?",
                                    "EXIT",
                                    "CANCEL",
                                    &exit_dialog_selected_btn,
                                    exit_dialog_flash_frames,
                                    open_t,
                                    font_bold,
                                    nullptr);
    }
}

} // namespace tradeboy::app
