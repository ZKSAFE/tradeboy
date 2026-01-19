#include "App.h"

#include <algorithm>
#include <sstream>
#include <chrono>

#include <SDL.h>

#include "imgui.h"

#include "../spot/SpotScreen.h"
#include "../spot/SpotUiEvents.h"
#include "../spotOrder/SpotOrderScreen.h"
#include "../market/MarketDataService.h"
#include "../market/HyperliquidWgetDataSource.h"
#include "../market/HyperliquidWsDataSource.h"
#include "../model/TradeModel.h"
#include "../perp/PerpScreen.h"
#include "../account/AccountScreen.h"
#include "../utils/File.h"
#include "../utils/Flash.h"
#include "../utils/Math.h"
#include "../ui/MatrixBackground.h"
#include "../ui/MatrixTheme.h"
#include "../ui/MainUI.h"

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

    boot_anim_active = true;
    boot_anim_frames = 0;
    boot_anim_t = 0.0f;
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

        // priv_key_hex is expected to be empty on startup; avoid mutating it here.
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

void App::open_spot_order(bool buy) {
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

    tradeboy::spotOrder::Side side = buy ? tradeboy::spotOrder::Side::Buy : tradeboy::spotOrder::Side::Sell;
    spot_order.open_with(row, side, maxv);
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

    if (exit_poweroff_anim_active) {
        return;
    }

    // Exit modal has highest priority.
    if (exit_modal_open) {
        if (exit_dialog_closing) {
            return;
        }

        // Always allow B to close/cancel, even while flashing.
        if (tradeboy::utils::pressed(in.b, edges.prev.b)) {
            exit_dialog_closing = true;
            exit_dialog_close_frames = 0;
            exit_dialog_quit_after_close = false;
            exit_dialog_flash_frames = 0;
            exit_dialog_pending_action = -1;
            return;
        }

        if (exit_dialog_flash_frames <= 0) {
            if (tradeboy::utils::pressed(in.left, edges.prev.left) || tradeboy::utils::pressed(in.right, edges.prev.right)) {
                exit_dialog_selected_btn = 1 - exit_dialog_selected_btn;
            }
            if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
                exit_dialog_pending_action = exit_dialog_selected_btn;
                exit_dialog_flash_frames = 18;
            }
        }
        return;
    }

    // Global M: open exit modal (even if other modals are open).
    if (tradeboy::utils::pressed(in.m, edges.prev.m)) {
        // Close any lower-priority modals to avoid state conflicts under the exit dialog.
        account_address_dialog_open = false;
        account_address_dialog_closing = false;
        account_address_dialog_open_frames = 0;
        account_address_dialog_close_frames = 0;
        account_address_dialog_flash_frames = 0;
        account_address_dialog_pending_action = -1;

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

    if (tradeboy::spotOrder::handle_input(spot_order, in, edges)) {
        return;
    }

    // Account address dialog (common Dialog) has priority over page inputs.
    if (account_address_dialog_open) {
        if (account_address_dialog_closing) {
            return;
        }

        if (account_address_dialog_flash_frames <= 0) {
            if (tradeboy::utils::pressed(in.left, edges.prev.left) || tradeboy::utils::pressed(in.right, edges.prev.right)) {
                account_address_dialog_selected_btn = 1 - account_address_dialog_selected_btn;
            }
            if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
                account_address_dialog_pending_action = account_address_dialog_selected_btn;
                account_address_dialog_flash_frames = 18;
            }
            if (tradeboy::utils::pressed(in.b, edges.prev.b)) {
                account_address_dialog_closing = true;
                account_address_dialog_close_frames = 0;
                account_address_dialog_flash_frames = 0;
                account_address_dialog_pending_action = -1;
            }
        }
        return;
    }

    // Global tab switching (only when not inside order modal)
    if (!spot_order.open) {
        if (tradeboy::utils::pressed(in.l1, edges.prev.l1)) {
            l1_flash_frames = 6;
            if (tab == Tab::Spot) tab = Tab::Account;
            else if (tab == Tab::Perp) tab = Tab::Spot;
            else tab = Tab::Perp;
            return;
        }
        if (tradeboy::utils::pressed(in.r1, edges.prev.r1)) {
            r1_flash_frames = 6;
            if (tab == Tab::Spot) tab = Tab::Perp;
            else if (tab == Tab::Perp) tab = Tab::Account;
            else tab = Tab::Spot;
            return;
        }
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
    } else if (tab == Tab::Account) {
        if (account_flash_timer > 0) {
            account_flash_timer--;
        }

        if (tradeboy::utils::pressed(in.x, edges.prev.x)) {
            account_address_dialog_open = true;
            account_address_dialog_selected_btn = 1;
            account_address_dialog_open_frames = 0;
            account_address_dialog_flash_frames = 0;
            account_address_dialog_pending_action = -1;
            account_address_dialog_closing = false;
            account_address_dialog_close_frames = 0;
            return;
        }

        if (tradeboy::utils::pressed(in.left, edges.prev.left)) {
            account_focused_col = 0;
        }
        if (tradeboy::utils::pressed(in.right, edges.prev.right)) {
            account_focused_col = 1;
        }

        if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
            // Trigger action for focused column
            account_flash_timer = 18;
            account_flash_btn = account_focused_col;
        }
    }
}

