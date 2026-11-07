// Player settings, saved as key=value lines next to the game.
#pragma once
#include "core.hpp"

namespace rr {

struct Settings {
    int fx = 8, forest = 6;      // volumes, 0..10
    bool sprint_toggle = false;  // R toggles sprint instead of holding it
    bool swap_ab = false;        // for a device where A and B come out the wrong way round
    int bob = 2;                 // head bob: 0 off, 1 reduced, 2 full
    bool show_fps = false;
    int detail = -1;             // view resolution: 0 high, 1 medium, 2 low (-1: pick for the device)
    bool fullscreen = true;      // the Mac: the whole screen, or a window
    int wild_region = 0;         // which region's obstacles a wild trail uses
    int length = 1;              // wild trails: 0 short, 1 standard, 2 long
    int difficulty = 0;          // 0 easy, 1 normal, 2 hard
#if defined(__EMSCRIPTEN__)
    // The touch stick. (Screen::Steering; the defaults are in reset_steering.)
    int steer_speed, steer_travel, steer_curve, steer_dead, steer_smooth;
    Settings() { reset_steering(); }
    void reset_steering() { steer_speed = 120, steer_travel = 40, steer_curve = 2, steer_dead = 6, steer_smooth = 0; }
#endif

    void load(const std::string& path);
    void save(const std::string& path) const;
};

// Wild trails: generated from a new random number each run, in any region already reached.
float length_metres(int i);
inline float detail_scale(int d) { return d == 2 ? 0.62f : d == 1 ? 0.8f : 1.0f; }
inline float mac_detail_scale(int d) { return d == 2 ? 0.55f : d == 1 ? 0.72f : 1.0f; }  // (of a high-density screen's pixels)
const char* length_name(int i);

}  // namespace rr
