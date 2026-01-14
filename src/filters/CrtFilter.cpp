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
    , u_tint_(-1) {}

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
        "float hash12(vec2 p){\n"
        "  vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
        "  p3 += dot(p3, p3.yzx + 33.33);\n"
        "  return fract((p3.x + p3.y) * p3.z);\n"
        "}\n"
        "void main(){\n"
        "  vec2 uv = vUV;\n"
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
        "  vec4 colC = texture2D(uTex, uv);\n"
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

    width_ = 0;
    height_ = 0;
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
