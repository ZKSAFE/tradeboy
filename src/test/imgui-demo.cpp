#include <SDL2/SDL.h>
#include <GLES2/gl2.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static std::string get_gl_string(GLenum name) {
    const char* s = (const char*)glGetString(name);
    return s ? std::string(s) : std::string("(null)");
}

static void append_line(std::string& dst, const std::string& line) {
    dst.append(line);
    if (!dst.empty() && dst.back() != '\n') dst.push_back('\n');
}

struct PingJob {
    std::thread th;
    std::atomic<bool> running{false};
    std::mutex mu;
    std::string output;

    void start() {
        stop();
        running = true;
        {
            std::lock_guard<std::mutex> lk(mu);
            output.clear();
            append_line(output, "Running: ping -c 3 www.baidu.com");
        }
        th = std::thread([this]() {
            FILE* fp = popen("ping -c 3 www.baidu.com 2>&1", "r");
            if (!fp) {
                std::lock_guard<std::mutex> lk(mu);
                append_line(output, "popen() failed");
                running = false;
                return;
            }
            char buf[256];
            while (fgets(buf, sizeof(buf), fp)) {
                std::lock_guard<std::mutex> lk(mu);
                output.append(buf);
            }
            int rc = pclose(fp);
            {
                std::lock_guard<std::mutex> lk(mu);
                char tail[64];
                snprintf(tail, sizeof(tail), "ping exit code: %d\n", rc);
                output.append(tail);
            }
            running = false;
        });
    }

    void stop() {
        if (th.joinable()) {
            // No clean cancel for ping; just join if already done.
            // If still running, leave it; popup can be closed without waiting.
            th.detach();
        }
        running = false;
    }

    std::string snapshot() {
        std::lock_guard<std::mutex> lk(mu);
        return output;
    }
};

static const char* rg34xx_button_name(uint8_t button) {
    // Based on rg34xx-native-app SDL_JoyButton mapping
    switch (button) {
        case 0: return "A";
        case 1: return "B";
        case 2: return "Y";
        case 3: return "X";
        case 4: return "L1";
        case 5: return "R1";
        case 6: return "SELECT";
        case 7: return "START";
        case 8: return "M";
        case 9: return "L2";
        case 10: return "R2";
        case 13: return "VOL-";
        case 14: return "VOL+";
        default: return "(BTN)";
    }
}

static std::string format_joy_button(const SDL_JoyButtonEvent& e) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "Joystick%d: %s %s (id=%d)", (int)e.which, rg34xx_button_name(e.button), (e.state == SDL_PRESSED ? "Pressed" : "Released"), (int)e.button);
    return std::string(tmp);
}

static const char* rg34xx_hat_dir_name(uint8_t value) {
    // Observed mapping on RG34XX:
    // 0=CENTERED, 1=UP, 2=RIGHT, 3=RIGHT+UP, 4=DOWN, 6=RIGHT+DOWN,
    // 12=LEFT+DOWN, 8=LEFT, 9=LEFT+UP
    switch (value) {
        case 0: return "CENTER";
        case 1: return "UP";
        case 2: return "RIGHT";
        case 3: return "RIGHT+UP";
        case 4: return "DOWN";
        case 6: return "RIGHT+DOWN";
        case 8: return "LEFT";
        case 9: return "LEFT+UP";
        case 12: return "LEFT+DOWN";
        default: return "(HAT)";
    }
}

static std::string format_joy_hat(const SDL_JoyHatEvent& e) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "Joystick%d: Hat %s (value=%d)", (int)e.which, rg34xx_hat_dir_name(e.value), (int)e.value);
    return std::string(tmp);
}

