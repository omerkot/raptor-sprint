#include "ui.hpp"

namespace rr {

static Font g_font{};
static bool g_loaded = false;

void ui_load_font(const std::string& path) {
#if !defined(PLATFORM_ANDROID)  // (there it's inside the app, where FileExists can't see it)
    if (!FileExists(path.c_str())) {
        TraceLog(LOG_WARNING, "font: %s not found, using the built-in font", path.c_str());
        return;
    }
#endif
    // One large atlas, mipmapped, so the text stays smooth when drawn small.
    g_font = LoadFontEx(path.c_str(), 72, nullptr, 0);
    GenTextureMipmaps(&g_font.texture);
    SetTextureFilter(g_font.texture, TEXTURE_FILTER_TRILINEAR);
    g_loaded = g_font.texture.id != 0;
}

void ui_unload_font() {
    if (g_loaded) UnloadFont(g_font);
    g_loaded = false;
}

static float spacing(float size) { return size * 0.02f; }
// Nunito's letters sit a little smaller than the built-in font's at the same nominal size.
static const float kScale = 1.15f;

void ui_draw(const char* s, float x, float y, float size, Color c) {
    if (!g_loaded) {
        DrawText(s, (int)x, (int)y, (int)size, c);
        return;
    }
    size *= kScale;
    DrawTextEx(g_font, s, {std::round(x), std::round(y - size * 0.06f)}, size, spacing(size), c);
}

float ui_measure(const char* s, float size) {
    if (!g_loaded) return (float)MeasureText(s, (int)size);
    size *= kScale;
    return MeasureTextEx(g_font, s, size, spacing(size)).x;
}

}  // namespace rr
