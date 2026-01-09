#include "Input.h"

namespace tradeboy::app {

InputState poll_input_state_from_events(const std::vector<SDL_Event>& events) {
    InputState in;

    auto set_key = [&](SDL_Keycode key) {
        switch (key) {
            case SDLK_UP: in.up = true; break;
            case SDLK_DOWN: in.down = true; break;
            case SDLK_LEFT: in.left = true; break;
            case SDLK_RIGHT: in.right = true; break;
            case SDLK_z: in.a = true; break;
            case SDLK_x: in.b = true; break;
            case SDLK_q: in.l1 = true; break;
            case SDLK_w: in.r1 = true; break;
            case SDLK_c: in.x = true; break;
            case SDLK_m: in.m = true; break;
            default: break;
        }
    };

    for (const auto& e : events) {
        if (e.type == SDL_KEYDOWN) {
            set_key(e.key.keysym.sym);
        }
        if (e.type == SDL_JOYHATMOTION) {
            Uint8 v = e.jhat.value;
            if (v & SDL_HAT_UP) in.up = true;
            if (v & SDL_HAT_DOWN) in.down = true;
            if (v & SDL_HAT_LEFT) in.left = true;
            if (v & SDL_HAT_RIGHT) in.right = true;
        }
        if (e.type == SDL_JOYBUTTONDOWN) {
            switch (e.jbutton.button) {
                case 0: in.a = true; break;
                case 1: in.b = true; break;
                case 3: in.x = true; break;
                case 4: in.l1 = true; break;
                case 5: in.r1 = true; break;
                case 8: in.m = true; break;
                default: break;
            }
        }
    }

    return in;
}

} // namespace tradeboy::app
