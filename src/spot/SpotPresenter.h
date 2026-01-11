#pragma once

#include "SpotViewModel.h"

#include "../model/TradeModel.h"

namespace tradeboy::spot {

struct SpotUiState {
    bool spot_action_focus = false;
    int spot_action_idx = 0;
    int x_press_frames = 0;
    int buy_press_frames = 0;
    int sell_press_frames = 0;
};

SpotViewModel build_spot_view_model(const tradeboy::model::TradeModelSnapshot& snap, const SpotUiState& ui);

} // namespace tradeboy::spot
