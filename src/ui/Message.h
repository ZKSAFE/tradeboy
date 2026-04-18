#pragma once

#include <string>

#include "imgui.h"

#include "utils/Typewriter.h"

namespace tradeboy::ui {

struct MessageState {
    bool open = false;
    bool closing = false;
    int frames = 0;
    int total_frames = 0;
    int open_frames = 0;
    int close_frames = 0;
    std::string body;
    bool queued_open = false;
    int queued_frames = 0;
    std::string queued_body;
    tradeboy::utils::TypewriterState tw;

    void reset() {
        open = false;
        closing = false;
        frames = 0;
        total_frames = 0;
        open_frames = 0;
        close_frames = 0;
        body.clear();
        tw.last_text.clear();
        tw.start_time = 0.0;
    }

    void open_message(const std::string& in_body, int duration_frames) {
        open = true;
        closing = false;
        frames = duration_frames > 0 ? duration_frames : 1;
        total_frames = frames;
        open_frames = 0;
        close_frames = 0;
        body = in_body;
        tw.last_text.clear();
        tw.start_time = 0.0;
    }

    void show(const std::string& in_body, int duration_frames) {
        if (open) {
            queued_open = true;
            queued_frames = duration_frames > 0 ? duration_frames : 1;
            queued_body = in_body;
            if (!closing) {
                start_close();
            }
            return;
        }
        open_message(in_body, duration_frames);
    }

    void start_close() {
        if (!open || closing) return;
        closing = true;
        close_frames = 0;
    }

    float get_open_t() const {
        if (!open) return 0.0f;
        if (!closing) {
            return (open_frames < 18) ? (float)open_frames / 18.0f : 1.0f;
        }
        const int close_dur = 18;
        return 1.0f - (float)close_frames / (float)close_dur;
    }

    bool tick() {
        if (!open) return false;

        if (!closing) {
            if (open_frames < 18) {
                open_frames++;
            }
            if (frames > 0) {
                frames--;
            }
            if (frames <= 0) {
                start_close();
            }
            return false;
        }

        close_frames++;
        if (close_frames >= 18) {
            const bool has_queued = queued_open;
            const int next_frames = queued_frames;
            const std::string next_body = queued_body;
            reset();
            if (has_queued) {
                queued_open = false;
                queued_frames = 0;
                queued_body.clear();
                open_message(next_body, next_frames);
            }
            return true;
        }
        return false;
    }
};

void render_message(const char* id,
                    const MessageState& state,
                    ImFont* font_bold);

} // namespace tradeboy::ui
