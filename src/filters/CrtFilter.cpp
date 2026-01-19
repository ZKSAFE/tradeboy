#include "filters/CrtFilter.h"

#include <cstdio>

extern void log_to_file(const char* fmt, ...);

namespace {

static GLuint compile_shader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLchar logbuf[1024];
        GLsizei n = 0;
        glGetShaderInfoLog(sh, (GLsizei)sizeof(logbuf), &n, logbuf);
        log_to_file("[CRT] shader compile failed: %s\n", logbuf);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPos");
    glBindAttribLocation(prog, 1, "aUV");
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLchar logbuf[1024];
        GLsizei n = 0;
        glGetProgramInfoLog(prog, (GLsizei)sizeof(logbuf), &n, logbuf);
        log_to_file("[CRT] program link failed: %s\n", logbuf);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

} // namespace

namespace tradeboy {
namespace filters {

CrtFilter::CrtFilter()
    : scan_strength(0.20f)
    , vignette_strength(0.93f)
    , rgb_shift(1.60f)
    , bulge(0.03f)
    , zoom(1.02f)
    , phosphor_k_rg(0.40f)
    , phosphor_g_gain(1.14f)
    , phosphor_b_cut(0.22f)
    , overlay_blur_strength(1.0f)
    , overlay_darken(0.55f)
    , width_(0)
    , height_(0)
    , prog_(0)
    , vs_(0)
    , fs_(0)
    , fbo_(0)
    , tex_(0)
    , depth_(0)
    , vbo_(0)
    , u_tex_(-1)
    , u_resolution_(-1)
    , u_time_(-1)
    , u_scan_strength_(-1)
    , u_vignette_strength_(-1)
    , u_rgb_shift_(-1)
    , u_bulge_(-1)
    , u_zoom_(-1)
    , u_tint_(-1)
    , u_overlay_rect_(-1)
    , u_overlay_active_(-1)
    , u_overlay_blur_strength_(-1)
    , u_overlay_darken_(-1)
    , u_poweroff_t_(-1)
    , u_poweroff_active_(-1)
    , u_boot_t_(-1)
    , u_boot_active_(-1)
    , overlay_rect_uv_(0, 0, 0, 0)
    , overlay_active_(false)
    , poweroff_t_(0.0f)
    , poweroff_active_(false)
    , boot_t_(0.0f)
    , boot_active_(false) {}


bool CrtFilter::init(int width, int height) {
    shutdown();

    width_ = width;
    height_ = height;

    const char* crt_vs_src =
        "attribute vec2 aPos;\n"
        "attribute vec2 aUV;\n"
        "varying vec2 vUV;\n"
        "void main(){\n"
        "  vUV = aUV;\n"
        "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n";

    const char* crt_fs_src =
        "precision mediump float;\n"
        "varying vec2 vUV;\n"
        "uniform sampler2D uTex;\n"
        "uniform vec2 uResolution;\n"
        "uniform float uTime;\n"
        "uniform float uScanStrength;\n"
        "uniform float uVignetteStrength;\n"
        "uniform float uRgbShift;\n"
        "uniform float uBulge;\n"
        "uniform float uZoom;\n"
        "uniform vec3 uTint;\n"
        "uniform vec4 uOverlayRect;\n"
        "uniform float uOverlayActive;\n"
        "uniform float uOverlayBlurStrength;\n"
        "uniform float uOverlayDarken;\n"
        "uniform float uPoweroffT;\n"
        "uniform float uPoweroffActive;\n"
        "uniform float uBootT;\n"
        "uniform float uBootActive;\n"
        "float hash12(vec2 p){\n"
        "  vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
        "  p3 += dot(p3, p3.yzx + 33.33);\n"
        "  return fract((p3.x + p3.y) * p3.z);\n"
        "}\n"
        "float easeInOut(float t){\n"
        "  t = clamp(t, 0.0, 1.0);\n"
        "  return t * t * (3.0 - 2.0 * t);\n"
        "}\n"
        "float sat(float x){ return clamp(x, 0.0, 1.0); }\n"
        "vec2 quantUV(vec2 uv, float cells){\n"
        "  vec2 g = vec2(cells, cells * (uResolution.y / uResolution.x));\n"
        "  return (floor(uv * g) + 0.5) / g;\n"
        "}\n"
        "void main(){\n"
        "  vec2 baseUV = vUV;\n"
        "  vec2 uv = baseUV;\n"
        "  bool poweroffOn = (uPoweroffActive > 0.5);\n"
        "  bool bootOn = (!poweroffOn && uBootActive > 0.5);\n"
        "  if (poweroffOn) {\n"
        "    float t = clamp(uPoweroffT, 0.0, 1.0);\n"
        "    float tLine = easeInOut(min(1.0, t / 0.65));\n"
        "    float tDot  = easeInOut((t - 0.65) / 0.35);\n"
        "    float sy = mix(1.0, 0.006, tLine);\n"
        "    float sx = mix(1.0, 0.006, tDot);\n"
        "    uv = (uv - 0.5) * vec2(sx, sy) + 0.5;\n"
        "  }\n"
        "  uv = (uv - 0.5) / uZoom + 0.5;\n"
        "  vec2 c = uv * 2.0 - 1.0;\n"
        "  float r2 = dot(c,c);\n"
        "  uv = (c * (1.0 + uBulge * r2)) * 0.5 + 0.5;\n"
        "  if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {\n"
        "    gl_FragColor = vec4(0.0,0.0,0.0,1.0);\n"
        "    return;\n"
        "  }\n"
        "  vec2 px = vec2(1.0 / uResolution.x, 1.0 / uResolution.y);\n"
        "  vec2 shift = vec2(uRgbShift * px.x, 0.0);\n"
        "  bool outsideOverlay = false;\n"
        "  if (uOverlayActive > 0.5) {\n"
        "    outsideOverlay = (baseUV.x < uOverlayRect.x || baseUV.x > uOverlayRect.z || baseUV.y < uOverlayRect.y || baseUV.y > uOverlayRect.w);\n"
        "  }\n"
        "  vec4 colC = texture2D(uTex, uv);\n"
        "  if (outsideOverlay) {\n"
        "    float s = uOverlayBlurStrength;\n"
        "    vec2 o = px * (1.5 * s);\n"
        "    vec3 c0 = texture2D(uTex, uv).rgb;\n"
        "    vec3 c1 = texture2D(uTex, uv + vec2(o.x, 0.0)).rgb;\n"
        "    vec3 c2 = texture2D(uTex, uv - vec2(o.x, 0.0)).rgb;\n"
        "    vec3 c3 = texture2D(uTex, uv + vec2(0.0, o.y)).rgb;\n"
        "    vec3 c4 = texture2D(uTex, uv - vec2(0.0, o.y)).rgb;\n"
        "    vec3 cb = (c0 * 0.40 + (c1 + c2 + c3 + c4) * 0.15);\n"
        "    colC = vec4(cb, 1.0);\n"
        "  }\n"
        "  float r = texture2D(uTex, uv + shift).r;\n"
        "  float g = colC.g;\n"
        "  float b = texture2D(uTex, uv - shift).b;\n"
        "  vec3 col = vec3(r,g,b);\n"
        "  float scan = 0.5 + 0.5 * sin((uv.y * uResolution.y) * 3.14159);\n"
        "  col *= 1.0 - uScanStrength * scan;\n"
        "  vec2 dv = uv - 0.5;\n"
        "  float vig = 1.0 - uVignetteStrength * smoothstep(0.15, 0.70, dot(dv,dv));\n"
        "  col *= vig;\n"
        "  float n = (hash12(uv * uResolution + uTime * 60.0) - 0.5) * 0.02;\n"
        "  col += n;\n"
        "  float luma = dot(col, vec3(0.299, 0.587, 0.114));\n"
        "  float tintW = 1.0 - smoothstep(0.55, 0.78, luma);\n"
        "  tintW = tintW * tintW;\n"
        "  float maxRB = max(col.r, col.b);\n"
        "  float greenDom = smoothstep(0.06, 0.22, col.g - maxRB);\n"
        "  float w = tintW * greenDom;\n"
        "  float kRG = uTint.x;\n"
        "  float gGain = uTint.y;\n"
        "  float bCut = uTint.z;\n"
        "  float g2 = col.g * mix(1.0, gGain, w);\n"
        "  col.r = mix(col.r, col.r + g2 * kRG, w);\n"
        "  col.g = g2;\n"
        "  col.b = mix(col.b, col.b * (1.0 - bCut), w);\n"
        "  col = clamp(col, 0.0, 1.0);\n"
        "  if (outsideOverlay) {\n"
        "    col *= (1.0 - uOverlayDarken);\n"
        "  }\n"
        "  if (poweroffOn) {\n"
        "    float t = clamp(uPoweroffT, 0.0, 1.0);\n"
        "    float tLine = easeInOut(min(1.0, t / 0.65));\n"
        "    float tDot  = easeInOut((t - 0.65) / 0.35);\n"
        "    float sy = mix(1.0, 0.006, tLine);\n"
        "    float sx = mix(1.0, 0.006, tDot);\n"
        "    float ay = abs(baseUV.y - 0.5);\n"
        "    float ax = abs(baseUV.x - 0.5);\n"
        "    float fy = 0.020 + 0.030 * (1.0 - tLine);\n"
        "    float fx = 0.020 + 0.030 * (1.0 - tDot);\n"
        "    float my = 1.0 - smoothstep(sy * 0.5, sy * 0.5 + fy, ay);\n"
        "    float mx = 1.0 - smoothstep(sx * 0.5, sx * 0.5 + fx, ax);\n"
        "    float m = my * mix(1.0, mx, tDot);\n"
        "    float glow = (1.0 - t) * 0.45;\n"
        "    col *= m;\n"
        "    col += glow * m;\n"
        "  }\n"

        "  if (bootOn) {\n"
        "    float t = clamp(uBootT, 0.0, 1.0);\n"
        "    float wSnow = 1.0 - smoothstep(0.26, 0.38, t);\n"
        "    float wMosaic = smoothstep(0.22, 0.40, t) * (1.0 - smoothstep(0.86, 0.98, t));\n"
        "    float wNormal = smoothstep(0.86, 1.00, t);\n"

        "    float n0 = hash12(baseUV * uResolution + uTime * 120.0);\n"
        "    float n1 = hash12(baseUV * uResolution * 0.7 + uTime * 240.0);\n"
        "    float snow = sat(n0 * 0.65 + n1 * 0.35);\n"
        "    snow = pow(snow, 1.6);\n"

        "    float jitterW = 1.0 - smoothstep(0.40, 0.85, t);\n"
        "    float jitter = (hash12(vec2(uTime * 60.0, baseUV.y * 931.0)) - 0.5) * jitterW;\n"
        "    vec2 uvJ = clamp(baseUV + vec2(jitter * 0.025, 0.0), 0.0, 1.0);\n"

        "    float tm = smoothstep(0.40, 0.90, t);\n"
        "    float cells = mix(22.0, 110.0, tm);\n"
        "    vec2 uvM = quantUV(uvJ, cells);\n"
        "    vec3 cM = texture2D(uTex, uvM).rgb;\n"
        "    float grain = mix(0.36, 0.08, tm);\n"
        "    vec3 g = vec3(hash12(baseUV * uResolution * mix(0.9, 2.6, tm) + uTime * 190.0) - 0.5);\n"
        "    cM = clamp(cM + g * grain, 0.0, 1.0);\n"

        "    vec3 outCol = vec3(0.0);\n"
        "    outCol += vec3(snow) * wSnow;\n"
        "    outCol += cM * wMosaic;\n"
        "    outCol += col * wNormal;\n"

        "    float flick = 0.90 + 0.10 * sin(uTime * 40.0);\n"
        "    outCol *= flick;\n"
        "    col = outCol;\n"
        "  }\n"
        "  gl_FragColor = vec4(col, 1.0);\n"
        "}\n";

    vs_ = compile_shader(GL_VERTEX_SHADER, crt_vs_src);
    fs_ = compile_shader(GL_FRAGMENT_SHADER, crt_fs_src);
    if (!vs_ || !fs_) {
        shutdown();
        return false;
    }

    prog_ = link_program(vs_, fs_);
    if (!prog_) {
        shutdown();
        return false;
    }

    u_tex_ = glGetUniformLocation(prog_, "uTex");
    u_resolution_ = glGetUniformLocation(prog_, "uResolution");
    u_time_ = glGetUniformLocation(prog_, "uTime");
    u_scan_strength_ = glGetUniformLocation(prog_, "uScanStrength");
    u_vignette_strength_ = glGetUniformLocation(prog_, "uVignetteStrength");
    u_rgb_shift_ = glGetUniformLocation(prog_, "uRgbShift");
    u_bulge_ = glGetUniformLocation(prog_, "uBulge");
    u_zoom_ = glGetUniformLocation(prog_, "uZoom");
    u_tint_ = glGetUniformLocation(prog_, "uTint");
    u_overlay_rect_ = glGetUniformLocation(prog_, "uOverlayRect");
    u_overlay_active_ = glGetUniformLocation(prog_, "uOverlayActive");
    u_overlay_blur_strength_ = glGetUniformLocation(prog_, "uOverlayBlurStrength");
    u_overlay_darken_ = glGetUniformLocation(prog_, "uOverlayDarken");
    u_poweroff_t_ = glGetUniformLocation(prog_, "uPoweroffT");
    u_poweroff_active_ = glGetUniformLocation(prog_, "uPoweroffActive");

    u_boot_t_ = glGetUniformLocation(prog_, "uBootT");
    u_boot_active_ = glGetUniformLocation(prog_, "uBootActive");

    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenRenderbuffers(1, &depth_);
    glBindRenderbuffer(GL_RENDERBUFFER, depth_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width_, height_);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_);
    GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
        log_to_file("[CRT] FBO incomplete: 0x%x\n", (unsigned int)fb_status);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        shutdown();
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    const float quad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };

    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(quad), quad, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}

