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
#include "../ui/DialogState.h"
#include "../ui/NumberInputModal.h"

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
    int account_selected_btn = 0; // 0=S<>P, 1=Withdraw, 2=Deposit
    int account_flash_timer = 0;
    int account_flash_btn = -1; // 0=S<>P, 1=Withdraw, 2=Deposit

    // Unified dialog states
    tradeboy::ui::DialogState exit_dialog;
    tradeboy::ui::DialogState alert_dialog;
    tradeboy::ui::DialogState account_address_dialog;

    tradeboy::ui::DialogState internal_transfer_dialog;

    tradeboy::ui::NumberInputState internal_transfer_amount;

    tradeboy::ui::NumberInputState withdraw_amount;

    tradeboy::ui::NumberInputState deposit_amount;

    int internal_transfer_pending_dir = -1; // 0=SPOT->PERP, 1=PERP->SPOT

    // Exit dialog specific state
    bool exit_dialog_quit_after_close = false;

    // UI feedback state
    bool action_btn_held = false; // A button held
    bool l1_btn_held = false;
    bool r1_btn_held = false;

    int l1_flash_frames = 0;
    int r1_flash_frames = 0;

    tradeboy::spotOrder::SpotOrderState spot_order;

    bool quit_requested = false;

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

    std::atomic<bool> hl_transfer_inflight{false};
    std::atomic<bool> hl_transfer_alert_pending{false};
    std::atomic<bool> hl_transfer_refresh_requested{false};
    mutable pthread_mutex_t hl_transfer_mu;
    std::string hl_transfer_alert_body;

    std::thread hl_transfer_thread;

    std::atomic<bool> hl_withdraw_inflight{false};
    std::atomic<bool> hl_withdraw_alert_pending{false};
    mutable pthread_mutex_t hl_withdraw_mu;
    std::string hl_withdraw_alert_body;

    std::thread hl_withdraw_thread;

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

    void set_alert(const std::string& body);
};

} // namespace tradeboy::app
