#pragma once

#include "imgui.h"
#include <SDL_opengles2.h>

namespace tradeboy {
namespace filters {

class CrtFilter {
public:
    CrtFilter();

    bool init(int width, int height);
    void shutdown();

    bool is_ready() const;

    void begin();
    void end(float time_seconds);

    void set_overlay_rect_uv(const ImVec4& rect_uv, bool active);
    void set_poweroff(float t, bool active);
    void set_boot(float t, bool active);

    float scan_strength;
    float vignette_strength;
    float rgb_shift;
    float bulge;
    float zoom;

    float phosphor_k_rg;
    float phosphor_g_gain;
    float phosphor_b_cut;

    float overlay_blur_strength;
    float overlay_darken;

private:
    int width_;
    int height_;

    GLuint prog_;
    GLuint vs_;
    GLuint fs_;

    GLuint fbo_;
    GLuint tex_;
    GLuint depth_;
    GLuint vbo_;

    GLint u_tex_;
    GLint u_resolution_;
    GLint u_time_;
    GLint u_scan_strength_;
    GLint u_vignette_strength_;
    GLint u_rgb_shift_;
    GLint u_bulge_;
    GLint u_zoom_;
    GLint u_tint_;

    GLint u_overlay_rect_;
    GLint u_overlay_active_;
    GLint u_overlay_blur_strength_;
    GLint u_overlay_darken_;

    GLint u_poweroff_t_;
    GLint u_poweroff_active_;

    GLint u_boot_t_;
    GLint u_boot_active_;

    ImVec4 overlay_rect_uv_;
    bool overlay_active_;

    float poweroff_t_;
    bool poweroff_active_;

    float boot_t_;
    bool boot_active_;
};

} // namespace filters
} // namespace tradeboy
