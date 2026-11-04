// Player settings, saved as key=value lines next to the game.
#pragma once
#include "core.hpp"

namespace rr {

struct Settings {
    int fx = 8, forest = 6;      // volumes, 0..10
    bool sprint_toggle = false;  // R toggles sprint instead of holding it
    int bob = 2;                 // head bob: 0 off, 1 reduced, 2 full
    bool show_fps = false;
    int detail = -1;             // view resolution: 0 high, 1 medium, 2 low (-1: pick for the device)
    int wild_region = 0;         // which region's obstacles a wild trail uses
    int length = 1;              // wild trails: 0 short, 1 standard, 2 long
    int difficulty = 0;          // 0 easy, 1 normal, 2 hard

    void load(const std::string& path);
    void save(const std::string& path) const;
};

// Wild trails: generated from a new random number each run, in any region already reached.
float length_metres(int i);
inline float detail_scale(int d) { return d == 2 ? 0.62f : d == 1 ? 0.8f : 1.0f; }
const char* length_name(int i);

}  // namespace rr
