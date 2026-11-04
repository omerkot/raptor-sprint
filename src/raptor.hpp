// The whole raptor (a race only ever shows its snout), seen on the title screen and at the end of
// a trail. It follows a script of cues: looking round, at the camera, turning, walking, running,
// sniffing, leaping (onto a rock, if asked), calling, shaking itself off, drinking. Its skin is a
// tube along the spine plus tubes for the skull, jaw, legs, arms, toes and claws, rebuilt each
// frame from a small skeleton and laid on one painted map; the tail is a chain of joints that lag
// behind and swing through.
#pragma once
#include <functional>

#include "core.hpp"

namespace rr {

enum class RAct { Idle, Look, Turn, Walk, Run, Sniff, Leap, Call, Shake, Drink };

// One step of a script. Positions are on the stage, in the raptor's own units (RaptorShow::kScale).
struct Cue {
    RAct act;
    Vector2 spot = {0, 0};  // where to face (Turn) or go (Walk, Run, Leap)
    float len = 0;          // seconds (0: the usual; a Run's sets its speed)
    float rise = 0;         // Leap: how far above the ground it lands (on top of a rock)
};

// What happened in the last update, for sounds and splashes.
enum : unsigned { RV_STEP = 1, RV_TAKEOFF = 2, RV_LAND = 4, RV_CALL = 8, RV_SHAKE = 16, RV_LAP = 32 };

class RaptorShow {
public:
    // The stage's frame: x right, y up, z towards where the camera starts.
    static constexpr float kScale = 0.85f;  // modelled at this size relative to the stage

    // Plays the cues in order, then from `loop_from` round again. Without a script it plays
    // the title screen's routine.
    void script(std::vector<Cue> cues, int loop_from = 0);
    // Puts it somewhere, facing `yaw` (0: towards +z).
    void place(Vector2 at, float yaw);
    // The ground's height on the stage (none: flat at 0).
    std::function<float(float, float)> ground;

    // `cam` is the camera's position on the stage.
    void update(float dt, Vector3 cam);
    void draw(const Matrix& stage);
    void unload();

    unsigned events = 0;
    RAct act() const { return act_; }
    float act_time() const { return act_t_; }
    // Points on it, on the stage (as of the last draw).
    Vector3 head() const { return head_; }
    Vector3 hip() const { return hip_; }
    Vector3 chest() const { return chest_; }
    Vector2 pos() const { return pos_; }

private:
    struct Foot {
        Vector3 at = {0, 0, 0}, from = {0, 0, 0};  // on the stage (planted) and where a step began
        float swing = -1;                            // 0..1 while stepping, -1 planted
    };
    void begin(int step);
    void build_mesh();
    float ground_at(float x, float z) const { return ground ? ground(x, z) : 0.0f; }

    std::vector<Cue> cues_;
    int loop_from_ = 0;
    float t_ = 0, act_t_ = 0, act_len_ = 1, leap_air_ = 0.62f;
    int step_ = -1;
    RAct act_ = RAct::Idle;
    Vector2 pos_ = {-0.2f, 0}, from_ = {0, 0}, to_ = {0, 0};
    float yaw_ = 0.6f, yaw_from_ = 0, yaw_to_ = 0, yaw_rate_ = 0, turn_rate_ = 0;
    float base_ = 0, perch_ = 0, perch_from_ = 0, rise_ = 0;  // standing height; how high it's perched
    Vector3 cam_ = {0, 2, 8};
    // The pose, eased towards each act's targets.
    float crouch_ = 0, lift_ = 0, pitch_ = 0.04f, head_yaw_ = 0, head_pitch_ = 0, neck_low_ = 0, jaw_ = 0, tail_lift_ = 0;
    float bend_ = 0, hip_x_ = 0, puff_ = 0, vy_ = 0, last_y_ = 0;
    float tail_yaw_[5] = {}, tail_pitch_[5] = {};
    float look_yaw_ = 0, look_pitch_ = 0, look_next_ = 0;  // where it's glancing (changes in jerks, like a bird)
    float bob_ = 0, airborne_ = 0, run_phase_ = 0, run_speed_ = 7, lap_next_ = 0;
    bool called_ = false;
    Foot feet_[2];
    bool started_ = false;
    Vector3 head_ = {0, 0, 0}, hip_ = {0, 0, 0}, chest_ = {0, 0, 0};

    Mesh mesh_{};
    std::vector<float> pos_buf_, nrm_buf_, uv_buf_;
    std::vector<unsigned char> col_buf_;
    std::vector<unsigned short> idx_buf_;
    Mesh shadow_{};
    Material shadow_mat_{};
};

}  // namespace rr
