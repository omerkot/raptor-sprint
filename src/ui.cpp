#include "ui.hpp"

#include <cctype>
#include <cstring>

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

// On the Mac with no controller connected, what the text calls buttons are keys. (With one
// connected it means what it says, on either.) On a phone they're the buttons on the screen.
static const char* shown(const char* s) {
#if defined(GRAPHICS_API_OPENGL_33) || defined(__EMSCRIPTEN__)
    if (IsGamepadAvailable(0) || !std::strpbrk(s, "ABR")) return s;
    static const struct { const char* from; const char* to; } kKeys[] = {
#if defined(__EMSCRIPTEN__)
        {"(A)", ""}, {"(B)", ""}, {"(R)", ""}, {"hold B", "hold Duck"}, {"Sprint: R", "Sprint"},
        {"Brake: Down", "Brake: pull the stick down"}, {"A  continue", "Tap to continue"},
#else
        {"(A)", "(Space)"}, {"(B)", "(Z)"}, {"(R)", "(Shift)"}, {"hold B", "hold Z"}, {"Sprint: R", "Sprint: Shift"},
        {"B / Start", "Z / Esc"}, {"A  ", "Space  "}, {"B  ", "Z  "},
#endif
    };
    static std::string out;
    out = s;
    for (const auto& k : kKeys)
        for (size_t at = out.find(k.from); at != std::string::npos; at = out.find(k.from, at + std::strlen(k.to))) {
            if (at > 0 && std::isalpha((unsigned char)out[at - 1])) {  // (the end of a word, not a button)
                at += 1 + std::strlen(k.from) - std::strlen(k.to);
                continue;
            }
            out.replace(at, std::strlen(k.from), k.to);
        }
    return out.c_str();
#else
    return s;
#endif
}

void ui_draw(const char* s, float x, float y, float size, Color c) {
    s = shown(s);
    if (!g_loaded) {
        DrawText(s, (int)x, (int)y, (int)size, c);
        return;
    }
    size *= kScale;
    DrawTextEx(g_font, s, {std::round(x), std::round(y - size * 0.06f)}, size, spacing(size), c);
}

float ui_measure(const char* s, float size) {
    s = shown(s);
    if (!g_loaded) return (float)MeasureText(s, (int)size);
    size *= kScale;
    return MeasureTextEx(g_font, s, size, spacing(size)).x;
}

}  // namespace rr
