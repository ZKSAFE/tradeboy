#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <memory>

#include "imgui.h"

#include "../windows/NumInputWindow.h"
#include "../market/IMarketDataSource.h"
#include "../market/MarketDataService.h"
#include "../model/TradeModel.h"

namespace tradeboy::spot { struct SpotUiEvent; }

namespace tradeboy::app {

enum class Tab {
    Spot = 0,
    Long = 1,
    Short = 2,
    Assets = 3,
};

struct App {
    Tab tab = Tab::Spot;

    int buy_press_frames = 0;
    int sell_press_frames = 0;
    
    // Trigger animation state
    int buy_trigger_frames = 0;
    int sell_trigger_frames = 0;

    int spot_row_idx = 0;
    int spot_action_idx = 0; // 0=buy, 1=sell
    bool spot_action_focus = false;

    // UI feedback state
    bool action_btn_held = false; // A button held
    bool l1_btn_held = false;
    bool r1_btn_held = false;

    tradeboy::windows::NumInputState num_input;

    bool exit_modal_open = false;
    bool quit_requested = false;

    int exit_dialog_selected_btn = 1;
    int exit_dialog_open_frames = 0;
    int exit_dialog_flash_frames = 0;
    int exit_dialog_pending_action = -1; // 0=confirm, 1=cancel
    bool exit_dialog_closing = false;
    int exit_dialog_close_frames = 0;
    bool exit_dialog_quit_after_close = false;

    bool overlay_rect_active = false;
    ImVec4 overlay_rect_uv = ImVec4(0, 0, 0, 0);

    std::string priv_key_hex;

    double wallet_usdc = 0.0;
    double hl_usdc = 0.0;

    tradeboy::model::TradeModel model;
    std::unique_ptr<tradeboy::market::IMarketDataSource> market_src;
    std::unique_ptr<tradeboy::market::MarketDataService> market_service;
    
    ImFont* font_bold = nullptr;

    void startup();
    void shutdown();

    void init_demo_data();
    void load_private_key();
    static void dec_frame_counter(int& v);

    void open_spot_trade(bool buy);

    void apply_spot_ui_events(const std::vector<tradeboy::spot::SpotUiEvent>& ev);

    void handle_input_edges(const tradeboy::app::InputState& in, const tradeboy::app::EdgeState& edges);

    void render();
};

} // namespace tradeboy::app
