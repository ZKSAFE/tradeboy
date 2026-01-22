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
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <sys/stat.h>

#include "app/App.h"
#include "app/Input.h"
#include "filters/CrtFilter.h"

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

void log_str(const char* s) {
    if (!s) return;
    FILE* f = fopen("log.txt", "a");
    if (!f) return;
    fputs(s, f);
    fclose(f);
}

static void crash_signal_handler(int sig) {
    FILE* f = fopen("log.txt", "a");
    if (f) {
        fprintf(f, "[CRASH] signal=%d\n", sig);
        // Best-effort backtrace for post-mortem debugging (core dumps are disabled on device).
        void* bt[64];
        int n = backtrace(bt, 64);
        fprintf(f, "[CRASH] backtrace_depth=%d\n", n);
        fflush(f);
        // backtrace_symbols_fd writes to a file descriptor.
        backtrace_symbols_fd(bt, n, fileno(f));
        fprintf(f, "\n");
        fclose(f);
    }
    _exit(128 + sig);
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

int main(int argc, char** argv) {
    // On device, the app may be launched with CWD=/, but our assets live under
    // /mnt/mmc/Roms/APPS. If that directory exists, switch CWD so relative
    // asset paths work (fonts, log file).
    if (dir_exists("/mnt/mmc/Roms/APPS")) {
        int rc = chdir("/mnt/mmc/Roms/APPS");
        (void)rc;
    }

    signal(SIGSEGV, crash_signal_handler);
    signal(SIGABRT, crash_signal_handler);
    signal(SIGFPE, crash_signal_handler);
    signal(SIGILL, crash_signal_handler);

    // Clear log file on startup
    FILE* f = fopen("log.txt", "w");
    if (f) {
        fprintf(f, "--- TradeBoy Log Start V6 ---\n");
        fclose(f);
    }

    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER) != 0) {
        log_str("SDL_Init failed\n");
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
    log_str("Display Mode: <logged>\n");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 6);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    SDL_Window* window = nullptr;
    {
        struct Attempt {
            const char* name;
            Uint32 flags;
        } attempts[] = {
            {"fullscreen_desktop", SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN},
            {"fullscreen", SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_SHOWN},
            {"windowed", SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN},
        };

        for (const auto& a : attempts) {
            log_str("[SDL] CreateWindow attempt\n");
            window = SDL_CreateWindow(
                "tradeboy",
                SDL_WINDOWPOS_UNDEFINED_DISPLAY(0),
                SDL_WINDOWPOS_UNDEFINED_DISPLAY(0),
                mode.w,
                mode.h,
                a.flags);
            if (window) break;
            log_str("[SDL] CreateWindow attempt failed\n");
        }
    }

    if (!window) {
        log_str("SDL_CreateWindow failed\n");
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glctx = SDL_GL_CreateContext(window);
    if (!glctx) {
        log_str("SDL_GL_CreateContext failed\n");
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

    tradeboy::filters::CrtFilter crt;
    if (!crt.init(mode.w, mode.h)) {
        log_str("[CRT] init failed, running without CRT\n");
    }

    // Font: prefer bundled cour-new.ttf for readable UI on 720x480.
    const char* font_path = nullptr;
    if (file_exists("cour-new.ttf")) font_path = "cour-new.ttf";
    else if (file_exists("output/cour-new.ttf")) font_path = "output/cour-new.ttf";
    else if (file_exists("CourierPrime-Regular.ttf")) font_path = "CourierPrime-Regular.ttf";
    else if (file_exists("output/NotoSansCJK-Regular.ttc")) font_path = "output/NotoSansCJK-Regular.ttc";
    else if (file_exists("NotoSansCJK-Regular.ttc")) font_path = "NotoSansCJK-Regular.ttc";

    if (font_path) {
        log_str("[Main] Loading font\n");
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
            log_str("[Main] Font atlas built\n");
        } else {
            log_str("[Main] Font load failed, using default\n");
        }
    } else {
        log_str("[Main] No font file found, using default\n");
    }

    // Load Bold Font
    const char* font_path_bold = nullptr;
    if (file_exists("cour-new-BOLDITALIC.ttf")) font_path_bold = "cour-new-BOLDITALIC.ttf";
    else if (file_exists("output/cour-new-BOLDITALIC.ttf")) font_path_bold = "output/cour-new-BOLDITALIC.ttf";
    
    ImFont* loaded_font_bold = nullptr;
    if (font_path_bold) {
         log_str("[Main] Loading bold font\n");
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

    log_str("[Main] Allocating App on heap...\n");
    auto* app_ptr = new tradeboy::app::App();
    log_str("[Main] new App returned\n");
    tradeboy::app::App& app = *app_ptr;

    log_str("[Main] App constructed\n");
    app.font_bold = loaded_font_bold;
    log_str("[Main] init_demo_data begin\n");
    app.init_demo_data();
    log_str("[Main] init_demo_data done\n");
    log_str("[Main] calling startup\n");
    app.startup();
    log_str("[Main] app.startup done\n");

    tradeboy::app::EdgeState edges;

    bool running = true;
    log_str("[Main] entering main loop\n");
    int frame_counter = 0;
    while (running) {
        std::vector<SDL_Event> events;
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            events.push_back(e);
            if (e.type == SDL_QUIT) {
                log_str("[Main] SDL_QUIT event\n");
                running = false;
            }
        }

        tradeboy::app::InputState in = tradeboy::app::poll_input_state_from_events(events);
        app.handle_input_edges(in, edges);
        edges.prev = in;

        if (app.quit_requested) {
            log_str("[Main] app.quit_requested\n");
            running = false;
        }

        if (frame_counter == 0) {
            log_str("[Main] first frame\n");
        }
        frame_counter++;

        if (frame_counter <= 5) log_str("[Frame] begin\n");

        if (frame_counter <= 5) log_str("[Frame] ImGui_ImplOpenGL3_NewFrame\n");
        ImGui_ImplOpenGL3_NewFrame();
        if (frame_counter <= 5) log_str("[Frame] ImGui_ImplSDL2_NewFrame\n");
        ImGui_ImplSDL2_NewFrame();
        if (frame_counter <= 5) log_str("[Frame] ImGui::NewFrame\n");
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)mode.w, (float)mode.h), ImGuiCond_Always);

        ImGuiWindowFlags wflags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBackground;
        ImGui::Begin("Spot", nullptr, wflags);
        if (frame_counter <= 5) log_str("[Frame] app.render begin\n");
        app.render();
        if (frame_counter <= 5) log_str("[Frame] app.render end\n");
        ImGui::End();

        if (frame_counter <= 5) log_str("[Frame] ImGui::Render\n");
        ImGui::Render();

        if (crt.is_ready()) {
            if (frame_counter <= 5) log_str("[Frame] crt.begin\n");
            crt.begin();
            if (frame_counter <= 5) log_str("[Frame] ImGui_ImplOpenGL3_RenderDrawData\n");
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            crt.set_overlay_rect_uv(app.overlay_rect_uv, app.overlay_rect_active);
            crt.set_poweroff(app.exit_poweroff_anim_t, app.exit_poweroff_anim_active);
            crt.set_boot(app.boot_anim_t, app.boot_anim_active && !app.exit_poweroff_anim_active);
            if (frame_counter <= 5) log_str("[Frame] crt.end\n");
            crt.end((float)ImGui::GetTime());
        } else {
            glViewport(0, 0, mode.w, mode.h);
            glClearColor(0.10f, 0.11f, 0.09f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        if (frame_counter <= 5) log_str("[Frame] SDL_GL_SwapWindow\n");
        SDL_GL_SwapWindow(window);

        if (frame_counter <= 5) log_str("[Frame] end\n");
    }

    log_str("[Main] main loop exit -> begin shutdown\n");

    log_str("[Main] calling app.shutdown()\n");
    app.shutdown();
    log_str("[Main] app.shutdown() done\n");

    log_str("[Main] deleting app_ptr\n");
    delete app_ptr;
    log_str("[Main] delete app_ptr done\n");

    if (joy0) {
        log_str("[Main] SDL_JoystickClose\n");
        SDL_JoystickClose(joy0);
        log_str("[Main] SDL_JoystickClose done\n");
    }

    log_str("[Main] ImGui_ImplOpenGL3_Shutdown\n");
    ImGui_ImplOpenGL3_Shutdown();
    log_str("[Main] ImGui_ImplOpenGL3_Shutdown done\n");

    log_str("[Main] ImGui_ImplSDL2_Shutdown\n");
    ImGui_ImplSDL2_Shutdown();
    log_str("[Main] ImGui_ImplSDL2_Shutdown done\n");

    log_str("[Main] ImGui::DestroyContext\n");
    ImGui::DestroyContext();
    log_str("[Main] ImGui::DestroyContext done\n");

    crt.shutdown();

    log_str("[Main] SDL_GL_DeleteContext\n");
    SDL_GL_DeleteContext(glctx);
    log_str("[Main] SDL_GL_DeleteContext done\n");

    log_str("[Main] SDL_DestroyWindow\n");
    SDL_DestroyWindow(window);
    log_str("[Main] SDL_DestroyWindow done\n");

    log_str("[Main] SDL_Quit\n");
    SDL_Quit();
    log_str("[Main] SDL_Quit done\n");

    return 0;
}
