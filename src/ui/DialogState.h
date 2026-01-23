#pragma once

#include <string>

namespace tradeboy::ui {

struct DialogState {
    bool open = false;
    int selected_btn = 1;
    int open_frames = 0;
    int flash_frames = 0;
    int pending_action = -1;
    bool closing = false;
    int close_frames = 0;

    std::string body;

    void reset() {
        open = false;
        selected_btn = 1;
        open_frames = 0;
        flash_frames = 0;
        pending_action = -1;
        closing = false;
        close_frames = 0;
        body.clear();
    }

    void open_dialog(const std::string& dialog_body = "", int default_btn = 1) {
        open = true;
        selected_btn = default_btn;
        open_frames = 0;
        flash_frames = 0;
        pending_action = -1;
        closing = false;
        close_frames = 0;
        body = dialog_body;
    }

    void start_close() {
        closing = true;
        close_frames = 0;
        flash_frames = 0;
        pending_action = -1;
    }

    void start_flash(int action) {
        pending_action = action;
        flash_frames = 18;
    }

    float get_open_t() const {
        if (!closing) {
            return (open_frames < 18) ? (float)open_frames / 18.0f : 1.0f;
        } else {
            const int close_dur = 18;
            return 1.0f - (float)close_frames / (float)close_dur;
        }
    }

    bool tick_open_anim() {
        if (!closing && open_frames < 18) {
            open_frames++;
            return true;
        }
        return false;
    }

    bool tick_close_anim(int close_dur = 18) {
        if (closing) {
            close_frames++;
            if (close_frames >= close_dur) {
                return true;
            }
        }
        return false;
    }

    bool tick_flash() {
        if (flash_frames > 0) {
            flash_frames--;
            if (flash_frames == 0 && pending_action >= 0) {
                return true;
            }
        }
        return false;
    }
};

} // namespace tradeboy::ui
