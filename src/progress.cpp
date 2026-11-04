#include "progress.hpp"

#include <fstream>

#include "player.hpp"

namespace rr {

static std::string record_path(const std::string& path, int d) {
    if (d == 0) return path;
    size_t dot = path.rfind('.');
    return path.substr(0, dot) + (d == 1 ? "_normal" : "_hard") + path.substr(dot);
}

void Progress::load(const std::string& path) {
    for (int d = 0; d < kDifficulties; d++) {
        std::ifstream in(record_path(path, d));
        int r, k, c;
        float t;
        while (in >> r >> k >> t >> c)
            if (r >= 0 && r < kRegions && k >= 0 && k < kLevelsPerRegion) best_at[d][r][k] = t, claws_at[d][r][k] = (int)clampf(c, 0, 3);
    }
}

void Progress::save(const std::string& path) const {
    std::ofstream out(record_path(path, difficulty));
    for (int r = 0; r < kRegions; r++)
        for (int k = 0; k < kLevelsPerRegion; k++)
            if (best_at[difficulty][r][k] > 0) out << r << ' ' << k << ' ' << best_at[difficulty][r][k] << ' ' << claws_at[difficulty][r][k] << '\n';
}

bool Progress::done(int r, int k) const {
    for (int d = 0; d < kDifficulties; d++)
        if (best_at[d][r][k] > 0) return true;
    return false;
}

bool Progress::region_open(int r) const {
    if (r <= 0) return true;
    if (r >= kRegions) return false;
    for (int k = 0; k < kLevelsPerRegion; k++)
        if (!done(r - 1, k)) return false;
    return true;
}

bool Progress::level_open(int r, int k) const { return region_open(r) && (k == 0 || done(r, k - 1)); }

int Progress::region_claws(int r) const {
    int n = 0;
    for (int k = 0; k < kLevelsPerRegion; k++) n += claws_at[difficulty][r][k];
    return n;
}

int Progress::total_claws() const {
    int n = 0;
    for (int r = 0; r < kRegions; r++) n += region_claws(r);
    return n;
}

void Progress::next_up(int* r, int* k) const {
    for (int a = 0; a < kRegions; a++)
        for (int b = 0; b < kLevelsPerRegion; b++)
            if (level_open(a, b) && !done(a, b)) {
                *r = a, *k = b;
                return;
            }
    *r = kRegions - 1, *k = kLevelsPerRegion - 1;
}

float par_time(const Course& c, const Tuning& tune) {
    Player p;
    p.tune = tune;
    p.reset(c);
    float t = 0, limit = c.length / 4.0f;
    while (!p.finished && t < limit) {
        p.step(1.0f / 60.0f, autopilot(p, c), c, true);
        t += 1.0f / 60.0f;
    }
    return p.finished ? p.time : c.length / 14.0f;
}

float claw_time(float par, int claws) { return claws >= 3 ? par * 1.03f : par * 1.12f; }

int claws_for(float time, float par) {
    if (time <= claw_time(par, 3)) return 3;
    if (time <= claw_time(par, 2)) return 2;
    return 1;
}

}  // namespace rr
