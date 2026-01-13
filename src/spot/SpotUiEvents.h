#pragma once

#include <vector>

#include "../app/Input.h"
#include "../utils/Math.h"

namespace tradeboy::spot {

struct SpotUiState {
    bool spot_action_focus = false;
    int spot_action_idx = 0;
    int buy_press_frames = 0;
    int sell_press_frames = 0;
};

enum class SpotUiEventType {
    RowDelta,
    EnterActionFocus,
    ExitActionFocus,
    SetActionIdx,
    TriggerAction
};

struct SpotUiEvent {
    SpotUiEventType type;
    int value = 0;
    bool flag = false;

    SpotUiEvent() = default;
    SpotUiEvent(SpotUiEventType type, int value, bool flag) : type(type), value(value), flag(flag) {}
};

inline std::vector<SpotUiEvent> collect_spot_ui_events(const tradeboy::app::InputState& in,
                                                      const tradeboy::app::EdgeState& edges,
                                                      const SpotUiState& ui) {
    std::vector<SpotUiEvent> ev;

    if (tradeboy::utils::pressed(in.up, edges.prev.up)) {
        ev.push_back(SpotUiEvent(SpotUiEventType::RowDelta, -1, false));
    }
    if (tradeboy::utils::pressed(in.down, edges.prev.down)) {
        ev.push_back(SpotUiEvent(SpotUiEventType::RowDelta, +1, false));
    }

    if (ui.spot_action_focus) {
        if (tradeboy::utils::pressed(in.left, edges.prev.left)) {
            if (ui.spot_action_idx == 1) ev.push_back(SpotUiEvent(SpotUiEventType::SetActionIdx, 0, false));
        }
        if (tradeboy::utils::pressed(in.right, edges.prev.right)) {
            if (ui.spot_action_idx == 0) ev.push_back(SpotUiEvent(SpotUiEventType::SetActionIdx, 1, false));
        }
        if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
            const bool buy = (ui.spot_action_idx == 0);
            ev.push_back(SpotUiEvent(SpotUiEventType::TriggerAction, 0, buy));
        }
        if (tradeboy::utils::pressed(in.b, edges.prev.b)) {
            ev.push_back(SpotUiEvent(SpotUiEventType::ExitActionFocus, 0, false));
        }
    } else {
        if (tradeboy::utils::pressed(in.left, edges.prev.left)) {
            ev.push_back(SpotUiEvent(SpotUiEventType::EnterActionFocus, 0, false));
        }
        if (tradeboy::utils::pressed(in.right, edges.prev.right)) {
            ev.push_back(SpotUiEvent(SpotUiEventType::EnterActionFocus, 1, false));
        }
        if (tradeboy::utils::pressed(in.a, edges.prev.a)) {
            ev.push_back(SpotUiEvent(SpotUiEventType::TriggerAction, 0, true));
        }
    }

    return ev;
}

} // namespace tradeboy::spot
