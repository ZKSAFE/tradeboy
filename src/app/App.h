#pragma once

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>

#include <pthread.h>

#include "imgui.h"

#include "../spotOrder/SpotOrderScreen.h"
#include "../market/IMarketDataSource.h"
#include "../market/MarketDataService.h"
#include "../model/TradeModel.h"

#include "../arb/ArbitrumRpcService.h"

#include "../wallet/Wallet.h"

namespace tradeboy::spot { struct SpotUiEvent; }

namespace tradeboy::app {

enum class Tab {
    Spot = 0,
    Perp = 1,
    Account = 2,
};

struct App {
    App();
    ~App();

    Tab tab = Tab::Spot;

    int buy_press_frames = 0;
    int sell_press_frames = 0;
    
    // Trigger animation state
    int buy_trigger_frames = 0;
    int sell_trigger_frames = 0;

    int spot_row_idx = 0;
    int spot_action_idx = 0; // 0=buy, 1=sell
    bool spot_action_focus = false;

    // Account state
    int account_focused_col = 0; // 0=Hyperliquid, 1=Arbitrum
    int account_flash_timer = 0;
    int account_flash_btn = -1; // 0=Withdraw, 1=Deposit (based on col)

    bool account_address_dialog_open = false;
    int account_address_dialog_selected_btn = 1;
    int account_address_dialog_open_frames = 0;
    int account_address_dialog_flash_frames = 0;
    int account_address_dialog_pending_action = -1; // 0=confirm, 1=cancel
    bool account_address_dialog_closing = false;
    int account_address_dialog_close_frames = 0;

    bool alert_dialog_open = false;
    int alert_dialog_open_frames = 0;
    int alert_dialog_flash_frames = 0;
    bool alert_dialog_closing = false;
    int alert_dialog_close_frames = 0;
    std::string alert_dialog_body;

    // UI feedback state
    bool action_btn_held = false; // A button held
    bool l1_btn_held = false;
    bool r1_btn_held = false;

    int l1_flash_frames = 0;
    int r1_flash_frames = 0;

    tradeboy::spotOrder::SpotOrderState spot_order;

    bool exit_modal_open = false;
    bool quit_requested = false;

    int exit_dialog_selected_btn = 1;
    int exit_dialog_open_frames = 0;
    int exit_dialog_flash_frames = 0;
    int exit_dialog_pending_action = -1; // 0=confirm, 1=cancel
    bool exit_dialog_closing = false;
    int exit_dialog_close_frames = 0;
    bool exit_dialog_quit_after_close = false;

    bool exit_poweroff_anim_active = false;
    int exit_poweroff_anim_frames = 0;
    float exit_poweroff_anim_t = 0.0f;

    bool boot_anim_active = true;
    int boot_anim_frames = 0;
    float boot_anim_t = 0.0f;

    bool overlay_rect_active = false;
    ImVec4 overlay_rect_uv = ImVec4(0, 0, 0, 0);

    tradeboy::wallet::WalletConfig wallet_cfg;
    std::string wallet_address_short;
    std::string arb_tx_fee_str;

    bool arb_rpc_last_ok = false;

    std::atomic<bool> arb_deposit_inflight{false};
    std::atomic<bool> arb_deposit_alert_pending{false};
    mutable pthread_mutex_t arb_deposit_mu;
    std::string arb_deposit_alert_body;

    std::thread arb_deposit_thread;

    tradeboy::model::TradeModel model;
    std::unique_ptr<tradeboy::market::IMarketDataSource> market_src;
    std::unique_ptr<tradeboy::market::MarketDataService> market_service;
    std::unique_ptr<tradeboy::arb::ArbitrumRpcService> arb_rpc_service;
    
    ImFont* font_bold = nullptr;

    void startup();
    void shutdown();

    void init_demo_data();
    static void dec_frame_counter(int& v);

    void open_spot_order(bool buy);

    void apply_spot_ui_events(const std::vector<tradeboy::spot::SpotUiEvent>& ev);

    void handle_input_edges(const tradeboy::app::InputState& in, const tradeboy::app::EdgeState& edges);

    void render();
};

} // namespace tradeboy::app
