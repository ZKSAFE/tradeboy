#pragma once

#include "imgui.h"

namespace tradeboy::account {

// Renders the Account screen UI.
// focused_col: 0 for Hyperliquid, 1 for Arbitrum
void render_account_screen(int focused_col, int flash_btn, int flash_timer, ImFont* font_bold);

} // namespace tradeboy::account
