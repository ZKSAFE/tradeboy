#include <SDL.h>
#include <SDL_opengles2.h>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include <fstream>
#include <string>
#include <vector>
#include <cstdarg>
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
        fprintf(f, "--- TradeBoy Log Start ---\n");
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

    // New Spot UI currently uses only ImGui default font (no external font dependency).

    SDL_Joystick* joy0 = nullptr;
    if (SDL_NumJoysticks() > 0) {
        joy0 = SDL_JoystickOpen(0);
    }

    tradeboy::app::App app;
    log_to_file("[Main] App constructed\n");
    app.init_demo_data();
    log_to_file("[Main] init_demo_data done\n");
    app.load_private_key();
    log_to_file("[Main] load_private_key done\n");
    app.startup();
    log_to_file("[Main] app.startup done\n");

    tradeboy::app::EdgeState edges;

    bool running = true;
    log_to_file("[Main] entering main loop\n");
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

        glViewport(0, 0, mode.w, mode.h);
        glClearColor(0.10f, 0.11f, 0.09f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    app.shutdown();

    if (joy0) SDL_JoystickClose(joy0);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(glctx);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
