// The lower screen as a window of its own. On the RG DS under ROCKNIX each panel is a separate
// Wayland output, so the game opens one fullscreen window per panel: raylib's window on the upper panel, and this one on the lower. Both share raylib's GL
// context, so textures drawn in one can be shown in the other. Only on the SDL build; elsewhere
// open() returns false and the game stacks both screens in one window.
#pragma once
#include "core.hpp"

namespace rr {

class LowerWindow {
public:
    bool open(int width, int height);
    bool active() const { return window_ != nullptr; }
    // Show a texture (drawn upside down, as render textures are) filling the lower window.
    void present(Texture2D tex);
    void close();

private:
    void* window_ = nullptr;
};

}  // namespace rr
