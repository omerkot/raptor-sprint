// Sound, all synthesised at startup: footsteps, jumps, hits, the raptor's yelp and breath, chimes
// and menu clicks; plus the forest itself (wind, leaves, birds, insects, the rush of speed),
// generated live on the audio thread so it never loops.
#pragma once
#include "core.hpp"

namespace rr {

enum class Sfx {
    StepDirt, StepLeaf, Jump, Land, Duck, HitLog, HitRock, HitBranch, HitTree, Yelp, Breath,
    Checkpoint, Finish, Count, Go, MenuMove, MenuOk, MenuBack, Splash, Squelch, Creak, Crash,
    Clear, Call, Shake, Recover, Rumble, Count_
};

struct Audio {
    bool ok = false;
    float fx_volume = 0.8f, forest_volume = 0.6f;

    void init();
    void play(Sfx s, float volume = 1.0f, float pitch = 1.0f);
    // The forest: how fast the raptor is going (rush of air), and whether it plays at all.
    void set_forest(float speed, bool on);
    void unload();
    // Every effect as a WAV, plus the forest standing still and at a sprint (no audio device needed).
    static void dump(const std::string& dir);
};

extern Audio audio;

}  // namespace rr
