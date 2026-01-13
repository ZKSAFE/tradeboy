#include <SDL2/SDL.h>
#include <GLES2/gl2.h>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdio>
 #include <unistd.h>
 #include <sys/stat.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

 static bool dir_exists(const char* path) {
     if (!path) return false;
     struct stat st;
     if (stat(path, &st) != 0) return false;
     return S_ISDIR(st.st_mode);
 }

// --- Mock Data ---
struct Coin {
    std::string id;
    std::string symbol;
    std::string name;
    double price;
    double change24h;
    double holdings;
};

static std::vector<Coin> MOCK_COINS = {
    { "1", "BTC", "Bitcoin", 64230.50, 2.4, 0.15 },
    { "2", "ETH", "Ethereum", 3450.12, -1.2, 2.5 },
    { "3", "SOL", "Solana", 145.60, 5.8, 100.0 },
    { "4", "DOGE", "Dogecoin", 0.12, 0.5, 5000.0 },
    { "5", "ADA", "Cardano", 0.45, -3.4, 0.0 },
    { "6", "XRP", "Ripple", 0.60, 1.1, 0.0 },
    { "7", "DOT", "Polkadot", 7.20, -0.8, 0.0 },
};

// --- Theme Constants ---
namespace MatrixTheme {
    const ImU32 BG = IM_COL32(0, 0, 0, 255);
    const ImU32 TEXT = IM_COL32(0, 255, 65, 255);       // #00FF41
    const ImU32 DIM = IM_COL32(0, 143, 17, 255);        // #008F11
    const ImU32 DARK = IM_COL32(0, 59, 0, 255);         // #003B00
    const ImU32 ALERT = IM_COL32(255, 0, 85, 255);      // #FF0055
    const ImU32 BLACK = IM_COL32(0, 0, 0, 255);
}

// --- Helper Functions ---
void DrawGlowText(ImDrawList* dl, const ImVec2& pos, const char* text, ImU32 color) {
    // Simple glow effect by drawing transparency behind
    ImU32 glowCol = (color & 0x00FFFFFF) | 0x40000000; // ~25% alpha
    dl->AddText(ImVec2(pos.x + 1, pos.y + 1), glowCol, text);
    dl->AddText(ImVec2(pos.x - 1, pos.y - 1), glowCol, text);
    dl->AddText(pos, color, text);
}

// --- Main Application State ---
struct AppState {
    int selectedIndex = 0;
    int footerActionIndex = 0; // 0: BUY, 1: SELL
    bool buyPressed = false;
    bool sellPressed = false;
};

