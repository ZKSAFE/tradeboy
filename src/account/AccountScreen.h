#pragma once

#include "imgui.h"

namespace tradeboy::account {

// Renders the Account screen UI.
// selected_btn: 0=S<>P, 1=Withdraw, 2=Deposit
void render_account_screen(int selected_btn,
                           int flash_btn,
                           int flash_timer,
                           ImFont* font_bold,
                           const char* hl_usdc,
                           const char* hl_perp_usdc,
                           const char* hl_total_asset,
                           const char* hl_pnl_24h,
                           const char* hl_pnl_24h_pct,
                           const char* arb_address_short,
                           const char* arb_eth,
                           const char* arb_usdc,
                           const char* arb_gas,
                           const char* arb_fee);

} // namespace tradeboy::account
