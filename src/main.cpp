#include <SDL.h>
#include <SDL_opengles2.h>

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

#include <fstream>
#include <string>
#include <vector>

#include "app/App.h"
#include "app/Input.h"
#include "uiComponents/Fonts.h"
#include "uiComponents/Theme.h"

static bool file_exists(const char* path) {
    if (!path) return false;
    std::ifstream f(path);
    return f.good();
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER) != 0) {
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
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glctx = SDL_GL_CreateContext(window);
    if (!glctx) {
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

    tradeboy::ui::apply_retro_style();

    const char* glsl_version = "#version 100";
    ImGui_ImplSDL2_InitForOpenGL(window, glctx);
    ImGui_ImplOpenGL3_Init(glsl_version);

    const char* font_path = nullptr;
    if (file_exists("output/NotoSansCJK-Regular.ttc")) font_path = "output/NotoSansCJK-Regular.ttc";
    else if (file_exists("NotoSansCJK-Regular.ttc")) font_path = "NotoSansCJK-Regular.ttc";

    tradeboy::ui::init_fonts(io, font_path);

    SDL_Joystick* joy0 = nullptr;
    if (SDL_NumJoysticks() > 0) {
        joy0 = SDL_JoystickOpen(0);
    }

    tradeboy::app::App app;
    app.init_demo_data();
    app.load_private_key();

    tradeboy::app::EdgeState edges;

    bool running = true;
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

    if (joy0) SDL_JoystickClose(joy0);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(glctx);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
