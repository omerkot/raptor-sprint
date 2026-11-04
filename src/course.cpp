#include "course.hpp"

#include "noise.hpp"

namespace rr {

namespace {
struct Rng {
    uint32_t s;
    float operator()(float lo = 0, float hi = 1) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return lo + (hi - lo) * ((s & 0xffffff) / 16777215.0f);
    }
};
}  // namespace

float Course::raw_height(float x, float z) const {
    return 10.0f * fbm(x * 0.007f + 13.1f, z * 0.007f + 7.7f, 4, seed) + 1.6f * fbm(x * 0.05f, z * 0.05f, 3, seed + 9) - 6.0f;
}

Nearest Course::nearest(Vector3 p, int hint, int window) const {
    Nearest best;
    best.dist = 1e9f;
    int n = (int)pts.size();
    int lo = std::max(0, hint - window), hi = std::min(n - 2, hint + window);
    for (int i = lo; i <= hi; i++) {
        Vector3 a = pts[i].p, b = pts[i + 1].p;
        float ex = b.x - a.x, ez = b.z - a.z;
        float t = clampf(((p.x - a.x) * ex + (p.z - a.z) * ez) / (ex * ex + ez * ez), 0, 1);
        float qx = a.x + ex * t, qz = a.z + ez * t;
        float d = std::sqrt((p.x - qx) * (p.x - qx) + (p.z - qz) * (p.z - qz));
        if (d < best.dist) {
            best.dist = d;
            best.i = i;
            best.t = t;
        }
    }
    const PathPt &a = pts[best.i], &b = pts[best.i + 1];
    best.y = lerpf(a.p.y, b.p.y, best.t);
    best.half = lerpf(a.half, b.half, best.t);
    best.s = lerpf(a.s, b.s, best.t);
    Vector3 r = heading_right(lerp_angle(a.yaw, b.yaw, best.t));
    Vector3 q = Vector3Lerp(a.p, b.p, best.t);
    best.side = (p.x - q.x) * r.x + (p.z - q.z) * r.z;
    return best;
}

Nearest Course::nearest_global(Vector3 p) const {
    int bi = 0;
    float bd = 1e18f;
    for (int i = 0; i < (int)pts.size(); i += 4) {
        float dx = p.x - pts[i].p.x, dz = p.z - pts[i].p.z, d = dx * dx + dz * dz;
        if (d < bd) bd = d, bi = i;
    }
    return nearest(p, bi, 6);
}

float Course::ground(float x, float z, const Nearest& n) const {
    float raw = raw_height(x, z);
    return lerpf(n.y, raw, smooth(n.half + 1.5f, n.half + 9.0f, n.dist));
}

float Course::ground(float x, float z, int hint) const { return ground(x, z, nearest({x, 0, z}, hint)); }

Vector3 Course::roller_at(const Obstacle& o, float t) const {
    Vector3 p = Vector3Add(o.c, Vector3Scale(o.axis, o.len * t));
    p.y = ground(p.x, p.z, o.idx) + o.r * 0.8f;
    return p;
}

