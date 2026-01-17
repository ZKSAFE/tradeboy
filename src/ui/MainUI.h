#pragma once

#include "imgui.h"

#include "../app/App.h"

namespace tradeboy::ui {

void render_main_header(tradeboy::app::Tab tab, bool l1_flash, bool r1_flash, ImFont* font_bold);

} // namespace tradeboy::ui
