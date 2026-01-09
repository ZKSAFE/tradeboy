#include "Fonts.h"
#include <cstdio>
#include <cstdarg>

namespace tradeboy::ui {

static Fonts g_fonts;

// Simple file logger
static void log_font(const char* fmt, ...) {
    FILE* f = fopen("log.txt", "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fclose(f);
}

Fonts& fonts() { return g_fonts; }

bool init_fonts(ImGuiIO& io, const char* font_path) {
    g_fonts = Fonts{};
    if (!font_path) {
        log_font("[Fonts] Error: font_path is null\n");
        return false;
    }

    log_font("[Fonts] Loading font from: %s\n", font_path);

    ImFontConfig cfg;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;

    // Target sizes: Body=52px, Large=76px
    // Disable oversampling to save texture atlas space on RG34XX
    g_fonts.body = io.Fonts->AddFontFromFileTTF(font_path, 40.0f, &cfg, io.Fonts->GetGlyphRangesDefault());
    if (!g_fonts.body) {
        log_font("[Fonts] Failed to load body font (40px)\n");
    } else {
        log_font("[Fonts] Loaded body font (40px)\n");
    }

    g_fonts.large = io.Fonts->AddFontFromFileTTF(font_path, 58.0f, &cfg, io.Fonts->GetGlyphRangesDefault());
    if (!g_fonts.large) {
        log_font("[Fonts] Failed to load large font (58px)\n");
    } else {
        log_font("[Fonts] Loaded large font (58px)\n");
    }

    g_fonts.small = io.Fonts->AddFontFromFileTTF(font_path, 32.0f, &cfg, io.Fonts->GetGlyphRangesDefault());
    if (!g_fonts.small) {
        log_font("[Fonts] Failed to load small font (32px)\n");
    } else {
        log_font("[Fonts] Loaded small font (32px)\n");
    }

    // Build atlas immediately to check for errors
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    log_font("[Fonts] Atlas built: %dx%d\n", width, height);

    if (g_fonts.body) {
        io.FontDefault = g_fonts.body;
        return true;
    }

    return g_fonts.large != nullptr;
}

} // namespace tradeboy::ui