void App::render() {
    // Process triggers
    if (buy_trigger_frames > 0) {
        buy_trigger_frames--;
        if (buy_trigger_frames == 0) open_spot_order(true);
    }
    if (sell_trigger_frames > 0) {
        sell_trigger_frames--;
        if (sell_trigger_frames == 0) open_spot_order(false);
    }

    // Flash logic: faster blink while trigger is active.
    // Total trigger duration is still 45 frames, but blink period is shorter.
    bool buy_flash = tradeboy::utils::blink_on(buy_trigger_frames, 6, 3);
    bool sell_flash = tradeboy::utils::blink_on(sell_trigger_frames, 6, 3);

    // Header L1/R1 is one-shot: highlight once, like SpotOrderScreen.
    bool l1_flash = (l1_flash_frames > 0);
    bool r1_flash = (r1_flash_frames > 0);

    tradeboy::spot::SpotUiState ui;
    ui.spot_action_focus = spot_action_focus;
    ui.spot_action_idx = spot_action_idx;
    ui.buy_press_frames = buy_press_frames;
    ui.sell_press_frames = sell_press_frames;

    // Shared background layer (grid)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 size = ImGui::GetContentRegionAvail();
        tradeboy::ui::render_matrix_grid(dl, p, size);
    }

    // Main header for top-level tabs (Spot/Perp/Account)
    if (!spot_order.open) {
        tradeboy::ui::render_main_header(tab, l1_flash, r1_flash, font_bold);
    }

    // Spot page now uses the new UI demo layout. Data layer is intentionally
    // not connected yet (render uses mock data only).
    // Hide Spot page while order page is open to avoid overlap.
    if (!spot_order.open) {
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
        } else if (tab == Tab::Perp) {
            tradeboy::perp::render_perp_screen(font_bold);
        } else {
            tradeboy::account::render_account_screen(
                account_focused_col,
                account_flash_btn,
                account_flash_timer,
                font_bold
            );
        }
    }

    dec_frame_counter(buy_press_frames);
    dec_frame_counter(sell_press_frames);
    dec_frame_counter(l1_flash_frames);
    dec_frame_counter(r1_flash_frames);
    tradeboy::spotOrder::render(spot_order, font_bold);

    // Process account address dialog flash -> trigger closing when finished.
    if (account_address_dialog_open && !account_address_dialog_closing && account_address_dialog_flash_frames > 0) {
        account_address_dialog_flash_frames--;
        if (account_address_dialog_flash_frames == 0 && account_address_dialog_pending_action >= 0) {
            account_address_dialog_closing = true;
            account_address_dialog_close_frames = 0;
            account_address_dialog_pending_action = -1;
        }
    }

    // Account dialog close animation
    if (account_address_dialog_open && account_address_dialog_closing) {
        const int close_dur = 18;
        account_address_dialog_close_frames++;
        if (account_address_dialog_close_frames >= close_dur) {
            account_address_dialog_open = false;
            account_address_dialog_closing = false;
            account_address_dialog_close_frames = 0;
        }
    }

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
                exit_poweroff_anim_active = true;
                exit_poweroff_anim_frames = 0;
            }
            exit_dialog_quit_after_close = false;
        }
    }

    overlay_rect_active = false;
    overlay_rect_uv = ImVec4(0, 0, 0, 0);

    exit_poweroff_anim_t = 0.0f;

    // Boot animation (CRT startup) is disabled once finished; poweroff overrides it.
    if (exit_poweroff_anim_active) {
        boot_anim_active = false;
        boot_anim_frames = 0;
        boot_anim_t = 0.0f;
    } else if (boot_anim_active) {
        const int dur = 72;
        boot_anim_frames++;
        float t = (float)boot_anim_frames / (float)dur;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        boot_anim_t = t;
        if (boot_anim_frames >= dur) {
            boot_anim_active = false;
            boot_anim_frames = 0;
            boot_anim_t = 0.0f;
        }
    }

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

    if (account_address_dialog_open) {
        float open_t = 1.0f;
        if (!account_address_dialog_closing) {
            if (account_address_dialog_open_frames < 18) account_address_dialog_open_frames++;
            open_t = (float)account_address_dialog_open_frames / 18.0f;
        } else {
            const int close_dur = 18;
            open_t = 1.0f - (float)account_address_dialog_close_frames / (float)close_dur;
        }

        tradeboy::ui::render_dialog("AccountAddressDialog",
                                    "> ",
                                    "FULL_WALLET_SIGNATURE\n0x88f273412a8901cde4a1bb22390f12c129e4a1",
                                    "COPY",
                                    "CLOSE",
                                    &account_address_dialog_selected_btn,
                                    account_address_dialog_flash_frames,
                                    open_t,
                                    font_bold,
                                    nullptr);
    }

    // CRT power-off postprocess (after confirm exit, after exit dialog close completes).
    if (exit_poweroff_anim_active) {
        const int dur = 34;
        exit_poweroff_anim_frames++;
        float t = (float)exit_poweroff_anim_frames / (float)dur;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        exit_poweroff_anim_t = t;
        if (exit_poweroff_anim_frames >= dur) {
            quit_requested = true;
        }
    }
}

} // namespace tradeboy::app