void RenderMarketView(AppState& state, const ImVec2& size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos(); // Top-left of the content area
    
    // Background Grid
    const float gridStep = 40.0f;
    ImU32 gridCol = IM_COL32(0, 255, 65, 20);
    for (float x = 0; x < size.x; x += gridStep)
        dl->AddLine(ImVec2(p.x + x, p.y), ImVec2(p.x + x, p.y + size.y), gridCol);
    for (float y = 0; y < size.y; y += gridStep)
        dl->AddLine(ImVec2(p.x, p.y + y), ImVec2(p.x + size.x, p.y + y), gridCol);

    // Layout Constants
    const float padding = 16.0f;
    const float headerH = 60.0f;
    const float footerH = 70.0f;
    const float tableHeaderH = 30.0f;
    const float rowH = 44.0f;
    
    float y = p.y + padding;
    float w = size.x - 2 * padding;
    float left = p.x + padding;
    float right = p.x + size.x - padding;

    // --- Header ---
    {
        ImGui::SetWindowFontScale(1.5f); // Larger title
        const char* title = "> SPOT_TRADE";
        DrawGlowText(dl, ImVec2(left, y), title, MatrixTheme::TEXT);
        ImGui::SetWindowFontScale(1.0f);

        const char* nav = "PERP | ASSET | PROFILE";
        ImVec2 navSz = ImGui::CalcTextSize(nav);
        dl->AddText(ImVec2(right - navSz.x - 70, y + 8), MatrixTheme::DIM, nav);

        // L1 R1 hints
        dl->AddRectFilled(ImVec2(right - 50, y), ImVec2(right - 30, y + 20), MatrixTheme::DIM, 2.0f);
        dl->AddText(ImVec2(right - 48, y + 2), MatrixTheme::BLACK, "L1");
        dl->AddRectFilled(ImVec2(right - 25, y), ImVec2(right - 5, y + 20), MatrixTheme::DIM, 2.0f);
        dl->AddText(ImVec2(right - 23, y + 2), MatrixTheme::BLACK, "R1");
        
        y += headerH;
        
        // Separator line
        dl->AddLine(ImVec2(left, y - 10), ImVec2(right, y - 10), MatrixTheme::DIM, 2.0f);
    }

    // --- Table Headers ---
    {
        float col1 = left;
        float col2 = left + w * 0.35f;
        float col3 = right - 130;
        float col4 = right;

        dl->AddText(ImVec2(col1 + 30, y), MatrixTheme::DIM, "CODE"); // +30 for cursor space
        
        const char* h2 = "HOLDINGS";
        ImVec2 sz2 = ImGui::CalcTextSize(h2);
        dl->AddText(ImVec2(col2 - sz2.x * 0.5f, y), MatrixTheme::DIM, h2);
        
        const char* h3 = "PRICE";
        ImVec2 sz3 = ImGui::CalcTextSize(h3);
        dl->AddText(ImVec2(col3 - sz3.x, y), MatrixTheme::DIM, h3);
        
        const char* h4 = "24H";
        ImVec2 sz4 = ImGui::CalcTextSize(h4);
        dl->AddText(ImVec2(col4 - sz4.x, y), MatrixTheme::DIM, h4);

        y += tableHeaderH;
    }

    // --- Coin List ---
    {
        float listH = size.y - padding - footerH - y + p.y;
        
        // Auto scroll logic
        int startIdx = 0;
        int maxRows = (int)(listH / rowH);
        if (state.selectedIndex >= maxRows) {
            startIdx = state.selectedIndex - maxRows + 1;
        }

        for (int i = startIdx; i < (int)MOCK_COINS.size() && (i - startIdx) < maxRows; ++i) {
            const auto& coin = MOCK_COINS[i];
            bool isSelected = (i == state.selectedIndex);
            
            float rowY = y + (i - startIdx) * rowH;
            
            // Row Background
            if (isSelected) {
                // Inverted style for selection
                dl->AddRectFilled(ImVec2(left, rowY), ImVec2(right, rowY + rowH - 4), MatrixTheme::TEXT, 4.0f);
            } else {
                // Subtle dark strip
                dl->AddRectFilled(ImVec2(left, rowY), ImVec2(right, rowY + rowH - 4), IM_COL32(0, 59, 0, 40), 4.0f);
            }

            ImU32 textCol = isSelected ? MatrixTheme::BLACK : MatrixTheme::TEXT;
            ImU32 numCol = isSelected ? MatrixTheme::BLACK : MatrixTheme::TEXT;
            ImU32 dimCol = isSelected ? MatrixTheme::BLACK : MatrixTheme::DIM;
            ImU32 changeCol = isSelected ? MatrixTheme::BLACK : (coin.change24h >= 0 ? MatrixTheme::TEXT : MatrixTheme::ALERT);

            // Columns
            float col1 = left;
            float col2 = left + w * 0.35f;
            float col3 = right - 130;
            float col4 = right;

            // Cursor
            if (isSelected) {
                // Blink effect
                if ((ImGui::GetTime() * 2.0 - (int)(ImGui::GetTime() * 2.0)) < 0.7)
                    dl->AddText(ImVec2(col1 + 5, rowY + 8), textCol, ">");
            }

            // Symbol
            dl->AddText(ImVec2(col1 + 30, rowY + 8), textCol, coin.symbol.c_str());

            // Holdings
            if (coin.holdings > 0) {
                char holdBuf[32];
                snprintf(holdBuf, 32, "%.2f", coin.holdings);
                ImVec2 sz = ImGui::CalcTextSize(holdBuf);
                dl->AddText(ImVec2(col2 - sz.x * 0.5f, rowY + 8), textCol, holdBuf);
            }

            // Price
            char priceBuf[32];
            snprintf(priceBuf, 32, "%.2f", coin.price);
            ImVec2 szP = ImGui::CalcTextSize(priceBuf);
            dl->AddText(ImVec2(col3 - szP.x, rowY + 8), numCol, priceBuf);

            // Change
            char chgBuf[32];
            snprintf(chgBuf, 32, "%+.1f%%", coin.change24h);
            ImVec2 szC = ImGui::CalcTextSize(chgBuf);
            dl->AddText(ImVec2(col4 - szC.x, rowY + 8), changeCol, chgBuf);
        }
    }

    // --- Footer ---
    {
        float footerTop = p.y + size.y - footerH;
        
        // Separator
        dl->AddLine(ImVec2(left, footerTop), ImVec2(right, footerTop), MatrixTheme::DIM, 4.0f);
        
        const auto& selCoin = MOCK_COINS[state.selectedIndex];
        char summary[128];
        double val = selCoin.holdings * selCoin.price;
        if (selCoin.holdings > 0)
            snprintf(summary, 128, "Holding %.2f %s worth $%.2f", selCoin.holdings, selCoin.symbol.c_str(), val);
        else
            snprintf(summary, 128, "No %s in wallet", selCoin.symbol.c_str());
            
        dl->AddText(ImVec2(left, footerTop + 20), MatrixTheme::TEXT, summary);

        // Buttons
        float btnW = 100.0f;
        float btnH = 40.0f;
        float btnY = footerTop + 15;
        float sellX = right - btnW;
        float buyX = sellX - btnW - 20;

        // BUY
        bool buyFocus = (state.footerActionIndex == 0);
        ImU32 buyBg = buyFocus ? MatrixTheme::TEXT : MatrixTheme::BG;
        ImU32 buyFg = buyFocus ? MatrixTheme::BLACK : MatrixTheme::DIM;
        ImU32 buyBorder = buyFocus ? MatrixTheme::TEXT : MatrixTheme::DIM;
        
        if (state.buyPressed && buyFocus) { buyBg = MatrixTheme::DIM; } // Press effect

        dl->AddRectFilled(ImVec2(buyX, btnY), ImVec2(buyX + btnW, btnY + btnH), buyBg, 4.0f);
        dl->AddRect(ImVec2(buyX, btnY), ImVec2(buyX + btnW, btnY + btnH), buyBorder, 4.0f, 0, 2.0f);
        ImVec2 tSz = ImGui::CalcTextSize("BUY");
        dl->AddText(ImVec2(buyX + (btnW - tSz.x)*0.5f, btnY + (btnH - tSz.y)*0.5f), buyFg, "BUY");
        
        if (buyFocus) {
             // Glow border
             dl->AddRect(ImVec2(buyX-2, btnY-2), ImVec2(buyX + btnW+2, btnY + btnH+2), IM_COL32(0,255,65,100), 4.0f, 0, 2.0f);
             dl->AddText(ImVec2(buyX - 15, btnY + 10), MatrixTheme::TEXT, ">");
        }

        // SELL
        bool sellFocus = (state.footerActionIndex == 1);
        bool hasHoldings = (selCoin.holdings > 0);
        ImU32 sellBg = sellFocus ? MatrixTheme::TEXT : MatrixTheme::BG;
        ImU32 sellFg = sellFocus ? MatrixTheme::BLACK : MatrixTheme::DIM;
        ImU32 sellBorder = sellFocus ? MatrixTheme::TEXT : MatrixTheme::DIM;

        if (!hasHoldings) {
            sellBg = MatrixTheme::BG;
            sellFg = MatrixTheme::DARK;
            sellBorder = MatrixTheme::DARK;
        } else if (state.sellPressed && sellFocus) {
             sellBg = MatrixTheme::DIM;
        }

        dl->AddRectFilled(ImVec2(sellX, btnY), ImVec2(sellX + btnW, btnY + btnH), sellBg, 4.0f);
        dl->AddRect(ImVec2(sellX, btnY), ImVec2(sellX + btnW, btnY + btnH), sellBorder, 4.0f, 0, 2.0f);
        ImVec2 sSz = ImGui::CalcTextSize("SELL");
        dl->AddText(ImVec2(sellX + (btnW - sSz.x)*0.5f, btnY + (btnH - sSz.y)*0.5f), sellFg, "SELL");

        if (sellFocus && hasHoldings) {
             dl->AddRect(ImVec2(sellX-2, btnY-2), ImVec2(sellX + btnW+2, btnY + btnH+2), IM_COL32(0,255,65,100), 4.0f, 0, 2.0f);
             dl->AddText(ImVec2(sellX - 15, btnY + 10), MatrixTheme::TEXT, ">");
        }
    }
}

