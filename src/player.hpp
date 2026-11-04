// The raptor: running, steering, jumping, ducking, stumbling, and the race clock. Pure logic.
#pragma once
#include "course.hpp"

namespace rr {

struct Controls {
    float steer = 0;  // -1 left .. 1 right
    bool brake = false, jump = false, duck = false, sprint = false;
};

struct Tuning {
    float run_speed = 15.0f;     // m/s on the trail (54 km/h)
    float sprint_speed = 20.5f;  // while breath lasts
    float rough_speed = 7.5f;    // in the undergrowth off the trail
    float accel = 7.0f, brake = 22.0f;
    float turn_rate = 1.9f;      // rad/s at low speed, a little less when fast
    float jump_speed = 6.7f, gravity = 19.0f;
    float eye_stand = 1.55f, eye_duck = 0.85f;
    float body_r = 0.35f;
    float breath_use = 0.30f, breath_regain = 0.21f;  // per second
    float pace = 1.0f;           // the difficulty: 1 is easy (see tuning_for)
};

// The whole race runs `pace` times as fast: speeds, leaps and turns, so a leap still covers the
// same ground, in less time. (Breath is the same at every difficulty.)
Tuning tuning_for(int difficulty);

// What happened during the last step, for sound.
enum Event : unsigned {
    EV_STEP = 1, EV_JUMP = 2, EV_LAND = 4, EV_DUCK = 8, EV_HIT = 16, EV_CHECKPOINT = 32, EV_FINISH = 64,
    EV_SPLASH = 128, EV_CREAK = 256, EV_CRASH = 512, EV_RUMBLE = 1024,
};

struct Player {
    Tuning tune;
    Vector3 pos = {0, 0, 0};  // feet
    float yaw = 0, speed = 0, vy = 0;
    bool grounded = true;
    float duck = 0;      // 0 standing .. 1 crouched
    float breath = 1;    // 0..1
    bool winded = false; // ran out: no sprint until breath recovers a little
    float stumble = 0;   // seconds left of a stumble
    float shake = 0;     // camera shake, decays
    float stride = 0;    // stride phase (radians)
    float land = 0;      // landing dip, decays
    float steer_vis = 0; // smoothed steering (head roll)
    int hint = 0;        // nearest trail sample
    Nearest near;
    int hits = 0;
    int next_check = 0;  // index into course.checkpoints
    float time = 0;      // race time
    std::vector<float> splits;
    bool finished = false;
    int last_hit = -1;   // obstacle index of the last stumble (one stumble per obstacle)
    const char* last_hit_what = "";
    unsigned events = 0;  // Event bits from the last step
    bool rough = false;   // in the undergrowth (footsteps rustle)
    bool in_mud = false, in_water = false;
    float clock = 0;          // seconds since the reset (drives falling trees)
    std::vector<float> fall_start;  // per obstacle: when a falling tree began to fall (-1: standing)
    std::vector<float> roll_speed;  // per obstacle: how fast a boulder was sent off to meet the raptor
    std::vector<float> fall_time;   // and how long a falling tree was given to come down on it
    // How far a falling tree has fallen: 0 standing .. pi/2 lying across the trail.
    float fall_angle(int obstacle) const;
    // How far a rolling boulder has rolled: 0 waiting .. 1 at rest beyond the trail.
    float roll(const Obstacle& o, int obstacle) const;

    void reset(const Course& c);
    float eye_height() const { return lerpf(tune.eye_stand, tune.eye_duck, duck); }
    // One step. `racing` false freezes the clock and holds the raptor (countdown, results).
    void step(float dt, const Controls& in, const Course& c, bool racing);
};

// Steers along the trail, jumps logs, ducks branches and swerves round rocks (screenshots, tests).
Controls autopilot(const Player& p, const Course& c);

}  // namespace rr
