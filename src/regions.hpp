// The journey: six regions of four trails. Each region has its own light and colour, and brings
// in new obstacles that ask for quicker, tighter button sequences.
#pragma once
#include "core.hpp"

namespace rr {

constexpr int kRegions = 6;
constexpr int kLevelsPerRegion = 4;

struct Biome {
    Vector3 fog;
    float fog_density;
    Vector3 sun_col, to_sun, sky_amb, ground_amb;
    Color ground, leaves, bark;  // tints
};

struct Region {
    const char* name;
    const char* blurb;
    const char* news[3];  // what's new here (shown on the first trail's countdown)
    Biome biome;
};

const Region& region(int i);
int level_seed(int r, int k);
float level_length(int r, int k);

}  // namespace rr
