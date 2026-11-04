// Headless checks on generated courses: the trail never crosses itself, nothing solid stands on
// it except the obstacles, and the autopilot runs every course to the finish clearing every
// obstacle (so each one, and each sequence of them, can be cleared).
//   course_test journey [difficulty]          all 24 journey trails (default: every difficulty)
//   course_test [first_seed] [count] [metres] [region] [difficulty]   generated trails
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../src/player.hpp"
#include "../src/regions.hpp"

using namespace rr;

static int failures = 0;

static void run(int seed, float metres, int reg, const char* name, int diff) {
    Course c;
    c.generate(seed, metres, reg, diff);
    Course easy;
    easy.generate(seed, metres, reg, 0);
    int kinds[12] = {};
    for (const Obstacle& o : c.obstacles) kinds[(int)o.kind]++;
    float crowd = (float)c.obstacles.size() / easy.obstacles.size();
    printf("%-18s %-6s %5.0f m: %3d obstacles (x%.2f) log %d br %d rock %d crk %d mud %d fall %d | bram %d jam %d gate %d roll %d sink %d ribs %d", name, difficulty_name(diff), metres,
           (int)c.obstacles.size(), crowd, kinds[0], kinds[1], kinds[2], kinds[3], kinds[4], kinds[5], kinds[6], kinds[7], kinds[8], kinds[9], kinds[10], kinds[11]);
    bool ok = true;
    if (std::fabs(crowd - difficulty_crowd(diff)) > 0.04f) printf("\n  FAIL: wanted x%.2f the obstacles", difficulty_crowd(diff)), ok = false;
    int close = -1;
    for (int i = 0; i < (int)c.pts.size() && close < 0; i += 3)
        for (int j = i + 60; j < (int)c.pts.size(); j += 3) {
            float dx = c.pts[i].p.x - c.pts[j].p.x, dz = c.pts[i].p.z - c.pts[j].p.z;
            if (dx * dx + dz * dz < 12.0f * 12.0f) { close = i; break; }
        }
    if (close >= 0) printf("\n  FAIL: trail comes back near itself at %d m", close), ok = false;
    int on_trail = 0;
    for (const Tree& t : c.trees) {
        Nearest n = c.nearest_global(t.p);
        if (n.dist < n.half + t.r * 0.5f) on_trail++;
    }
    if (on_trail) printf("\n  FAIL: %d trees stand on the trail", on_trail), ok = false;
    Player p;
    p.tune = tuning_for(diff);
    p.reset(c);
    float t = 0;
    while (!p.finished && t < metres / 4.0f) {
        p.step(1.0f / 60.0f, autopilot(p, c), c, true);
        t += 1.0f / 60.0f;
        if (p.events & EV_HIT) {
            printf("\n  hit %s at %.1f m (side %.1f, speed %.1f, breath %.2f, up %.2f, duck %.2f, t %.1f) near:", p.last_hit_what, p.near.s, p.near.side, p.speed, p.breath, p.pos.y - p.near.y, p.duck, p.time);
            static const char* kn[] = {"log", "branch", "rock", "creek", "mud", "faller", "bramble", "jam", "gate", "roller", "sinkhole", "ribs"};
            for (const Obstacle& o : c.obstacles)
                if (std::fabs(o.idx - p.near.s) < 30) printf(" %s@%d", kn[(int)o.kind], o.idx);
        }
    }
    if (!p.finished) printf("\n  FAIL: did not finish (stopped at %.0f m)", p.near.s), ok = false;
    if (p.hits) printf("\n  FAIL: %d stumbles", p.hits), ok = false;
    printf("%s  %.1f s\n", ok ? "" : "\n ", p.time);
    if (!ok) failures++;
}

int main(int argc, char** argv) {
    if (argc > 1 && strcmp(argv[1], "journey") == 0) {
        int only = argc > 2 ? atoi(argv[2]) : -1;
        for (int d = 0; d < kDifficulties; d++)
            for (int r = 0; r < kRegions && (only < 0 || only == d); r++)
                for (int k = 0; k < kLevelsPerRegion; k++)
                    run(level_seed(r, k), level_length(r, k), r, TextFormat("%s %d", region(r).name, k + 1), d);
    } else {
        int first = argc > 1 ? atoi(argv[1]) : 1, count = argc > 2 ? atoi(argv[2]) : 8;
        float metres = argc > 3 ? (float)atof(argv[3]) : 1600.0f;
        int reg = argc > 4 ? atoi(argv[4]) : 0;
        int only = argc > 5 ? atoi(argv[5]) : -1;
        for (int d = 0; d < kDifficulties; d++)
            for (int seed = first; seed < first + count && (only < 0 || only == d); seed++) run(seed, metres, reg, "generated", d);
    }
    printf(failures ? "%d FAILURES\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
