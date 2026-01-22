#pragma once

#include "imgui.h"

namespace tradeboy::account {

// Renders the Account screen UI.
// focused_col: 0 for Hyperliquid, 1 for Arbitrum
void render_account_screen(int focused_col,
                           int flash_btn,
                           int flash_timer,
                           ImFont* font_bold,
                           const char* hl_usdc,
                           const char* hl_perp_usdc,
                           const char* arb_address_short,
                           const char* arb_eth,
                           const char* arb_usdc,
                           const char* arb_gas,
                           const char* arb_fee);

} // namespace tradeboy::account
