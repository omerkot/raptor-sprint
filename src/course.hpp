// The course: a winding trail through forest, sampled every metre, with the ground around it and
// everything placed along it. Pure logic (no GPU), so tests and the autoplay can use it.
#pragma once
#include "core.hpp"

namespace rr {

struct PathPt {
    Vector3 p;   // centre of the trail, on the ground
    float yaw;   // heading: 0 is -Z, positive turns right (towards +X)
    float s;     // distance from the start
    float half;  // half the trail's width
};

inline Vector3 heading_dir(float yaw) { return {std::sin(yaw), 0.0f, -std::cos(yaw)}; }
inline Vector3 heading_right(float yaw) { return {std::cos(yaw), 0.0f, std::sin(yaw)}; }

constexpr int kRegionsMax = 6;

// How hard a race is: 0 easy, 1 normal, 2 hard. Pace speeds up the whole race (running, jumping,
// falling trees), so every gap stays the same distance and just arrives sooner; crowd is how many
// obstacles a trail has, against easy.
constexpr int kDifficulties = 3;
inline float difficulty_pace(int d) { return d == 2 ? 1.2f : d == 1 ? 1.1f : 1.0f; }
inline float difficulty_crowd(int d) { return d == 2 ? 1.5f : d == 1 ? 1.2f : 1.0f; }
inline const char* difficulty_name(int d) { return d == 2 ? "Hard" : d == 1 ? "Normal" : "Easy"; }

enum class ObKind { Log, Branch, Rock, Creek, Mud, Faller, Bramble, Jam, Gate, Roller, Sinkhole, Ribs };

// Logs lie across the trail (jump them; `gap` above 0 makes one the great bone after a ribcage), branches hang across at head height (duck under),
// rocks block part of it (go round). Logs and branches are cylinders along `axis`.
// Creeks and mud are bands across the trail, `r` metres either side of `c` along it, `len` wide:
// a creek must be jumped clean, mud jumped or waded slowly. A faller is a dead tree at the
// trail's edge (base `c`) that falls across it along `axis` (trunk radius `r`, length `len`)
// when the raptor comes within `trigger` metres.
// One new kind for each region:
//   Bramble   a low thorn bank across part of the trail, `r` either side of `c`, `len` wide: jump
//             it or run round it.
//   Jam       a wall of driftwood too tall to jump, `len` wide along `axis`, `r` thick, open
//             `gap_w` either side of `gap` metres along it.
//   Gate      roots across the trail with more arching `gap` over them: jump and duck together.
//   Roller    a boulder (radius `r`) that rolls `len` metres from `c` along `axis`, across the
//             trail, when the raptor comes within `trigger` metres, timed to meet it there at
//             the speed it's going: seen ahead all the way, it can't just be run past.
//   Sinkhole  a pit (`r` is its pull, `len` its core) that drags the raptor in: steer away.
//   Ribs      a huge dead animal's ribcage along the trail, centred on `c`, `len` long along
//             `axis`, its lane `r` either side of the spine: stay central, and duck. A great
//             leg bone lies across the trail just past it, to be leapt.
struct Obstacle {
    ObKind kind;
    Vector3 c;
    Vector3 axis;  // unit, horizontal (logs and branches)
    float r, len;
    int idx;  // nearest trail sample
    float trigger = 0;
    float gap = 0, gap_w = 0;
};

constexpr int kRollerSight = 28;
constexpr float kGateBough = 2.3f;  // height of the underside of a gate's bough
constexpr float kRibsHeadroom = 1.25f;
constexpr float kBrambleTop = 0.62f, kJamTop = 1.6f;

struct Tree {
    Vector3 p;  // base of the trunk
    float r, h;
    int idx;
};

enum class PlantKind { Fern, Grass, Bush };
struct Plant {
    PlantKind kind;
    Vector3 p;
    float size, yaw;
    int idx;
};

struct Nearest {
    int i = 0;        // trail sample at the start of the nearest segment
    float t = 0;      // 0..1 along that segment
    float dist = 0;   // horizontal distance from the trail's centre line
    float side = 0;   // signed: + to the right of the trail
    float y = 0;      // trail height there
    float half = 2;   // trail half-width there
    float s = 0;      // distance along the course
};

struct Course {
    std::vector<PathPt> pts;
    std::vector<Obstacle> obstacles;
    std::vector<Tree> trees;
    std::vector<Plant> plants;
    std::vector<int> checkpoints;  // trail sample indices, the last one is the finish (at `length`)
    float length = 0;
    int seed = 1;
    int region = 0;  // which obstacles appear (regions.hpp)
    int difficulty = 0;

    void generate(int seed, float length, int region = 0, int difficulty = 0);
    // Where a rolling boulder has got to, `t` 0..1 of the way along its path (its centre).
    // kRollerSight is how far ahead along the trail it starts to roll.
    Vector3 roller_at(const Obstacle& o, float t) const;
    float raw_height(float x, float z) const;
    // Nearest point on the trail, searching segments within `window` samples of `hint`.
    Nearest nearest(Vector3 p, int hint, int window = 30) const;
    // The ground: flat along the trail, blending into the hills beside it.
    float ground(float x, float z, int hint) const;
    float ground(float x, float z, const Nearest& n) const;
    // Slow global search (generation and tests).
    Nearest nearest_global(Vector3 p) const;
};

}  // namespace rr
