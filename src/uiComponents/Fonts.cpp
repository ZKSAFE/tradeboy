#include "Fonts.h"

namespace tradeboy::ui {

static Fonts g_fonts;

Fonts& fonts() { return g_fonts; }

bool init_fonts(ImGuiIO& io, const char* font_path) {
    g_fonts = Fonts{};
    if (!font_path) return false;

    // Explicit sizes; do not rely on global scaling.
    g_fonts.body = io.Fonts->AddFontFromFileTTF(font_path, 44.0f, nullptr, io.Fonts->GetGlyphRangesDefault());
    g_fonts.large = io.Fonts->AddFontFromFileTTF(font_path, 66.0f, nullptr, io.Fonts->GetGlyphRangesDefault());

    if (g_fonts.body) {
        io.FontDefault = g_fonts.body;
        return true;
    }

    return g_fonts.large != nullptr;
}

} // namespace tradeboy::ui
