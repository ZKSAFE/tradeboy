#include <SDL.h>
#include <SDL_opengles2.h>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include <fstream>
#include <string>
#include <vector>
#include <cstdarg>
#include <cmath>
#include <unistd.h>
#include <sys/stat.h>

#include "app/App.h"
#include "app/Input.h"

// Simple file logger
void log_to_file(const char* fmt, ...) {
    FILE* f = fopen("log.txt", "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fclose(f);
}

static bool file_exists(const char* path) {
    if (!path) return false;
    std::ifstream f(path);
    return f.good();
}

static bool dir_exists(const char* path) {
    if (!path) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return S_ISDIR(st.st_mode);
}

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

int main(int argc, char** argv) {
    // On device, the app may be launched with CWD=/, but our assets live under
    // /mnt/mmc/Roms/APPS. If that directory exists, switch CWD so relative
    // asset paths work (fonts, log file).
    if (dir_exists("/mnt/mmc/Roms/APPS")) {
        chdir("/mnt/mmc/Roms/APPS");
    }

    // Clear log file on startup
    FILE* f = fopen("log.txt", "w");
    if (f) {
        fprintf(f, "--- TradeBoy Log Start V6 ---\n");
        fclose(f);
    }

    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER) != 0) {
        log_to_file("SDL_Init failed: %s\n", SDL_GetError());
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_JoystickEventState(SDL_ENABLE);

    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) != 0) {
        mode.w = 720;
        mode.h = 480;
        mode.refresh_rate = 60;
    }

    // RG34XX: SDL may report a bogus "desktop" size (e.g. 1280x1024) which can
    // make mali-fbdev fail with "Can't create EGL window surface". Clamp to the
    // known framebuffer size.
    if (mode.w != 720 || mode.h != 480) {
        SDL_DisplayMode desktop;
        if (SDL_GetDesktopDisplayMode(0, &desktop) == 0) {
            mode = desktop;
        }
        if (mode.w != 720 || mode.h != 480) {
            mode.w = 720;
            mode.h = 480;
            mode.refresh_rate = 60;
        }
    }
    log_to_file("Display Mode: %dx%d @ %dHz\n", mode.w, mode.h, mode.refresh_rate);

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    SDL_Window* window = SDL_CreateWindow(
        "tradeboy",
        SDL_WINDOWPOS_UNDEFINED_DISPLAY(0),
        SDL_WINDOWPOS_UNDEFINED_DISPLAY(0),
        mode.w,
        mode.h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);

    if (!window) {
        log_to_file("SDL_CreateWindow failed: %s\n", SDL_GetError());
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glctx = SDL_GL_CreateContext(window);
    if (!glctx) {
        log_to_file("SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_MakeCurrent(window, glctx);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    const char* glsl_version = "#version 100";
    ImGui_ImplSDL2_InitForOpenGL(window, glctx);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // CRT post-process (FBO + fullscreen quad)
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
        "  gl_FragColor = vec4(col, 1.0);\n"
        "}\n";

    GLuint crt_vs = compile_shader(GL_VERTEX_SHADER, crt_vs_src);
    GLuint crt_fs = compile_shader(GL_FRAGMENT_SHADER, crt_fs_src);
    GLuint crt_prog = 0;
    if (crt_vs && crt_fs) {
        crt_prog = link_program(crt_vs, crt_fs);
    }

    GLint crt_uTex = -1;
    GLint crt_uResolution = -1;
    GLint crt_uTime = -1;
    GLint crt_uScanStrength = -1;
    GLint crt_uVignetteStrength = -1;
    GLint crt_uRgbShift = -1;
    GLint crt_uBulge = -1;
    GLint crt_uZoom = -1;

    if (crt_prog) {
        crt_uTex = glGetUniformLocation(crt_prog, "uTex");
        crt_uResolution = glGetUniformLocation(crt_prog, "uResolution");
        crt_uTime = glGetUniformLocation(crt_prog, "uTime");
        crt_uScanStrength = glGetUniformLocation(crt_prog, "uScanStrength");
        crt_uVignetteStrength = glGetUniformLocation(crt_prog, "uVignetteStrength");
        crt_uRgbShift = glGetUniformLocation(crt_prog, "uRgbShift");
        crt_uBulge = glGetUniformLocation(crt_prog, "uBulge");
        crt_uZoom = glGetUniformLocation(crt_prog, "uZoom");
    }

    GLuint crt_fbo = 0;
    GLuint crt_tex = 0;
    GLuint crt_depth = 0;
    if (crt_prog) {
        glGenTextures(1, &crt_tex);
        glBindTexture(GL_TEXTURE_2D, crt_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mode.w, mode.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenRenderbuffers(1, &crt_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, crt_depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, mode.w, mode.h);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glGenFramebuffers(1, &crt_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, crt_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, crt_tex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, crt_depth);
        GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (fb_status != GL_FRAMEBUFFER_COMPLETE) {
            log_to_file("[CRT] FBO incomplete: 0x%x\n", (unsigned int)fb_status);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glDeleteFramebuffers(1, &crt_fbo);
            glDeleteRenderbuffers(1, &crt_depth);
            glDeleteTextures(1, &crt_tex);
            crt_fbo = 0;
            crt_depth = 0;
            crt_tex = 0;
            glDeleteProgram(crt_prog);
            crt_prog = 0;
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

    GLuint crt_vbo = 0;
    if (crt_prog && crt_fbo) {
        const float quad[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
        };
        glGenBuffers(1, &crt_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, crt_vbo);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)sizeof(quad), quad, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    // Font: prefer bundled cour-new.ttf for readable UI on 720x480.
    const char* font_path = nullptr;
    if (file_exists("cour-new.ttf")) font_path = "cour-new.ttf";
    else if (file_exists("output/cour-new.ttf")) font_path = "output/cour-new.ttf";
    else if (file_exists("CourierPrime-Regular.ttf")) font_path = "CourierPrime-Regular.ttf";
    else if (file_exists("output/NotoSansCJK-Regular.ttc")) font_path = "output/NotoSansCJK-Regular.ttc";
    else if (file_exists("NotoSansCJK-Regular.ttc")) font_path = "NotoSansCJK-Regular.ttc";

    if (font_path) {
        log_to_file("[Main] Loading font: %s\n", font_path);
        ImFontConfig cfg;
        cfg.OversampleH = 1;
        cfg.OversampleV = 1;
        cfg.PixelSnapH = true;
        ImFont* f = io.Fonts->AddFontFromFileTTF(font_path, 28.0f, &cfg);
        if (f) {
            io.FontDefault = f;
            unsigned char* pixels = nullptr;
            int w = 0, h = 0;
            io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
            log_to_file("[Main] Font atlas built: %dx%d\n", w, h);
        } else {
            log_to_file("[Main] Font load failed, using default\n");
        }
    } else {
        log_to_file("[Main] No font file found, using default\n");
    }

    // Load Bold Font
    const char* font_path_bold = nullptr;
    if (file_exists("cour-new-BOLDITALIC.ttf")) font_path_bold = "cour-new-BOLDITALIC.ttf";
    else if (file_exists("output/cour-new-BOLDITALIC.ttf")) font_path_bold = "output/cour-new-BOLDITALIC.ttf";
    
    ImFont* loaded_font_bold = nullptr;
    if (font_path_bold) {
         log_to_file("[Main] Loading bold font: %s\n", font_path_bold);
         ImFontConfig cfg;
         cfg.OversampleH = 1;
         cfg.OversampleV = 1;
         cfg.PixelSnapH = true;
         // Merge? No, separate font.
         loaded_font_bold = io.Fonts->AddFontFromFileTTF(font_path_bold, 28.0f, &cfg);
    }

    SDL_Joystick* joy0 = nullptr;
    if (SDL_NumJoysticks() > 0) {
        joy0 = SDL_JoystickOpen(0);
    }

    log_to_file("[Main] Allocating App on heap...\n");
    auto* app_ptr = new tradeboy::app::App();
    tradeboy::app::App& app = *app_ptr;

    app.font_bold = loaded_font_bold;
    log_to_file("[Main] App constructed\n");
    app.init_demo_data();
    log_to_file("[Main] init_demo_data done\n");
    log_to_file("[Main] calling load_private_key\n");
    app.load_private_key();
    log_to_file("[Main] load_private_key done\n");
    log_to_file("[Main] calling startup\n");
    app.startup();
    log_to_file("[Main] app.startup done\n");

    tradeboy::app::EdgeState edges;

    bool running = true;
    log_to_file("[Main] entering main loop\n");
    int frame_counter = 0;
    while (running) {
        std::vector<SDL_Event> events;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            events.push_back(e);
            if (e.type == SDL_QUIT) running = false;
        }

        tradeboy::app::InputState in = tradeboy::app::poll_input_state_from_events(events);
        app.handle_input_edges(in, edges);
        edges.prev = in;

        if (app.quit_requested) {
            running = false;
        }

        if (frame_counter == 0) {
            log_to_file("[Main] first frame\n");
        }
        frame_counter++;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)mode.w, (float)mode.h), ImGuiCond_Always);

        ImGuiWindowFlags wflags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBackground;
        ImGui::Begin("Spot", nullptr, wflags);
        app.render();
        ImGui::End();

        ImGui::Render();

        if (crt_prog && crt_fbo && crt_tex && crt_vbo) {
            glBindFramebuffer(GL_FRAMEBUFFER, crt_fbo);
            glViewport(0, 0, mode.w, mode.h);
            glClearColor(0.10f, 0.11f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, mode.w, mode.h);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);

            glUseProgram(crt_prog);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, crt_tex);
            if (crt_uTex >= 0) glUniform1i(crt_uTex, 0);
            if (crt_uResolution >= 0) glUniform2f(crt_uResolution, (float)mode.w, (float)mode.h);
            if (crt_uTime >= 0) glUniform1f(crt_uTime, (float)ImGui::GetTime());
            if (crt_uScanStrength >= 0) glUniform1f(crt_uScanStrength, 0.20f);
            if (crt_uVignetteStrength >= 0) glUniform1f(crt_uVignetteStrength, 0.75f);
            if (crt_uRgbShift >= 0) glUniform1f(crt_uRgbShift, 1.60f);
            if (crt_uBulge >= 0) glUniform1f(crt_uBulge, 0.03f);
            if (crt_uZoom >= 0) glUniform1f(crt_uZoom, 1.02f);

            glBindBuffer(GL_ARRAY_BUFFER, crt_vbo);
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
        } else {
            glViewport(0, 0, mode.w, mode.h);
            glClearColor(0.10f, 0.11f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        SDL_GL_SwapWindow(window);
    }

    log_to_file("[Main] main loop exit -> begin shutdown\n");

    log_to_file("[Main] calling app.shutdown()\n");
    app.shutdown();
    log_to_file("[Main] app.shutdown() done\n");

    log_to_file("[Main] deleting app_ptr\n");
    delete app_ptr;
    log_to_file("[Main] delete app_ptr done\n");

    if (joy0) {
        log_to_file("[Main] SDL_JoystickClose\n");
        SDL_JoystickClose(joy0);
        log_to_file("[Main] SDL_JoystickClose done\n");
    }

    log_to_file("[Main] ImGui_ImplOpenGL3_Shutdown\n");
    ImGui_ImplOpenGL3_Shutdown();
    log_to_file("[Main] ImGui_ImplOpenGL3_Shutdown done\n");

    log_to_file("[Main] ImGui_ImplSDL2_Shutdown\n");
    ImGui_ImplSDL2_Shutdown();
    log_to_file("[Main] ImGui_ImplSDL2_Shutdown done\n");

    log_to_file("[Main] ImGui::DestroyContext\n");
    ImGui::DestroyContext();
    log_to_file("[Main] ImGui::DestroyContext done\n");

    log_to_file("[Main] SDL_GL_DeleteContext\n");
    SDL_GL_DeleteContext(glctx);
    log_to_file("[Main] SDL_GL_DeleteContext done\n");

    if (crt_vbo) glDeleteBuffers(1, &crt_vbo);
    if (crt_fbo) glDeleteFramebuffers(1, &crt_fbo);
    if (crt_depth) glDeleteRenderbuffers(1, &crt_depth);
    if (crt_tex) glDeleteTextures(1, &crt_tex);
    if (crt_prog) glDeleteProgram(crt_prog);
    if (crt_vs) glDeleteShader(crt_vs);
    if (crt_fs) glDeleteShader(crt_fs);

    log_to_file("[Main] SDL_DestroyWindow\n");
    SDL_DestroyWindow(window);
    log_to_file("[Main] SDL_DestroyWindow done\n");

    log_to_file("[Main] SDL_Quit\n");
    SDL_Quit();
    log_to_file("[Main] SDL_Quit done\n");

    return 0;
}