void Course::generate(int seed_, float len, int region_, int difficulty_) {
    seed = seed_;
    region = region_;
    difficulty = difficulty_;
    length = len;
    Rng rng{(uint32_t)seed * 2654435761u + 1};
    pts.clear();
    obstacles.clear();
    trees.clear();
    plants.clear();
    checkpoints.clear();

    // The trail: curvature drifts towards random targets; a gentle pull keeps the overall heading
    // roughly forward so the trail never doubles back on itself.
    Vector3 p = {0, 0, 0};
    float yaw = 0, k = 0, k_target = 0, next_change = 45;
    int n = (int)len + 61;  // the trail runs on 60 m past the finish line
    for (int i = 0; i < n; i++) {
        float s = (float)i;
        if (s >= next_change) {
            float sharp = rng() < 0.3f ? 1.0f : 0.5f;
            k_target = rng(-1.0f, 1.0f) / 26.0f * sharp - yaw * 0.012f;
            if (rng() < 0.2f) k_target = 0;
            if (std::fabs(yaw) > 0.85f && k_target * yaw > 0) k_target = -k_target;
            next_change = s + rng(25, 70);
        }
        if (std::fabs(yaw) > 1.0f && k_target * yaw > 0) k_target = -std::fabs(k_target) * (yaw > 0 ? 1.0f : -1.0f);
        k += (k_target - k) * 0.06f;
        float half = 2.0f + 0.5f * (fbm(s * 0.015f, 3.3f, 2, seed + 3) - 0.5f) * 2.0f;
        pts.push_back({p, yaw, s, half});
        yaw += k;
        p = Vector3Add(p, heading_dir(yaw));
    }
    // Trail heights: the hills, smoothed so the trail climbs and dips gently.
    std::vector<float> h(n);
    for (int i = 0; i < n; i++) h[i] = raw_height(pts[i].p.x, pts[i].p.z);
    for (int pass = 0; pass < 3; pass++) {
        std::vector<float> o(n);
        for (int i = 0; i < n; i++) {
            float sum = 0, w = 0;
            for (int j = std::max(0, i - 12); j <= std::min(n - 1, i + 12); j++) sum += h[j], w += 1;
            o[i] = sum / w;
        }
        h = o;
    }
    for (int i = 0; i < n; i++) pts[i].p.y = h[i];

    for (float s = 400; s < len - 1; s += 400) checkpoints.push_back((int)s);
    checkpoints.push_back((int)len);

    auto on_trail = [&](Vector3 q, float margin) {
        Nearest nr = nearest_global(q);
        return nr.dist < nr.half + margin;
    };
    auto ground_at = [&](float x, float z, int hint) { return ground(x, z, hint); };

    // Obstacles, in patterns. Each region brings three new patterns (and weights them heavily),
    // keeps the earlier ones, and spaces everything a little closer. (From their own random
    // numbers, so the forest stays the same whatever the difficulty.)
    Rng orng{(uint32_t)seed * 40503u + 977u};
    auto add_log = [&](int i, float twist_max) {
        const PathPt& q = pts[i];
        Vector3 fwd = heading_dir(q.yaw), right = heading_right(q.yaw);
        Obstacle o{};
        o.kind = ObKind::Log;
        o.idx = i;
        o.axis = Vector3Normalize(Vector3Add(right, Vector3Scale(fwd, orng(-twist_max, twist_max))));
        o.r = orng(0.30f, 0.40f);
        o.len = q.half * 2 + orng(3.0f, 5.0f);
        o.c = Vector3Add(q.p, Vector3Scale(right, orng(-0.6f, 0.6f)));
        // (Uphill, a leap has less height to spare: such a log has sunk a little.)
        o.c.y = q.p.y + o.r * 0.85f - clampf((q.p.y - pts[i - 6].p.y) * 0.6f, 0, 0.25f);
        obstacles.push_back(o);
    };
    auto add_branch = [&](int i) {
        const PathPt& q = pts[i];
        Vector3 fwd = heading_dir(q.yaw), right = heading_right(q.yaw);
        Obstacle o{};
        o.kind = ObKind::Branch;
        o.idx = i;
        o.axis = Vector3Normalize(Vector3Add(right, Vector3Scale(fwd, orng(-0.2f, 0.2f))));
        o.r = orng(0.13f, 0.18f);
        o.len = q.half * 2 + 4.0f;
        o.c = q.p;
        o.c.y = q.p.y + 1.22f + o.r;
        obstacles.push_back(o);
        for (int e = -1; e <= 1; e += 2) {  // held up by a trunk at each end
            Vector3 b = Vector3Add(o.c, Vector3Scale(o.axis, e * o.len * 0.5f));
            b.y = ground(b.x, b.z, i) - 0.2f;
            trees.push_back({b, orng(0.32f, 0.45f), orng(16, 22), i});
        }
    };
    auto add_rock = [&](int i, float side) {
        const PathPt& q = pts[i];
        Obstacle o{};
        o.kind = ObKind::Rock;
        o.idx = i;
        o.r = orng(0.85f, 1.1f);
        o.c = Vector3Add(q.p, Vector3Scale(heading_right(q.yaw), side * q.half * orng(0.35f, 0.55f)));
        o.c.y = q.p.y - o.r * 0.25f;
        obstacles.push_back(o);
    };
    auto add_band = [&](ObKind kind, int i, float half_len) {
        const PathPt& q = pts[i];
        Obstacle o{};
        o.kind = kind;
        o.idx = i;
        o.axis = heading_right(q.yaw);
        o.r = half_len;
        o.len = q.half * 2 + (kind == ObKind::Creek ? 22.0f : 3.0f);
        o.c = q.p;
        obstacles.push_back(o);
    };
    auto add_faller = [&](int i, float side) {
        const PathPt& q = pts[i];
        Vector3 right = heading_right(q.yaw);
        Obstacle o{};
        o.kind = ObKind::Faller;
        o.idx = i;
        o.axis = Vector3Scale(right, -side);  // falls across the trail
        o.r = orng(0.30f, 0.36f);
        o.len = 15.0f;
        o.c = Vector3Add(q.p, Vector3Scale(right, side * (q.half + 1.4f)));
        o.c.y = ground(o.c.x, o.c.z, i) - 0.1f;
        o.trigger = 30.0f;
        obstacles.push_back(o);
    };
    auto add_bramble = [&](int i, float side) {
        const PathPt& q = pts[i];
        Vector3 right = heading_right(q.yaw);
        Obstacle o{};
        o.kind = ObKind::Bramble;
        o.idx = i;
        o.axis = right;
        o.r = orng(1.1f, 1.4f);
        float inner = -0.25f, outer = q.half + 3.5f;  // from just past the middle to well off the trail
        o.len = outer - inner;
        o.c = Vector3Add(q.p, Vector3Scale(right, side * (inner + outer) * 0.5f));
        o.c.y = q.p.y;
        obstacles.push_back(o);
    };
    auto add_jam = [&](int i, float side) {
        const PathPt& q = pts[i];
        Obstacle o{};
        o.kind = ObKind::Jam;
        o.idx = i;
        o.axis = heading_right(q.yaw);
        o.r = 0.7f;
        o.len = q.half * 2 + 14.0f;
        o.gap = side * (q.half - 1.15f);
        o.gap_w = 1.05f;
        o.c = q.p;
        obstacles.push_back(o);
    };
    auto add_gate = [&](int i) {
        add_log(i, 0.05f);
        Obstacle& o = obstacles.back();
        o.kind = ObKind::Gate;
        o.c = Vector3Add(pts[i].p, {0, o.r * 0.85f, 0});
        // A leap starts a little way back: going downhill it's that much higher over the root.
        o.gap = kGateBough + std::max(0.0f, pts[i - 5].p.y - pts[i].p.y);
        Vector3 c = o.c;
        float l = o.len;
        for (int e = -1; e <= 1; e += 2) {  // the two great trees whose roots these are
            Vector3 b = Vector3Add(c, Vector3Scale(o.axis, e * l * 0.5f));
            b.y = ground(b.x, b.z, i) - 0.2f;
            trees.push_back({b, orng(0.62f, 0.8f), orng(20, 26), i});
        }
    };
    auto add_roller = [&](int i, float side) {
        const PathPt& q = pts[i];
        const PathPt& seen_from = pts[i - kRollerSight];
        // It has to be in view from where it starts to roll: no good on a sharp bend, and on a
        // gentle one it comes from the outside, so the two headings don't add up off the screen.
        if (std::fabs(angle_diff(seen_from.yaw, q.yaw)) > 0.6f) {
            add_rock(i, side);
            return;
        }
        Vector3 chord = Vector3Subtract(q.p, seen_from.p);
        float yaw_to = std::atan2(chord.x, -chord.z), off = angle_diff(seen_from.yaw, yaw_to);
        if (std::fabs(off) > 0.04f) side = off > 0 ? -1.0f : 1.0f;
        Vector3 right = heading_right(yaw_to);
        Obstacle o{};
        o.kind = ObKind::Roller;
        o.idx = i;
        o.r = orng(0.95f, 1.15f);
        float from = 14.0f, to = q.half + o.r + 1.6f;  // (half the distance the raptor has to run)
        o.axis = Vector3Scale(right, -side);
        o.c = Vector3Add(q.p, Vector3Scale(right, side * from));
        o.len = from + to;
        o.trigger = (float)kRollerSight;
        obstacles.push_back(o);
    };
    auto add_sinkhole = [&](int i, float side) {
        const PathPt& q = pts[i];
        Obstacle o{};
        o.kind = ObKind::Sinkhole;
        o.idx = i;
        o.r = 3.2f;    // the pull reaches this far
        o.len = 1.15f;  // the pit itself
        o.c = Vector3Add(q.p, Vector3Scale(heading_right(q.yaw), side * 0.7f));
        o.c.y = q.p.y;
        obstacles.push_back(o);
    };
    auto add_ribs = [&](int i) {  // `i`: the near end
        float l = 10.0f;
        const PathPt& q = pts[i + 5];
        Obstacle o{};
        o.kind = ObKind::Ribs;
        o.idx = i + 5;
        o.axis = Vector3Normalize(Vector3Subtract(pts[i + 10].p, pts[i].p));
        o.axis.y = 0;
        o.axis = Vector3Normalize(o.axis);
        o.len = l;
        o.r = 1.45f;
        o.c = Vector3Scale(Vector3Add(pts[i].p, pts[i + 10].p), 0.5f);
        o.c.y = q.p.y;
        obstacles.push_back(o);
    };
    enum { P_LOG, P_BRANCH, P_ROCK, P_BRAMBLE, P_CREEK, P_TWIN, P_JAM, P_LOG_BRANCH, P_BRANCH_ROW, P_GATE, P_SLALOM, P_ROCK_LOG, P_ROLLER,
           P_MUD, P_CREEK_BRANCH, P_SINKHOLE, P_FALLER, P_GAUNTLET, P_RIBS };
    static const int intro[kRegionsMax][3] = {{P_LOG, P_BRANCH, P_BRAMBLE}, {P_CREEK, P_TWIN, P_JAM}, {P_LOG_BRANCH, P_BRANCH_ROW, P_GATE},
                                              {P_SLALOM, P_ROCK_LOG, P_ROLLER}, {P_MUD, P_CREEK_BRANCH, P_SINKHOLE}, {P_FALLER, P_GAUNTLET, P_RIBS}};
    struct Pat { int id; float w; };
    std::vector<Pat> pats = {{P_LOG, 4}, {P_BRANCH, 3}, {P_ROCK, 3}, {P_BRAMBLE, region == 0 ? 3.5f : 3.0f}};
    int reg = (int)clampf(region, 0, kRegionsMax - 1);
    for (int r = 1; r <= reg; r++) {
        for (Pat& p : pats) p.w *= 0.6f;
        pats.push_back({intro[r][0], r == reg ? 4.5f : 2.5f});
        pats.push_back({intro[r][1], r == reg ? 3.5f : 2.0f});
        pats.push_back({intro[r][2], r == reg ? 3.5f : 2.0f});
    }
    float wsum = 0;
    for (const Pat& p : pats) wsum += p.w;
    float gap_lo = 30.0f - 1.5f * reg, gap_hi = 58.0f - 4.0f * reg;
    constexpr float kMinGap = 13.0f;  // from one pattern's end to the next one's start (the tightest twin logs)
    uint32_t orng_start = orng.s;
    // Lays the trail out with its gaps scaled, and says how many obstacles that came to.
    auto lay_out = [&](float gap_scale, float spare) {
      orng.s = orng_start;
      obstacles.clear();
      trees.clear();
      for (float s = 70 - spare; s < len - 90 + spare;) {
        float roll = orng(0, wsum);
        int id = pats.back().id;
        for (const Pat& p : pats) {
            if (roll < p.w) { id = p.id; break; }
            roll -= p.w;
        }
        int i = (int)s;
        float side = orng() < 0.5f ? -1.0f : 1.0f, extent = 0;
        switch (id) {
            case P_LOG: add_log(i, 0.3f); break;
            case P_BRANCH: add_branch(i); break;
            case P_ROCK: add_rock(i, side); break;
            case P_BRAMBLE: add_bramble(i, side); break;
            case P_JAM: add_jam(i + 6, side); extent = 6; break;  // (with room to steer for the gap)
            case P_GATE: add_gate(i); break;
            case P_ROLLER: add_roller(i, side); break;
            case P_SINKHOLE: add_sinkhole(i + 4, side); extent = 4; break;
            case P_RIBS:  // (with a clear run in, and room to stand up again before the bone)
                add_ribs(i + 7);
                add_log(i + 27, 0.08f);
                obstacles.back().gap = 1;
                obstacles.back().len = pts[i + 27].half * 2 + 1.6f;
                extent = 27;
                break;
            case P_CREEK: { float h = orng(2.2f, 2.9f); add_band(ObKind::Creek, i, h); extent = h; break; }
            case P_TWIN: { float d = orng(13.0f, 14.5f); add_log(i, 0.1f); add_log(i + (int)d, 0.1f); extent = d; break; }
            case P_LOG_BRANCH: add_log(i, 0.1f); add_branch(i + 14); extent = 14; break;
            case P_BRANCH_ROW: add_branch(i); add_branch(i + 5); add_branch(i + 10); extent = 10; break;
            case P_SLALOM: add_rock(i, side); add_rock(i + 13, -side); add_rock(i + 26, side); extent = 26; break;
            case P_ROCK_LOG: add_rock(i, side); add_log(i + 12, 0.1f); extent = 12; break;
            case P_MUD: { float h = orng(3.6f, 4.3f); add_band(ObKind::Mud, i, h); extent = h; break; }
            case P_CREEK_BRANCH: { float h = 2.4f; add_band(ObKind::Creek, i, h); add_branch(i + (int)(h + 11)); extent = h + 11; break; }
            case P_FALLER: add_faller(i + 10, side); extent = 10; break;  // (with room to see it coming and react)
            case P_GAUNTLET: add_log(i, 0.1f); add_branch(i + 13); add_rock(i + 25, side); add_log(i + 37, 0.1f); extent = 37; break;
        }
        s += extent + std::max(kMinGap, orng(gap_lo, gap_hi) * gap_scale);
      }
      return (int)obstacles.size();
    };
    // Easy is the plain layout. Harder trails close the gaps until they hold the right number more.
    int want = (int)std::lround(lay_out(1.0f, 0) * difficulty_crowd(difficulty));
    if (difficulty > 0) {
        float lo = 0.2f, hi = 1.0f, best = 1.0f;
        int best_off = 1 << 30;
        for (int tries = 0; tries < 14; tries++) {
            float mid = (lo + hi) * 0.5f;
            int got = lay_out(mid, 20), off = std::abs(got - want);
            if (off < best_off) best_off = off, best = mid;
            if (got == want) break;
            (got < want ? hi : lo) = mid;
        }
        lay_out(best, 20);
    }

    size_t own_trees = trees.size();  // (those holding up obstacles; the forest comes after)

    // Trees: a few close to the trail's edge, more further in. Nothing on the trail itself
    // (which matters where it curves back near an earlier stretch).
    for (int i = 0; i < n; i += 2) {
        const PathPt& q = pts[i];
        Vector3 right = heading_right(q.yaw), fwd = heading_dir(q.yaw);
        struct Band { float lo, hi, count; } bands[] = {{0.9f, 4.0f, 0.3f}, {4.0f, 15.0f, 1.1f}, {15.0f, 34.0f, 1.0f}};
        for (const Band& b : bands)
            for (int side = -1; side <= 1; side += 2) {
                float count = b.count;
                while (count > 0) {
                    if (rng() < count) {
                        float off = q.half + rng(b.lo, b.hi);
                        Vector3 at = Vector3Add(Vector3Add(q.p, Vector3Scale(right, side * off)), Vector3Scale(fwd, rng(-1, 1)));
                        if (!on_trail(at, 1.0f)) {
                            at.y = ground_at(at.x, at.z, i) - 0.15f;
                            float r = b.lo < 1 ? rng(0.25f, 0.6f) : rng(0.15f, 0.55f);
                            trees.push_back({at, r, rng(15, 24), i});
                        }
                    }
                    count -= 1;
                }
            }
    }

    // Undergrowth: ferns and grass crowd the edges; bushes further in.
    for (int i = 0; i < n; i++) {
        const PathPt& q = pts[i];
        Vector3 right = heading_right(q.yaw);
        for (int side = -1; side <= 1; side += 2) {
            auto put = [&](PlantKind kind, float lo, float hi, float size_lo, float size_hi, float margin) {
                Vector3 at = Vector3Add(q.p, Vector3Scale(right, side * (q.half + rng(lo, hi))));
                at = Vector3Add(at, Vector3Scale(heading_dir(q.yaw), rng(-0.5f, 0.5f)));
                if (on_trail(at, margin)) return;
                at.y = ground_at(at.x, at.z, i);
                plants.push_back({kind, at, rng(size_lo, size_hi), rng(0, kTau), i});
            };
            if (rng() < 0.75f) put(PlantKind::Fern, 0.1f, 3.0f, 0.8f, 1.5f, -0.2f);
            if (rng() < 0.9f) put(PlantKind::Fern, 3.0f, 16.0f, 0.9f, 1.7f, 0.5f);
            if (rng() < 0.7f) put(PlantKind::Grass, -0.4f, 0.8f, 0.4f, 0.7f, -0.6f);
            if (rng() < 0.22f) put(PlantKind::Bush, 2.5f, 22.0f, 1.2f, 2.4f, 1.0f);
        }
    }

    // Clear the way for what needs it: a boulder's run down to the trail and the raptor's view of
    // it all the way in, and round a ribcage.
    // (Afterwards, so the forest is otherwise the same at every difficulty.)
    for (const Obstacle& o : obstacles) {
        if (o.kind != ObKind::Roller && o.kind != ObKind::Ribs) continue;
        Vector3 from = o.kind == ObKind::Roller ? o.c : Vector3Subtract(o.c, Vector3Scale(o.axis, o.len * 0.5f + 1.5f));
        float run = o.kind == ObKind::Roller ? o.len + 1.0f : o.len + 3.0f, clear = o.kind == ObKind::Roller ? o.r + 1.2f : 4.5f;
        auto in_sight = [&](const Tree& t) {  // near any line from the raptor on its way in to the boulder then
            if (o.kind != ObKind::Roller) return false;
            Vector3 eye0 = pts[o.idx - kRollerSight].p, meet = pts[o.idx].p;
            for (float f = 0; f < 0.9f; f += 0.15f) {
                Vector3 a = Vector3Lerp(eye0, meet, f), b = Vector3Lerp(o.c, meet, f), ab = Vector3Subtract(b, a), at = Vector3Subtract(t.p, a);
                float u = clampf((at.x * ab.x + at.z * ab.z) / (ab.x * ab.x + ab.z * ab.z), 0, 1);
                float dx = at.x - ab.x * u, dz = at.z - ab.z * u;
                if (dx * dx + dz * dz < (1.1f + t.r) * (1.1f + t.r)) return true;
            }
            return false;
        };
        trees.erase(std::remove_if(trees.begin() + own_trees, trees.end(), [&](const Tree& t) {
                        Vector3 d = Vector3Subtract(t.p, from);
                        float along = d.x * o.axis.x + d.z * o.axis.z, across = d.x * -o.axis.z + d.z * o.axis.x;
                        return (along > -1.5f && along < run && std::fabs(across) < clear + t.r) || in_sight(t);
                    }), trees.end());
    }
}

}  // namespace rr
