#include "app/App.h"

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
#include "utils/File.h"
#include "utils/Flash.h"
#include "utils/Typewriter.h"
#include "wallet/Wallet.h"
#include "arb/ArbitrumRpc.h"

#include <cstdio>
#include <chrono>

#include "../ui/MatrixBackground.h"
#include "../ui/MatrixTheme.h"
#include "../ui/MainUI.h"

#include "../ui/Dialog.h"

extern void log_to_file(const char* fmt, ...);

namespace tradeboy::app {

static std::string make_address_short(const std::string& addr_0x) {
    if (addr_0x.size() < 10) return addr_0x;
    std::string s = addr_0x;
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
    if (s.size() <= 10) return addr_0x;
    std::string a = s.substr(0, 4);
    std::string b = s.substr(s.size() - 4);
    return std::string("0x") + a + "..." + b;
}

static void set_alert(tradeboy::app::App& app, const std::string& body) {
    app.alert_dialog_open = true;
    app.alert_dialog_open_frames = 0;
    app.alert_dialog_flash_frames = 0;
    app.alert_dialog_closing = false;
    app.alert_dialog_close_frames = 0;
    app.alert_dialog_body = body;
}

void App::init_demo_data() {
    log_to_file("[App] init_demo_data enter\n");
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

    log_to_file("[App] init_demo_data rows=%d\n", (int)rows.size());

    uint32_t seed = (uint32_t)(SDL_GetTicks() ^ 0xA53C9E17u);
    log_to_file("[App] init_demo_data seed=%u\n", (unsigned int)seed);
    auto rnd01 = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return (double)((seed >> 8) & 0xFFFFu) / 65535.0;
    };
    for (auto& r : rows) {
        log_to_file("[App] init_demo_data sym=%s price=%f\n", r.sym.c_str(), r.price);
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

    log_to_file("[App] init_demo_data applying to model\n");

    model.set_spot_rows(std::move(rows));
    model.set_spot_row_idx(spot_row_idx);

    log_to_file("[App] init_demo_data exit\n");
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

    bool created = false;
    std::string err;
    if (!tradeboy::wallet::load_or_create_config("./tradeboy.cfg", wallet_cfg, created, err)) {
        set_alert(*this, std::string("RPC_CONFIG_ERROR\n") + err);
    } else {
        wallet_address_short = make_address_short(wallet_cfg.wallet_address);
        if (created) {
            set_alert(*this, std::string("NEW_WALLET_CREATED\n") + wallet_cfg.wallet_address);
        }
    }

    arb_eth_str = "UNKNOWN";
    arb_usdc_str = "UNKNOWN";
    arb_gas_str = "GAS: UNKNOWN";
    arb_rpc_last_ok = false;
    arb_rpc_error_pending_alert.store(false);

    arb_rpc_stop = false;
    arb_rpc_thread = std::thread([this]() {
        while (!arb_rpc_stop.load()) {
            tradeboy::arb::WalletOnchainData d;
            std::string e;
            bool ok = tradeboy::arb::fetch_wallet_data(wallet_cfg.arb_rpc_url, wallet_cfg.wallet_address, d, e);

            if (ok && d.rpc_ok) {
                {
                    std::lock_guard<std::mutex> lk(arb_rpc_mu);
                    arb_eth_str = d.eth_balance;
                    arb_usdc_str = d.usdc_balance;
                    arb_gas_str = d.gas;
                    arb_gas_price_wei = d.gas_price_wei;
                }
                arb_rpc_last_ok = true;
            } else {
                {
                    std::lock_guard<std::mutex> lk(arb_rpc_mu);
                    arb_eth_str = "UNKNOWN";
                    arb_usdc_str = "UNKNOWN";
                    arb_gas_str = "GAS: UNKNOWN";
                    arb_gas_price_wei = 0.0L;
                }
                if (arb_rpc_last_ok) {
                    arb_rpc_error_pending_alert.store(true);
                }
                arb_rpc_last_ok = false;
            }

            for (int i = 0; i < 20; i++) {
                if (arb_rpc_stop.load()) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    });
}

void App::shutdown() {
    log_to_file("[App] shutdown()\n");
    arb_rpc_stop = true;
    if (arb_rpc_thread.joinable()) {
        arb_rpc_thread.join();
    }
    if (market_service) {
        market_service->stop();
        market_service.reset();
    }
    market_src.reset();
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

    if (alert_dialog_open) {
        if (alert_dialog_closing) {
            return;
        }
        if (alert_dialog_flash_frames <= 0) {
            if (tradeboy::utils::pressed(in.a, edges.prev.a) || tradeboy::utils::pressed(in.b, edges.prev.b)) {
                alert_dialog_flash_frames = 18;
            }
        }
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
                // 0=PRIVATE_KEY, 1=CLOSE
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
            std::string eth_s;
            std::string usdc_s;
            std::string gas_s;
            long double gas_price_wei = 0.0L;
            {
                std::lock_guard<std::mutex> lk(arb_rpc_mu);
                eth_s = arb_eth_str;
                usdc_s = arb_usdc_str;
                gas_s = arb_gas_str;
                gas_price_wei = arb_gas_price_wei;
            }

            // Estimate Arbitrum USDC transfer fee in USD.
            // Gas limit reference: 75,586
            double eth_mid = 0.0;
            {
                tradeboy::model::TradeModelSnapshot snap = model.snapshot();
                for (const auto& r : snap.spot_rows) {
                    if (r.sym == "ETH") {
                        eth_mid = r.price;
                        break;
                    }
                }
            }

            std::string fee_s = "TRANSATION FEE: $UNKNOWN";
            if (gas_price_wei > 0.0L && eth_mid > 0.0) {
                const long double gas_limit = 75586.0L;
                long double fee_eth = (gas_price_wei * gas_limit) / 1000000000000000000.0L;
                long double fee_usd = fee_eth * (long double)eth_mid;
                // 4 decimals
                char buf[64];
                std::snprintf(buf, sizeof(buf), "TRANSATION FEE: $%.4Lf", fee_usd);
                fee_s = buf;
            }
            arb_tx_fee_str = fee_s;

            tradeboy::account::render_account_screen(
                account_focused_col,
                account_flash_btn,
                account_flash_timer,
                font_bold,
                wallet_address_short.c_str(),
                eth_s.c_str(),
                usdc_s.c_str(),
                gas_s.c_str(),
                arb_tx_fee_str.c_str()
            );
        }
    }

    dec_frame_counter(buy_press_frames);
    dec_frame_counter(sell_press_frames);
    dec_frame_counter(l1_flash_frames);
    dec_frame_counter(r1_flash_frames);
    tradeboy::spotOrder::render(spot_order, font_bold);

    // Process alert dialog flash -> trigger closing when finished.
    if (alert_dialog_open && !alert_dialog_closing && alert_dialog_flash_frames > 0) {
        alert_dialog_flash_frames--;
        if (alert_dialog_flash_frames == 0) {
            alert_dialog_closing = true;
            alert_dialog_close_frames = 0;
        }
    }

    // Process account address dialog flash -> trigger closing when finished.
    if (account_address_dialog_open && !account_address_dialog_closing && account_address_dialog_flash_frames > 0) {
        account_address_dialog_flash_frames--;
        if (account_address_dialog_flash_frames == 0 && account_address_dialog_pending_action >= 0) {
            account_address_dialog_closing = true;
            account_address_dialog_close_frames = 0;
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

            if (account_address_dialog_pending_action == 0) {
                set_alert(*this, std::string("PRIVATE_KEY\n") + wallet_cfg.private_key);
            }
            account_address_dialog_pending_action = -1;
        }
    }

    // Alert dialog close animation
    if (alert_dialog_open && alert_dialog_closing) {
        const int close_dur = 18;
        alert_dialog_close_frames++;
        if (alert_dialog_close_frames >= close_dur) {
            alert_dialog_open = false;
            alert_dialog_closing = false;
            alert_dialog_close_frames = 0;
            alert_dialog_open_frames = 0;
        }
    }

    if (arb_rpc_error_pending_alert.exchange(false)) {
        set_alert(*this, "RPC_CONNECTION_FAILED");
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
                                    "Exit TradeBoy?\n\nDEVICE INFO\n- Model: RG34XX\n- OS: Ubuntu 22.04\n- Arch: armhf\n- Build: tradeboy-armhf\n- Display: 720x480\n- Renderer: SDL2 + OpenGL\n- Storage: /mnt/mmc\n- AppDir: /mnt/mmc/Roms/APPS\n- RPC: Arbitrum JSON-RPC\n- HL: Hyperliquid\n\nNETWORK\n- SSH: root@<device-ip>\n- WLAN: <ssid>\n- IP: <ip>\n\nWALLET\n- Address: <0x...>\n- USDC: <balance>\n- ETH: <balance>\n- GAS: <gwei>\n\nNOTES\nThis is a long diagnostic message used to test:\n1) auto-wrap across dialog width\n2) dialog height auto-grow up to 450px\n3) truncation when max height reached\n4) last visible line replaced with '...'\n\nLorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.\n\nMore lines:\n- Line A\n- Line B\n- Line C\n- Line D\n- Line E\n- Line F\n- Line G\n- Line H\n- Line I\n- Line J\n- Line K\n- Line L\n- Line M\n- Line N\n- Line O\n- Line P\n- Line Q\n- Line R\n- Line S\n- Line T\n- Line U\n- Line V\n- Line W\n- Line X\n- Line Y\n- Line Z",
                                    "EXIT",
                                    "CANCEL",
                                    &exit_dialog_selected_btn,
                                    exit_dialog_flash_frames,
                                    open_t,
                                    font_bold,
                                    nullptr);
    }

    if (alert_dialog_open) {
        float open_t = 1.0f;
        if (!alert_dialog_closing) {
            if (alert_dialog_open_frames < 18) alert_dialog_open_frames++;
            open_t = (float)alert_dialog_open_frames / 18.0f;
        } else {
            const int close_dur = 18;
            open_t = 1.0f - (float)alert_dialog_close_frames / (float)close_dur;
        }

        int sel = 1;
        tradeboy::ui::render_dialog("AlertDialog",
                                    "> ",
                                    alert_dialog_body,
                                    "",
                                    "OK",
                                    &sel,
                                    alert_dialog_flash_frames,
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
                                    std::string("WALLET_ADDRESS\n") + wallet_cfg.wallet_address,
                                    "PRIVATE_KEY",
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
