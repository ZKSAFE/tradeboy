#pragma once

#include <vector>

#include "../app/Input.h"
#include "../utils/Math.h"

namespace tradeboy::perp {

struct PerpUiState {
    bool action_focus = false;
    int action_idx = 0;
    int primary_press_frames = 0;
    int close_press_frames = 0;
};

enum class PerpUiEventType {
    RowDelta,
    PageDelta,
    EnterActionFocus,
    ExitActionFocus,
    SetActionIdx,
    TriggerAction
};

struct PerpUiEvent {
    PerpUiEventType type;
    int value = 0;
    bool flag = false;

    PerpUiEvent() = default;
    PerpUiEvent(PerpUiEventType type, int value, bool flag) : type(type), value(value), flag(flag) {}
};

inline std::vector<PerpUiEvent> collect_perp_ui_events(const tradeboy::app::InputState& in,
                                                       const tradeboy::app::EdgeState& edges,
                                                       const PerpUiState& ui) {
    std::vector<PerpUiEvent> ev;

    static int up_hold = 0;
    static int down_hold = 0;
    const int initial_delay = 16;
    const int repeat_interval = 3;

    if (!in.up) up_hold = 0;
    if (!in.down) down_hold = 0;

    if (tradeboy::utils::pressed(in.up, edges.prev.up)) {
        ev.push_back(PerpUiEvent(PerpUiEventType::RowDelta, -1, false));
        up_hold = 1;
    }
    if (tradeboy::utils::pressed(in.down, edges.prev.down)) {
        ev.push_back(PerpUiEvent(PerpUiEventType::RowDelta, +1, false));
        down_hold = 1;
    }

    if (in.up && !tradeboy::utils::pressed(in.up, edges.prev.up)) {
        up_hold++;
        if (up_hold >= initial_delay && ((up_hold - initial_delay) % repeat_interval) == 0) {
            ev.push_back(PerpUiEvent(PerpUiEventType::RowDelta, -1, false));
        }
    }
    if (in.down && !tradeboy::utils::pressed(in.down, edges.prev.down)) {
        down_hold++;
        if (down_hold >= initial_delay && ((down_hold - initial_delay) % repeat_interval) == 0) {
            ev.push_back(PerpUiEvent(PerpUiEventType::RowDelta, +1, false));
        }
    }

    if (tradeboy::utils::pressed(in.l2, edges.prev.l2)) {
        ev.push_back(PerpUiEvent(PerpUiEventType::PageDelta, -1, false));
    }
    if (tradeboy::utils::pressed(in.r2, edges.prev.r2)) {
        ev.push_back(PerpUiEvent(PerpUiEventType::PageDelta, +1, false));
    }

    if (ui.action_focus) {
        if (tradeboy::utils::pressed(in.left, edges.prev.left)) {
            if (ui.action_idx == 1) ev.push_back(PerpUiEvent(PerpUiEventType::SetActionIdx, 0, false));
        }
        if (tradeboy::utils::pressed(in.right, edges.prev.right)) {
            if (ui.action_idx == 0) ev.push_back(PerpUiEvent(PerpUiEventType::SetActionIdx, 1, false));
        }
        if (tradeboy::utils::pressed(in.b, edges.prev.b)) {
            ev.push_back(PerpUiEvent(PerpUiEventType::ExitActionFocus, 0, false));
        }
        if (!in.a && edges.prev.a) {
            const bool primary = (ui.action_idx == 0);
            ev.push_back(PerpUiEvent(PerpUiEventType::TriggerAction, 0, primary));
        }
    } else {
        if (tradeboy::utils::pressed(in.left, edges.prev.left)) {
            ev.push_back(PerpUiEvent(PerpUiEventType::EnterActionFocus, 0, false));
        }
        if (tradeboy::utils::pressed(in.right, edges.prev.right)) {
            ev.push_back(PerpUiEvent(PerpUiEventType::EnterActionFocus, 1, false));
        }
        if (!in.a && edges.prev.a) {
            ev.push_back(PerpUiEvent(PerpUiEventType::TriggerAction, 0, true));
        }
    }

    return ev;
}

} // namespace tradeboy::perp
