#pragma once

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

    float scan_strength;
    float vignette_strength;
    float rgb_shift;
    float bulge;
    float zoom;

    float phosphor_k_rg;
    float phosphor_g_gain;
    float phosphor_b_cut;

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
};

} // namespace filters
} // namespace tradeboy
