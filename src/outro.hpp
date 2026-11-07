// The end of a trail. The view leaves the raptor's eyes and finds it running on past the finish,
// then doing something that belongs to the region: calling back down the trail in Fern Hollow,
// drinking at a pool in the Riverlands, shaking off leaves in the Thicket, leaping onto a boulder
// on Granite Rise, shaking off mud in the Mire, outrunning a dead tree as it crashes down in
// Deadwood. Each trail of a region films it its own way: a chase, an orbit, from further up the
// trail, or a rising crane.
#pragma once
#include "course.hpp"
#include "player.hpp"
#include "raptor.hpp"

namespace rr {

class Outro {
public:
    void start(const Course& c, const Player& p, int region, int variant);
    void stop();
    void update(float dt);  // (plays its own sounds)
    // The view: from `eyes` (first person) out to the shot. `beside_card` is the screen's shape when
    // the finish card is on the same screen (0 when it isn't): the raptor stands to the right of it.
    Camera3D camera(const Camera3D& eyes, float beside_card) const;
    void draw(const Camera3D& cam);  // inside the 3D view, after the world
    void draw_casters();  // what casts a shadow (full only)
    void draw_dust(const Camera3D& cam, Rectangle view);  // over the finished view, before the HUD

    bool active = false;
    float t = 0;

private:
    Vector3 to_world(Vector3 s) const { return Vector3Transform(s, to_world_); }
    Vector3 to_stage(Vector3 w) const { return Vector3Transform(w, to_stage_); }
    Vector3 trail(float d, float side) const;  // a point d metres past the finish, `side` right of the centre
    Vector2 spot(float d, float side = 0) const;
    void spray(int kind, Vector3 from, float spread, int n);
    void aim(float dt, bool snap);  // eases the camera towards this trail's shot

    const Course* course_ = nullptr;
    RaptorShow raptor_;
    Matrix frame_{}, to_world_{}, to_stage_{};
    Vector3 origin_{};
    int region_ = 0, variant_ = 0, fin_ = 0;
    Vector3 cam_pos_{}, cam_look_{}, focus_{};
    float shake_ = 0;
    // Props.
    Mesh boulder_{}, tree_{}, pool_{}, bank_{};
    Vector3 boulder_at_{}, pool_at_{}, tree_base_{}, tree_axis_{};
    float pool_r_ = 1.3f, tree_lift_ = 0, tree_len_ = 11, tree_t0_ = 0.85f, spray_acc_ = 0;
    bool creaked_ = false, crashed_ = false;
    // Drops, leaves, mud and dust.
    enum { Drop, Leaf, Mud, Dust };
    struct Bit {
        Vector3 p, v;
        float life, max, size;
        Color c;
        int kind;
        float spin;
    };
    std::vector<Bit> bits_;
    // Drops and mud are little lit blobs, leaves lit cards, all in two meshes rebuilt each frame
    // (so they share the scene's light and fog); dust is soft puffs that fade into the haze.
    void build_bits();
    // Each is a ring of meshes, one per frame in turn: a handheld's GPU is still drawing the last
    // frame while this one is built, and what it's reading mustn't be written over.
    static constexpr int kMaxBlobs = 320, kMaxLeaves = 160, kRing = 3;
    Mesh blobs_[kRing] = {}, leaves_[kRing] = {};
    int turn_ = 0;
    Texture2D puff_{};
    std::vector<float> bp_, bn_, lp_, ln_;
    std::vector<unsigned char> bc_, lc_;
};

}  // namespace rr
