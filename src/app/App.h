#pragma once

#include <string>
#include <vector>

#include "../windows/NumInputWindow.h"
#include "../spot/KLineChart.h"

namespace tradeboy::app {

enum class Tab {
    Spot = 0,
    Long = 1,
    Short = 2,
    Assets = 3,
};

struct SpotRow {
    std::string sym;
    double price;
    double balance;
};

struct App {
    Tab tab = Tab::Spot;

    int tf_idx = 0; // 0=24H, 1=4H, 2=1H

    int x_press_frames = 0;
    int buy_press_frames = 0;
    int sell_press_frames = 0;

    std::vector<SpotRow> spot_rows;
    int spot_row_idx = 0;
    int spot_action_idx = 0; // 0=buy, 1=sell
    bool spot_action_focus = false;

    tradeboy::windows::NumInputState num_input;

    bool exit_modal_open = false;
    bool quit_requested = false;

    std::string priv_key_hex;

    double wallet_usdc = 0.0;
    double hl_usdc = 0.0;

    std::vector<tradeboy::spot::OHLC> kline_data;
    void regenerate_kline();

    void init_demo_data();
    void load_private_key();

    void next_timeframe();
    static void dec_frame_counter(int& v);

    void open_spot_trade(bool buy);

    void handle_input_edges(const tradeboy::app::InputState& in, const tradeboy::app::EdgeState& edges);

    void render();
};

} // namespace tradeboy::app
