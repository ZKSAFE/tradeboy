#include "Input.h"

namespace tradeboy::app {

InputState poll_input_state_from_events(const std::vector<SDL_Event>& events) {
    static InputState st;
    static Uint8 hat = 0;

    auto apply_key = [&](SDL_Keycode key, bool down) {
        switch (key) {
            case SDLK_UP: st.up = down; break;
            case SDLK_DOWN: st.down = down; break;
            case SDLK_LEFT: st.left = down; break;
            case SDLK_RIGHT: st.right = down; break;
            case SDLK_z: st.a = down; break;
            case SDLK_x: st.b = down; break;
            case SDLK_q: st.l1 = down; break;
            case SDLK_w: st.r1 = down; break;
            case SDLK_a: st.l2 = down; break;
            case SDLK_s: st.r2 = down; break;
            case SDLK_c: st.x = down; break;
            case SDLK_m: st.m = down; break;
            default: break;
        }
    };

    for (const auto& e : events) {
        if (e.type == SDL_KEYDOWN) {
            apply_key(e.key.keysym.sym, true);
        }
        if (e.type == SDL_KEYUP) {
            apply_key(e.key.keysym.sym, false);
        }
        if (e.type == SDL_JOYHATMOTION) {
            hat = e.jhat.value;
        }
        if (e.type == SDL_JOYBUTTONDOWN || e.type == SDL_JOYBUTTONUP) {
            const bool down = (e.type == SDL_JOYBUTTONDOWN);
            switch (e.jbutton.button) {
                case 0: st.a = down; break;
                case 1: st.b = down; break;
                case 3: st.x = down; break;
                case 4: st.l1 = down; break;
                case 5: st.r1 = down; break;
                case 9: st.l2 = down; break;
                case 10: st.r2 = down; break;
                case 8: st.m = down; break;
                default: break;
            }
        }
    }

    InputState out = st;
    out.up = out.up || (hat & SDL_HAT_UP);
    out.down = out.down || (hat & SDL_HAT_DOWN);
    out.left = out.left || (hat & SDL_HAT_LEFT);
    out.right = out.right || (hat & SDL_HAT_RIGHT);
    return out;
}

} // namespace tradeboy::app
