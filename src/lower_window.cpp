#include "lower_window.hpp"

#if defined(PLATFORM_DESKTOP_SDL)
#include <SDL.h>

namespace rr {

bool LowerWindow::open(int width, int height) {
    if (SDL_GetNumVideoDisplays() < 2) return false;
    SDL_Window* main = (SDL_Window*)GetWindowHandle();
    SDL_GLContext ctx = SDL_GL_GetCurrentContext();
    if (!main || !ctx) return false;
    // Display 1 is the lower panel (the launcher sets SDL_VIDEO_DISPLAY_PRIORITY=DSI-2,DSI-1).
    SDL_Window* w = SDL_CreateWindow("Raptor Sprint lower", SDL_WINDOWPOS_UNDEFINED_DISPLAY(1), SDL_WINDOWPOS_UNDEFINED_DISPLAY(1), width,
                                     height, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!w) {
        TraceLog(LOG_WARNING, "lower window: %s", SDL_GetError());
        return false;
    }
    if (SDL_GL_MakeCurrent(w, ctx) != 0) {
        TraceLog(LOG_WARNING, "lower window: can't share the context: %s", SDL_GetError());
        SDL_DestroyWindow(w);
        SDL_GL_MakeCurrent(main, ctx);
        return false;
    }
    // Only the upper window waits for the display; this one presents straight away, so the
    // two swaps don't halve the frame rate.
    SDL_GL_SetSwapInterval(0);
    SDL_GL_MakeCurrent(main, ctx);
    window_ = w;
    return true;
}

void LowerWindow::present(Texture2D tex) {
    if (!window_) return;
    SDL_Window* main = (SDL_Window*)GetWindowHandle();
    SDL_GLContext ctx = SDL_GL_GetCurrentContext();
    rlDrawRenderBatchActive();
    SDL_GL_MakeCurrent((SDL_Window*)window_, ctx);
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize((SDL_Window*)window_, &w, &h);
    rlViewport(0, 0, w, h);
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlOrtho(0, w, h, 0, 0, 1);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlClearColor(0, 0, 0, 255);
    rlClearScreenBuffers();
    DrawTexturePro(tex, {0, 0, (float)tex.width, -(float)tex.height}, {0, 0, (float)w, (float)h}, {0, 0}, 0, WHITE);
    rlDrawRenderBatchActive();
    SDL_GL_SwapWindow((SDL_Window*)window_);
    SDL_GL_MakeCurrent(main, ctx);
    rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());
}

void LowerWindow::close() {
    if (window_) SDL_DestroyWindow((SDL_Window*)window_);
    window_ = nullptr;
}

}  // namespace rr

#else

namespace rr {
bool LowerWindow::open(int, int) { return false; }
void LowerWindow::present(Texture2D) {}
void LowerWindow::close() {}
}  // namespace rr

#endif