int main(int argc, char** argv) {
     (void)argc;
     (void)argv;
 
     // Match main.cpp: ensure assets resolve when launched with CWD=/
     if (dir_exists("/mnt/mmc/Roms/APPS")) {
         chdir("/mnt/mmc/Roms/APPS");
     }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_JoystickEventState(SDL_ENABLE);
    if (SDL_NumJoysticks() > 0) {
        SDL_JoystickOpen(0);
    }

    // GLES 2.0 setup
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);

    SDL_DisplayMode mode;
    if (SDL_GetCurrentDisplayMode(0, &mode) != 0) {
        fprintf(stderr, "SDL_GetCurrentDisplayMode failed: %s\n", SDL_GetError());
        mode.w = 720;
        mode.h = 480;
        mode.refresh_rate = 60;
    }
    fprintf(stderr, "[Demo] Display Mode: %dx%d @ %dHz\n", mode.w, mode.h, mode.refresh_rate);

    SDL_Window* window = SDL_CreateWindow(
        "TradeBoy UI Demo",
        SDL_WINDOWPOS_UNDEFINED_DISPLAY(0),
        SDL_WINDOWPOS_UNDEFINED_DISPLAY(0),
        mode.w,
        mode.h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);

    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    if (!gl_context) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        return 1;
    }

    if (SDL_GL_MakeCurrent(window, gl_context) != 0) {
        fprintf(stderr, "SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    
    // Style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0,0);
    style.WindowBorderSize = 0;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0,0,0,1);

    // Font loading
    const char* fontPath = "output/CourierPrime-Regular.ttf";
    FILE* f = fopen(fontPath, "rb");
    if (!f) fontPath = "CourierPrime-Regular.ttf";
    else fclose(f);

    ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath, 24.0f);
    if (!font) {
        io.Fonts->AddFontDefault();
        fprintf(stderr, "Failed to load font, using default.\n");
    }

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    const char* gl_ver = (const char*)glGetString(GL_VERSION);
    const char* glsl_ver = (const char*)glGetString(0x8B8C); // GL_SHADING_LANGUAGE_VERSION
    fprintf(stderr, "[Demo] GL_VERSION: %s\n", gl_ver ? gl_ver : "(null)");
    fprintf(stderr, "[Demo] GLSL_VERSION: %s\n", glsl_ver ? glsl_ver : "(null)");
    
    ImGui_ImplOpenGL3_Init("#version 100");

    AppState state;
    bool done = false;
    
    // Map RG34XX keys
    // A=0, B=1, Y=2, X=3, L1=4, R1=5, START=7, SELECT=6
    // We'll use keyboard too for testing
    
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) done = true;
            
            // KEYBOARD
            if (event.type == SDL_KEYDOWN) {
                switch(event.key.keysym.sym) {
                    case SDLK_ESCAPE: done = true; break;
                    case SDLK_UP: 
                        state.selectedIndex = std::max(0, state.selectedIndex - 1); 
                        break;
                    case SDLK_DOWN: 
                        state.selectedIndex = std::min((int)MOCK_COINS.size() - 1, state.selectedIndex + 1); 
                        break;
                    case SDLK_LEFT:
                        state.footerActionIndex = 0;
                        break;
                    case SDLK_RIGHT:
                        state.footerActionIndex = 1;
                        break;
                    case SDLK_RETURN: 
                        if (state.footerActionIndex == 0) state.buyPressed = true;
                        else state.sellPressed = true;
                        break;
                }
            }
            if (event.type == SDL_KEYUP) {
                 if (event.key.keysym.sym == SDLK_RETURN) {
                     state.buyPressed = false;
                     state.sellPressed = false;
                 }
            }
            
            // JOYSTICK / GAMEPAD
            if (event.type == SDL_JOYBUTTONDOWN) {
                int btn = event.jbutton.button;
                // A button (0) to confirm
                if (btn == 0) {
                    if (state.footerActionIndex == 0) state.buyPressed = true;
                    else state.sellPressed = true;
                }
                // D-Pad is often mapped to Hat or Axis, but if Buttons:
                // Assuming standard RG34XX behavior where D-Pad is Hat 0
            }
            if (event.type == SDL_JOYBUTTONUP) {
                if (event.jbutton.button == 0) {
                    state.buyPressed = false;
                    state.sellPressed = false;
                }
            }
            if (event.type == SDL_JOYHATMOTION) {
                if (event.jhat.value == SDL_HAT_UP) {
                    state.selectedIndex = std::max(0, state.selectedIndex - 1); 
                }
                else if (event.jhat.value == SDL_HAT_DOWN) {
                    state.selectedIndex = std::min((int)MOCK_COINS.size() - 1, state.selectedIndex + 1); 
                }
                else if (event.jhat.value == SDL_HAT_LEFT) {
                    state.footerActionIndex = 0;
                }
                else if (event.jhat.value == SDL_HAT_RIGHT) {
                    state.footerActionIndex = 1;
                }
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0,0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("MarketView", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus);
        
        RenderMarketView(state, io.DisplaySize);

        ImGui::End();

        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
