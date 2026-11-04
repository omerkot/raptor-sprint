// The journey's progress: best time and claw marks (1-3) per trail and difficulty, saved next to
// the game. Finishing a trail, at any difficulty, opens the next; finishing all four of a region
// opens the next region.
// Claws come from the time against the trail's target, a clean computer-run lap of it.
#pragma once
#include "course.hpp"
#include "player.hpp"
#include "regions.hpp"

namespace rr {

struct Progress {
    float best_at[kDifficulties][kRegions][kLevelsPerRegion] = {};
    int claws_at[kDifficulties][kRegions][kLevelsPerRegion] = {};
    int difficulty = 0;  // the record in use: the rest of this is about that one

    float& best(int r, int k) { return best_at[difficulty][r][k]; }
    int& claws(int r, int k) { return claws_at[difficulty][r][k]; }
    bool done(int r, int k) const;  // finished at any difficulty
    // journey.txt holds easy; the others sit beside it (journey_normal.txt, journey_hard.txt).
    void load(const std::string& path);
    void save(const std::string& path) const;
    bool region_open(int r) const;
    bool level_open(int r, int k) const;
    int region_claws(int r) const;
    int total_claws() const;
    // The trail to offer next: the first open one not yet finished (or the last one).
    void next_up(int* r, int* k) const;
};

float par_time(const Course& c, const Tuning& tune);  // the autopilot's time over the course
float claw_time(float par, int claws);     // time needed for 2 or 3 claws
int claws_for(float time, float par);      // 1 for finishing .. 3 for matching the target

}  // namespace rr
