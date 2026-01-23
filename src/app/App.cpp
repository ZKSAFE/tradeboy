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
#include "market/Hyperliquid.h"

#include "utils/Log.h"

#include <cstdio>
#include <chrono>

#include "../ui/MatrixBackground.h"
#include "../ui/MatrixTheme.h"
#include "../ui/MainUI.h"

#include "../ui/Dialog.h"

namespace tradeboy::app {

App::App() {
    pthread_mutex_init(&arb_deposit_mu, nullptr);
}

App::~App() {
    pthread_mutex_destroy(&arb_deposit_mu);
}

static std::string make_address_short(const std::string& addr_0x) {
    if (addr_0x.size() < 10) return addr_0x;
    std::string s = addr_0x;
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
    if (s.size() <= 10) return addr_0x;
    std::string a = s.substr(0, 4);
    std::string b = s.substr(s.size() - 4);
    return std::string("0x") + a + "..." + b;
}

void App::set_alert(const std::string& body) {
    alert_dialog.open_dialog(body, 1);
}

static void set_deposit_alert(tradeboy::app::App& app, const std::string& body) {
    pthread_mutex_lock(&app.arb_deposit_mu);
    app.arb_deposit_alert_body = body;
    pthread_mutex_unlock(&app.arb_deposit_mu);
    app.arb_deposit_alert_pending.store(true);
}

static void set_alert_static(tradeboy::app::App& app, const std::string& body) {
    app.set_alert(body);
}

void App::init_demo_data() {
    log_str("[App] init_demo_data enter\n");
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

    log_str("[App] init_demo_data rows ready\n");

    uint32_t seed = (uint32_t)(SDL_GetTicks() ^ 0xA53C9E17u);
    log_str("[App] init_demo_data rng seeded\n");
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

    log_str("[App] init_demo_data applying to model\n");

    log_str("[App] init_demo_data call set_spot_rows\n");
    model.set_spot_rows(std::move(rows));

    log_str("[App] init_demo_data returned set_spot_rows\n");
    log_str("[App] init_demo_data call set_spot_row_idx\n");
    model.set_spot_row_idx(spot_row_idx);

    log_str("[App] init_demo_data returned set_spot_row_idx\n");

    log_str("[App] init_demo_data exit\n");
}

void App::startup() {
    boot_anim_active = true;
    boot_anim_frames = 0;
    boot_anim_t = 0.0f;

    bool created = false;
    std::string err;
    if (!tradeboy::wallet::load_or_create_config("./tradeboy.cfg", wallet_cfg, created, err)) {
        set_alert(std::string("RPC_CONFIG_ERROR\n") + err);
    } else {
        model.set_wallet(wallet_cfg.wallet_address, wallet_cfg.private_key);
        wallet_address_short = make_address_short(wallet_cfg.wallet_address);
        if (created) {
            set_alert(std::string("NEW_WALLET_CREATED\n") + wallet_cfg.wallet_address);
        }
    }

    if (!market_src) {
        log_str("[App] Market source: WS (forced)\n");
        market_src.reset(new tradeboy::market::HyperliquidWsDataSource());
    }
    if (!market_service) {
        market_service.reset(new tradeboy::market::MarketDataService(model, *market_src));
    }
    if (!wallet_cfg.wallet_address.empty()) {
        market_src->set_user_address(wallet_cfg.wallet_address);
    }
    market_service->start();

    if (!arb_rpc_service) {
        arb_rpc_service.reset(new tradeboy::arb::ArbitrumRpcService(model,
                                                                    wallet_cfg.arb_rpc_url,
                                                                    wallet_cfg.wallet_address));
    } else {
        arb_rpc_service->set_wallet(wallet_cfg.arb_rpc_url, wallet_cfg.wallet_address);
    }
    arb_rpc_service->start();
    arb_rpc_last_ok = false;
}

void App::shutdown() {
    log_str("[App] shutdown()\n");
    if (arb_deposit_thread.joinable()) {
        arb_deposit_thread.join();
    }
    if (arb_rpc_service) {
        arb_rpc_service->stop();
        arb_rpc_service.reset();
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
    const tradeboy::model::AccountSnapshot account = model.account_snapshot();
    double maxv = 0.0;
    if (buy) {
        maxv = (row.price > 0.0) ? (account.hl_usdc / row.price) : 0.0;
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

    if (alert_dialog.open) {
        if (alert_dialog.closing) {
            return;
        }
        if (alert_dialog.flash_frames <= 0) {
            if (tradeboy::utils::pressed(in.a, edges.prev.a) || tradeboy::utils::pressed(in.b, edges.prev.b)) {
                alert_dialog.start_flash(1);
            }
        }
        return;
    }

    // Exit modal has highest priority.
    if (exit_dialog.open) {
        if (exit_dialog.closing) {
            return;
        }

        // Always allow B to close/cancel, even while flashing.
        if (tradeboy::utils::pressed(in.b, edges.prev.b)) {
            exit_dialog.start_close();
            exit_dialog_quit_after_close = false;
            return;
        }

        if (exit_dialog.flash_frames <= 0) {
            if (tradeboy::utils::pressed(in.left, edges.prev.left) || tradeboy::utils::pressed(in.right, edges.prev.right)) {
                exit_dialog.selected_btn = 1 - exit_dialog.selected_btn;
            }
            if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
                exit_dialog.start_flash(exit_dialog.selected_btn);
            }
        }
        return;
    }

    // Global M: open exit modal (even if other modals are open).
    if (tradeboy::utils::pressed(in.m, edges.prev.m)) {
        // Close any lower-priority modals to avoid state conflicts under the exit dialog.
        account_address_dialog.reset();

        exit_dialog.open_dialog("", 1);
        exit_dialog_quit_after_close = false;
        return;
    }

    if (tradeboy::spotOrder::handle_input(spot_order, in, edges)) {
        return;
    }

    // Account address dialog (common Dialog) has priority over page inputs.
    if (account_address_dialog.open) {
        if (account_address_dialog.closing) {
            return;
        }

        if (account_address_dialog.flash_frames <= 0) {
            if (tradeboy::utils::pressed(in.left, edges.prev.left) || tradeboy::utils::pressed(in.right, edges.prev.right)) {
                account_address_dialog.selected_btn = 1 - account_address_dialog.selected_btn;
            }
            if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
                // 0=PRIVATE_KEY, 1=CLOSE
                account_address_dialog.start_flash(account_address_dialog.selected_btn);
            }
            if (tradeboy::utils::pressed(in.b, edges.prev.b)) {
                account_address_dialog.start_close();
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
        if (tradeboy::utils::pressed(in.x, edges.prev.x)) {
            account_address_dialog.open_dialog("", 1);
            return;
        }

        // Account: left/right cycles the 3 bottom buttons.
        if (tradeboy::utils::pressed(in.left, edges.prev.left)) {
            account_selected_btn = (account_selected_btn + 2) % 3;
        }
        if (tradeboy::utils::pressed(in.right, edges.prev.right)) {
            account_selected_btn = (account_selected_btn + 1) % 3;
        }

        if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
            account_flash_timer = 18;
            account_flash_btn = account_selected_btn;
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

    // Arbitrum deposit trigger: when flash completes, perform action.
    if (tab == Tab::Account && account_flash_timer > 0) {
        account_flash_timer--;
        if (account_flash_timer == 0 && account_flash_btn == 2) {
            if (!arb_deposit_inflight.exchange(true)) {
                const tradeboy::model::WalletSnapshot w = model.wallet_snapshot();
                const std::string rpc_url = wallet_cfg.arb_rpc_url;
                if (rpc_url.empty() || w.wallet_address.empty() || w.private_key.empty()) {
                    set_alert_static(*this, "DEPOSIT_FAILED\nMISSING_WALLET");
                    arb_deposit_inflight.store(false);
                } else {
                    if (arb_deposit_thread.joinable()) {
                        arb_deposit_thread.join();
                    }
                    const std::string to_addr = "0x2Df1c51E09aECF9cacB7bc98cB1742757f163dF7";
                    const unsigned long long amount_micro = 6000000ULL; // 6 USDC (6 decimals)
                    arb_deposit_thread = std::thread([this, rpc_url, w, to_addr, amount_micro]() {
                        std::string txh;
                        std::string err;
                        bool ok = tradeboy::arb::send_usdc_transfer_test(rpc_url,
                                                                         w.wallet_address,
                                                                         w.private_key,
                                                                         to_addr,
                                                                         amount_micro,
                                                                         txh,
                                                                         err);
                        if (ok) {
                            set_deposit_alert(*this, std::string("DEPOSIT_SENT\n") + txh);
                        } else {
                            set_deposit_alert(*this, std::string("DEPOSIT_FAILED\n") + err);
                        }
                        arb_deposit_inflight.store(false);
                    });
                }
            }
        }
    }

    // Flash logic: faster blink while trigger is active.
    // Total trigger duration is still 45 frames, but blink period is shorter.
    bool buy_flash = tradeboy::utils::blink_on(buy_trigger_frames, 6, 3);
    bool sell_flash = tradeboy::utils::blink_on(sell_trigger_frames, 6, 3);

    static const int TRADEMODEL_SNAPSHOT_ABI_GUARD = 1;
    (void)TRADEMODEL_SNAPSHOT_ABI_GUARD;

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
            tradeboy::model::AccountSnapshot account = model.account_snapshot();
            const std::string eth_s = account.arb_eth_str.empty() ? "UNKNOWN" : account.arb_eth_str;
            const std::string usdc_s = account.arb_usdc_str.empty() ? "UNKNOWN" : account.arb_usdc_str;
            const std::string gas_s = account.arb_gas_str.empty() ? "GAS: UNKNOWN" : account.arb_gas_str;
            const long double gas_price_wei = account.arb_gas_price_wei;
            const std::string hl_usdc_s = account.hl_usdc_str.empty() ? "UNKNOWN" : account.hl_usdc_str;
            const std::string hl_perp_usdc_s = account.hl_perp_usdc_str.empty() ? "UNKNOWN" : account.hl_perp_usdc_str;
            const std::string hl_total_asset_s = account.hl_total_asset_str.empty() ? "UNKNOWN" : account.hl_total_asset_str;
            const std::string hl_pnl_24h_s = account.hl_pnl_24h_str.empty() ? "UNKNOWN" : account.hl_pnl_24h_str;
            const std::string hl_pnl_24h_pct_s = account.hl_pnl_24h_pct_str.empty() ? "UNKNOWN" : account.hl_pnl_24h_pct_str;

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
                account_selected_btn,
                account_flash_btn,
                account_flash_timer,
                font_bold,
                hl_usdc_s.c_str(),
                hl_perp_usdc_s.c_str(),
                hl_total_asset_s.c_str(),
                hl_pnl_24h_s.c_str(),
                hl_pnl_24h_pct_s.c_str(),
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
    if (alert_dialog.open && !alert_dialog.closing) {
        if (alert_dialog.tick_flash()) {
            alert_dialog.start_close();
        }
    }

    // Process account address dialog flash -> trigger closing when finished.
    if (account_address_dialog.open && !account_address_dialog.closing) {
        if (account_address_dialog.tick_flash()) {
            account_address_dialog.start_close();
        }
    }

    // Account dialog close animation
    if (account_address_dialog.open && account_address_dialog.closing) {
        if (account_address_dialog.tick_close_anim()) {
            int action = account_address_dialog.pending_action;
            account_address_dialog.reset();

            if (action == 0) {
                set_alert(std::string("PRIVATE_KEY\n") + model.wallet_snapshot().private_key);
            }
        }
    }

    // Alert dialog close animation
    if (alert_dialog.open && alert_dialog.closing) {
        if (alert_dialog.tick_close_anim()) {
            alert_dialog.reset();
        }
    }

    {
        const bool now_ok = model.account_snapshot().arb_rpc_ok;
        if (!now_ok && arb_rpc_last_ok) {
            set_alert("RPC_CONNECTION_FAILED");
        }
        arb_rpc_last_ok = now_ok;
    }

    if (arb_deposit_alert_pending.exchange(false)) {
        std::string body;
        {
            pthread_mutex_lock(&arb_deposit_mu);
            body = arb_deposit_alert_body;
            pthread_mutex_unlock(&arb_deposit_mu);
        }
        set_alert(body);
    }

    // Process exit dialog flash -> trigger closing when finished.
    if (exit_dialog.open && !exit_dialog.closing) {
        if (exit_dialog.tick_flash()) {
            exit_dialog_quit_after_close = (exit_dialog.pending_action == 0);
            exit_dialog.start_close();
        }
    }

    // Close animation
    if (exit_dialog.open && exit_dialog.closing) {
        if (exit_dialog.tick_close_anim()) {
            exit_dialog.reset();
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

    if (exit_dialog.open) {
        exit_dialog.tick_open_anim();
        float open_t = exit_dialog.get_open_t();

        tradeboy::ui::render_dialog("ExitDialog",
                                    "> ",
                                    "Exit TradeBoy?\n\nDEVICE INFO\n- Model: RG34XX\n- OS: Ubuntu 22.04\n- Arch: armhf\n- Build: tradeboy-armhf\n- Display: 720x480\n- Renderer: SDL2 + OpenGL\n- Storage: /mnt/mmc\n- AppDir: /mnt/mmc/Roms/APPS\n- RPC: Arbitrum JSON-RPC\n- HL: Hyperliquid\n\nNETWORK\n- SSH: root@<device-ip>\n- WLAN: <ssid>\n- IP: <ip>\n\nWALLET\n- Address: <0x...>\n- USDC: <balance>\n- ETH: <balance>\n- GAS: <gwei>\n\nNOTES\nThis is a long diagnostic message used to test:\n1) auto-wrap across dialog width\n2) dialog height auto-grow up to 450px\n3) truncation when max height reached\n4) last visible line replaced with '...'\n\nLorem ipsum dolor sit amet, consectetur adipiscing elit. Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint occaecat cupidatat non proident, sunt in culpa qui officia deserunt mollit anim id est laborum.\n\nMore lines:\n- Line A\n- Line B\n- Line C\n- Line D\n- Line E\n- Line F\n- Line G\n- Line H\n- Line I\n- Line J\n- Line K\n- Line L\n- Line M\n- Line N\n- Line O\n- Line P\n- Line Q\n- Line R\n- Line S\n- Line T\n- Line U\n- Line V\n- Line W\n- Line X\n- Line Y\n- Line Z",
                                    "EXIT",
                                    "CANCEL",
                                    &exit_dialog.selected_btn,
                                    exit_dialog.flash_frames,
                                    open_t,
                                    font_bold,
                                    nullptr);
    }

    if (alert_dialog.open) {
        alert_dialog.tick_open_anim();
        float open_t = alert_dialog.get_open_t();

        int sel = 1;
        tradeboy::ui::render_dialog("AlertDialog",
                                    "> ",
                                    alert_dialog.body,
                                    "",
                                    "OK",
                                    &sel,
                                    alert_dialog.flash_frames,
                                    open_t,
                                    font_bold,
                                    nullptr);
    }

    if (account_address_dialog.open) {
        account_address_dialog.tick_open_anim();
        float open_t = account_address_dialog.get_open_t();

        tradeboy::ui::render_dialog("AccountAddressDialog",
                                    "> ",
                                    std::string("WALLET_ADDRESS\n") + model.wallet_snapshot().wallet_address,
                                    "PRIVATE_KEY",
                                    "CLOSE",
                                    &account_address_dialog.selected_btn,
                                    account_address_dialog.flash_frames,
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