void CrtFilter::shutdown() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (depth_) glDeleteRenderbuffers(1, &depth_);
    if (tex_) glDeleteTextures(1, &tex_);
    if (prog_) glDeleteProgram(prog_);
    if (vs_) glDeleteShader(vs_);
    if (fs_) glDeleteShader(fs_);

    vbo_ = 0;
    fbo_ = 0;
    depth_ = 0;
    tex_ = 0;
    prog_ = 0;
    vs_ = 0;
    fs_ = 0;

    u_tex_ = -1;
    u_resolution_ = -1;
    u_time_ = -1;
    u_scan_strength_ = -1;
    u_vignette_strength_ = -1;
    u_rgb_shift_ = -1;
    u_bulge_ = -1;
    u_zoom_ = -1;
    u_tint_ = -1;

    u_overlay_rect_ = -1;
    u_overlay_active_ = -1;
    u_overlay_blur_strength_ = -1;
    u_overlay_darken_ = -1;

    u_poweroff_t_ = -1;
    u_poweroff_active_ = -1;

    u_boot_t_ = -1;
    u_boot_active_ = -1;

    width_ = 0;
    height_ = 0;
}

void CrtFilter::set_overlay_rect_uv(const ImVec4& rect_uv, bool active) {
    overlay_rect_uv_ = rect_uv;
    overlay_active_ = active;
}