static std::string format_joy_axis(const SDL_JoyAxisEvent& e) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "Joystick%d: Axis%d value=%d", (int)e.which, (int)e.axis, (int)e.value);
    return std::string(tmp);
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
        mode.w = 640;
        mode.h = 480;
        mode.refresh_rate = 60;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    SDL_Window* window = SDL_CreateWindow(
        "imgui-demo",
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

    std::string gl_vendor = get_gl_string(GL_VENDOR);
    std::string gl_renderer = get_gl_string(GL_RENDERER);
    std::string gl_version = get_gl_string(GL_VERSION);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    // GLSL ES 1.00
    const char* glsl_version = "#version 100";
    ImGui_ImplSDL2_InitForOpenGL(window, glctx);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Font: optional. If the font file exists on device, user can copy it.
    // We keep defaults to avoid hard dependency.

    bool show_ping_popup = false;
    PingJob ping;

    std::string last_event = "(none)";
    std::string device_info;
    {
        append_line(device_info, "Device info");
        append_line(device_info, "GL_VENDOR: " + gl_vendor);
        append_line(device_info, "GL_RENDERER: " + gl_renderer);
        append_line(device_info, "GL_VERSION: " + gl_version);
        append_line(device_info, "Display0: " + std::to_string(mode.w) + "x" + std::to_string(mode.h) + "@" + std::to_string(mode.refresh_rate));
        append_line(device_info, "SDL_NumJoysticks: " + std::to_string(SDL_NumJoysticks()));
        if (SDL_NumJoysticks() > 0) {
            const char* name = SDL_JoystickNameForIndex(0);
            append_line(device_info, std::string("Joystick0: ") + (name ? name : "(null)"));
        }
    }

    // Open joystick 0 if available
    SDL_Joystick* joy0 = nullptr;
    if (SDL_NumJoysticks() > 0) {
        joy0 = SDL_JoystickOpen(0);
    }

    auto last_input = std::chrono::steady_clock::now();
    const auto idle_timeout = std::chrono::seconds(25);

    bool running = true;
    while (running) {
        SDL_Event e;
        bool any_key_down = false;

        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);

            switch (e.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_KEYDOWN:
                case SDL_JOYBUTTONDOWN:
                case SDL_JOYHATMOTION:
                case SDL_JOYAXISMOTION:
                    last_input = std::chrono::steady_clock::now();
                    break;
                default:
                    break;
            }

            if (e.type == SDL_KEYDOWN) {
                any_key_down = true;
                const char* kn = SDL_GetKeyName(e.key.keysym.sym);
                if (!kn || !kn[0]) kn = "(unknown)";
                char tmp[128];
                if (e.key.keysym.sym == SDLK_POWER) {
                    snprintf(tmp, sizeof(tmp), "Keyboard: POWER (%s)", kn);
                } else {
                    snprintf(tmp, sizeof(tmp), "Keyboard: %s", kn);
                }
                last_event = std::string(tmp);
            } else if (e.type == SDL_JOYBUTTONDOWN) {
                any_key_down = true;
                last_event = format_joy_button(e.jbutton);
            } else if (e.type == SDL_JOYHATMOTION) {
                last_event = format_joy_hat(e.jhat);
            } else if (e.type == SDL_JOYAXISMOTION) {
                // Only treat large axis movement as activity to avoid noise
                if (e.jaxis.value > 8000 || e.jaxis.value < -8000) {
                    last_event = format_joy_axis(e.jaxis);
                }
            }
        }

        // Any key opens the popup; when popup is open, any key closes it.
        bool request_open_popup = any_key_down && !show_ping_popup;
        bool request_close_popup = any_key_down && show_ping_popup;

        if (std::chrono::steady_clock::now() - last_input > idle_timeout) {
            running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2((float)mode.w - 40.0f, (float)mode.h - 40.0f), ImGuiCond_Always);

        ImGui::Begin("RG34XX ImGui Demo", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

        ImGui::Text("FPS: %.1f", io.Framerate);
        ImGui::Separator();
        ImGui::TextUnformatted(device_info.c_str());
        ImGui::Separator();
        ImGui::Text("Last input: %s", last_event.c_str());

        if (ImGui::Button("Network test (ping www.baidu.com)")) {
            show_ping_popup = true;
            ping.start();
            ImGui::OpenPopup("Network test");
        }

        if (request_open_popup) {
            show_ping_popup = true;
            ping.start();
            ImGui::OpenPopup("Network test");
        }

        if (ImGui::BeginPopupModal("Network test", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Press ANY key to close");
            ImGui::Separator();

            std::string out = ping.snapshot();
            ImGui::BeginChild("ping_out", ImVec2(0, 220), true);
            ImGui::TextUnformatted(out.c_str());
            ImGui::EndChild();

            if (request_close_popup) {
                show_ping_popup = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::End();

        ImGui::Render();

        glViewport(0, 0, mode.w, mode.h);
        glClearColor(0.05f, 0.06f, 0.07f, 1.0f);
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
