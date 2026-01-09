#pragma once

#include <SDL.h>

#include <vector>

namespace tradeboy::app {

struct InputState {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool a = false;
    bool b = false;
    bool l1 = false;
    bool r1 = false;
    bool x = false;
    bool m = false;
};

struct EdgeState {
    InputState prev;
};

InputState poll_input_state_from_events(const std::vector<SDL_Event>& events);

} // namespace tradeboy::app