void CrtFilter::set_poweroff(float t, bool active) {
    poweroff_t_ = t;
    poweroff_active_ = active;
}

void CrtFilter::set_boot(float t, bool active) {
    boot_t_ = t;
    boot_active_ = active;
}

bool CrtFilter::is_ready() const {
    return prog_ && fbo_ && tex_ && vbo_ && width_ > 0 && height_ > 0;
}

void CrtFilter::begin() {
    if (!is_ready()) return;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
    glClearColor(0.10f, 0.11f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void CrtFilter::end(float time_seconds) {
    if (!is_ready()) return;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    glUseProgram(prog_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);

    if (u_tex_ >= 0) glUniform1i(u_tex_, 0);
    if (u_resolution_ >= 0) glUniform2f(u_resolution_, (float)width_, (float)height_);
    if (u_time_ >= 0) glUniform1f(u_time_, time_seconds);
    if (u_scan_strength_ >= 0) glUniform1f(u_scan_strength_, scan_strength);
    if (u_vignette_strength_ >= 0) glUniform1f(u_vignette_strength_, vignette_strength);
    if (u_rgb_shift_ >= 0) glUniform1f(u_rgb_shift_, rgb_shift);
    if (u_bulge_ >= 0) glUniform1f(u_bulge_, bulge);
    if (u_zoom_ >= 0) glUniform1f(u_zoom_, zoom);
    if (u_tint_ >= 0) glUniform3f(u_tint_, phosphor_k_rg, phosphor_g_gain, phosphor_b_cut);

    if (u_overlay_rect_ >= 0)
        glUniform4f(u_overlay_rect_, overlay_rect_uv_.x, overlay_rect_uv_.y, overlay_rect_uv_.z, overlay_rect_uv_.w);
    if (u_overlay_active_ >= 0) glUniform1f(u_overlay_active_, overlay_active_ ? 1.0f : 0.0f);
    if (u_overlay_blur_strength_ >= 0) glUniform1f(u_overlay_blur_strength_, overlay_blur_strength);
    if (u_overlay_darken_ >= 0) glUniform1f(u_overlay_darken_, overlay_darken);

    if (u_poweroff_t_ >= 0) glUniform1f(u_poweroff_t_, poweroff_t_);
    if (u_poweroff_active_ >= 0) glUniform1f(u_poweroff_active_, poweroff_active_ ? 1.0f : 0.0f);

    if (u_boot_t_ >= 0) glUniform1f(u_boot_t_, boot_t_);
    if (u_boot_active_ >= 0) glUniform1f(u_boot_active_, boot_active_ ? 1.0f : 0.0f);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), (const void*)0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * (GLsizei)sizeof(float), (const void*)(2 * sizeof(float)));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

} // namespace filters
} // namespace tradeboy
