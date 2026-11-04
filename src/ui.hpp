// Text in the game's rounded typeface (Nunito ExtraBold, SIL Open Font License), drawn smoothly at
// any size. Falls back to raylib's built-in font if the file is missing.
#pragma once
#include "core.hpp"

namespace rr {

void ui_load_font(const std::string& path);
void ui_unload_font();
void ui_draw(const char* s, float x, float y, float size, Color c);
float ui_measure(const char* s, float size);

}  // namespace rr
