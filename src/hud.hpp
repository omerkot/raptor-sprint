// The lower screen (the blind map of the course, the stretch ahead, time, speed and breath) and
// the few things drawn over the view on the upper screen: the countdown (with what's new in a
// region), stumble flashes, clean clears, and the finish card with its claw marks.
#pragma once
#include "player.hpp"

namespace rr {

enum class Phase { Countdown, Race, Finished };

struct Best {
    float total = 0;  // 0: no time yet
    std::vector<float> splits;
    void load(const std::string& path);
    void save(const std::string& path) const;
};

struct HudState {
    Phase phase = Phase::Countdown;
    float countdown = 3;
    float flash = 0;  // red edge after a stumble
    float boost = 0;  // green edge after a run of clean moves wins back some breath
    bool new_best = false;
    bool go = false;    // the countdown just ended: show RUN
    std::string trail;  // the trail's name, over the map
    // The countdown card: a heading and (on a region's first trail) what's new.
    std::string intro_title;
    std::vector<std::string> intro_lines;
    bool hold = false;   // what's new stays up until A; the countdown starts after that
    float held = 0;      // how long it has been up
    // The finish card.
    float par = 0;    // the target lap (0: none)
    int claws = 0;
    std::string news; // "Trail 3 is open", ...
    std::string hint = "A  run again      B  menu";
    // Obstacles cleared clean: a word that pops up and fades, the run of them, and the count.
    float reward = 0;
    std::string reward_word;
    int streak = 0, best_streak = 0, clean = 0, passed = 0;
};

// The lower screen, drawn into rect r (designed at 640x480, scaled to fit).
void draw_lower(Rectangle r, const Course& c, const Player& p, const Best& best, const HudState& h);
// Over the view. `compact` adds the time and a small map of the stretch ahead, for one screen.
void draw_upper_overlay(Rectangle r, const Course& c, const Player& p, const Best& best, const HudState& h, bool compact);
// The finish card, fading in with `alpha`: across the middle of `r`, or (`left`) down its left
// side, leaving the right for the view.
void draw_finish_card(Rectangle r, const Player& p, const Best& best, const HudState& h, float alpha, bool left);
// Claw marks: `got` of 3 lit, at x, y, `size` tall.
void draw_claws(float x, float y, float size, int got);

std::string format_time(float t);

}  // namespace rr
