// The course turned into meshes, in chunks along the trail; only chunks near the camera are drawn
// (the fog hides the rest). Plus the raptor's snout, drawn in head space.
#pragma once
#include "course.hpp"
#include "gfx.hpp"

namespace rr {

struct Chunk {
    Vector3 center = {0, 0, 0};
    float radius = 0;    // everything in it (trees stand up to 34 m from the trail)
    float half_len = 0;  // half its length along the trail
    std::vector<Vector2> spots;  // where its trees and plants stand (x, z)
    enum Layer { Terrain, Trail, Bark, Rock, Fern, Leaves, Grass, Water, LayerCount };
    Mesh mesh[LayerCount] = {};
    bool has[LayerCount] = {};
};

// A dead tree that falls across the trail: built standing at the origin, posed at draw time.
struct Faller {
    int obstacle;
    Mesh trunk;
    Vector3 base, axis;
    float lift;  // how far the trunk's centre sits above the base once down
};

// A boulder that rolls across the trail: built at the origin, posed at draw time.
struct Roller {
    int obstacle;
    Mesh rock;
};

// How far each kind of thing is drawn (the Detail setting): the ground, trunks and obstacles
// out to `far`; ferns and grass (cut-out, the costliest to draw) out to `plants`; leaves out to
// `canopy`. Everything sinks into the fog before `far`, so nothing is seen to start.
struct DrawRange {
    float far = 85, plants = 85, canopy = 85;
};

// Props for the end of a trail: a boulder resting on the ground at the origin (its top about
// 1.25 r up), and a dead tree standing at the origin (posed as the fallers are).
Mesh make_boulder(float r);
Mesh make_dead_tree(float r, float len, bool low);

struct World {
    std::vector<Chunk> chunks;
    std::vector<Faller> fallers;
    std::vector<Roller> rollers;
    const Course* course = nullptr;
    Mesh snout{}, teeth{};
    int drawn = 0, triangles = 0;  // last frame's counts (diagnostics)

    void build(const Course& c, bool low);
    // `fall` gives each obstacle's movement (a faller's angle, a roller's progress); `tint` the
    // region's colours.
    void draw(const Camera3D& cam, const DrawRange& range, const std::vector<float>& fall, const Biome& tint);
    // The snout, attached to the head (basis: right, up, back, at the eye).
    void draw_snout(const Matrix& head);
    void unload();
};

}  // namespace rr
