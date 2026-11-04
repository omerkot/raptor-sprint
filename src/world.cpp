#include "world.hpp"

#include <cstdlib>

#include "noise.hpp"

namespace rr {

namespace {

constexpr int CHUNK = 20;  // trail samples (metres) per chunk
constexpr float kFrontLine = 6.0f, kSecondLine = 14.0f;  // how far from the trail's edge each row of trees ends

uint32_t g_rng = 777;
float rnd(float lo = 0, float hi = 1) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return lo + (hi - lo) * ((g_rng >> 8) / 16777215.0f);
}

Color tint(float r, float g, float b) { return rgb(r, g, b); }
Color scale_col(Color c, float f) { return rgb(c.r / 255.0f * f, c.g / 255.0f * f, c.b / 255.0f * f); }

// A tube along a centre line with a radius per point: trunks, logs and branches. Bark tiles
// about every 1.4 m round and 3 m along. `col` gives each vertex its colour (moss, shade).
template <class ColorFn>
void tube(MeshBuilder& mb, const std::vector<Vector3>& c, const std::vector<float>& r, int seg, ColorFn col, bool cap_ends) {
    int n = (int)c.size();
    std::vector<Vector3> tang(n), side(n), up(n);
    for (int i = 0; i < n; i++) {
        Vector3 t = Vector3Normalize(Vector3Subtract(c[std::min(n - 1, i + 1)], c[std::max(0, i - 1)]));
        Vector3 ref = std::fabs(t.y) > 0.9f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
        side[i] = Vector3Normalize(Vector3CrossProduct(t, ref));
        up[i] = Vector3CrossProduct(side[i], t);
        tang[i] = t;
    }
    float around = std::max(1.0f, std::round(kTau * r[0] / 1.4f));
    std::vector<float> along(n, 0);
    for (int i = 1; i < n; i++) along[i] = along[i - 1] + Vector3Distance(c[i], c[i - 1]);
    auto vert = [&](int i, int k, Vector3& p, Vector3& nr, Vector2& uv) {
        float a = kTau * k / seg;
        nr = Vector3Add(Vector3Scale(side[i], std::cos(a)), Vector3Scale(up[i], std::sin(a)));
        p = Vector3Add(c[i], Vector3Scale(nr, r[i]));
        uv = {around * k / seg, -along[i] / 3.0f};
    };
    for (int i = 0; i + 1 < n; i++)
        for (int k = 0; k < seg; k++) {
            Vector3 p[4], nr[4];
            Vector2 uv[4];
            vert(i, k, p[0], nr[0], uv[0]);
            vert(i, k + 1, p[1], nr[1], uv[1]);
            vert(i + 1, k, p[2], nr[2], uv[2]);
            vert(i + 1, k + 1, p[3], nr[3], uv[3]);
            Color k0 = col(p[0], nr[0]), k1 = col(p[1], nr[1]), k2 = col(p[2], nr[2]), k3 = col(p[3], nr[3]);
            mb.vtri(p[0], p[2], p[1], nr[0], nr[2], nr[1], k0, k2, k1, uv[0], uv[2], uv[1]);
            mb.vtri(p[1], p[2], p[3], nr[1], nr[2], nr[3], k1, k2, k3, uv[1], uv[2], uv[3]);
        }
    if (cap_ends)
        for (int e = 0; e < 2; e++) {
            int i = e ? n - 1 : 0;
            Vector3 nn = Vector3Scale(tang[i], e ? 1.0f : -1.0f);
            Color wood = tint(0.55f, 0.43f, 0.30f);
            for (int k = 0; k < seg; k++) {
                Vector3 p0, p1, n0;
                Vector2 u0;
                vert(i, k, p0, n0, u0);
                vert(i, k + 1, p1, n0, u0);
                mb.vtri(c[i], p0, p1, nn, nn, nn, scale_col(wood, 0.8f), wood, wood, {0.5f, 0.5f}, {0.52f, 0.5f}, {0.5f, 0.52f});
            }
        }
}

void add_tree(MeshBuilder& bark, MeshBuilder& leaves, const Course& co, const Tree& t, bool low) {
    // A slightly leaning, tapering trunk with a flared base.
    float lean_x = rnd(-0.03f, 0.03f), lean_z = rnd(-0.03f, 0.03f);
    // Handhelds: fewer rings (the flare and the lean stay; the fog hides the rest), and the further
    // a tree stands from the trail the plainer it is: only the front line is ever seen up close.
    // It's settled by where the tree stands, not how far off the camera is, so nothing ever pops.
    Nearest near = co.nearest(t.p, t.idx, 60);
    float back = near.dist - near.half;
    int row = !low ? 0 : back < kFrontLine ? 0 : back < kSecondLine ? 1 : 2;
    static const std::vector<float> kRings[3] = {{0.0f, 0.6f, 3.5f, 1.0f}, {0.0f, 2.5f, 1.0f}, {0.0f, 1.0f}};  // (the last is the top)
    std::vector<float> hs = low ? kRings[row] : std::vector<float>{0.0f, 0.35f, 0.9f, 2.5f, 6.0f, 11.0f, t.h};
    hs.back() = t.h;
    std::vector<Vector3> c;
    std::vector<float> r;
    for (float y : hs) {
        float w = std::sin(y * 0.4f + t.p.x) * 0.06f;
        c.push_back({t.p.x + lean_x * y + w, t.p.y + y, t.p.z + lean_z * y});
        float flare = row == 2 ? 1.0f : 1.0f + 0.7f * std::exp(-y * 2.2f);
        r.push_back(t.r * flare * lerpf(1.0f, 0.55f, y / t.h));
    }
    float moss_dir = rnd(0, kTau), shade = rnd(0.8f, 1.1f);
    Vector3 base = t.p;
    tube(bark, c, r, !low ? 8 : row == 0 ? 6 : row == 1 ? 5 : 4,
         [&](Vector3 p, Vector3 n) {
             float y = p.y - base.y;
             if (row == 2) return scale_col(WHITE, shade * 0.85f);  // (one span, base to top: no room for moss)
             float facing = 0.5f + 0.5f * (n.x * std::cos(moss_dir) + n.z * std::sin(moss_dir));
             float moss = (1.0f - smooth(0.3f, 2.4f, y)) * facing;
             float ao = 0.55f + 0.45f * smooth(0.0f, 1.2f, y);
             Vector3 k = Vector3Lerp({shade, shade, shade}, {0.45f, 0.62f, 0.25f}, moss * 0.85f);
             return rgb(k.x * ao, k.y * ao, k.z * ao);
         },
         false);
    // Canopy: a few big leaf sprays high up, mostly out of view but casting the light.
    int sprays = !low ? 4 : row == 2 ? 1 : 2;
    for (int i = 0; i < sprays; i++) {
        float y = t.h * rnd(0.55f, 0.95f), s = rnd(4.0f, 7.0f) * (row == 2 ? 1.3f : 1.0f);
        Vector3 at = {t.p.x + rnd(-2, 2), t.p.y + y, t.p.z + rnd(-2, 2)};
        Color k = scale_col(WHITE, rnd(0.7f, 1.0f));
        leaves.quad(s, s, xform(at, {rnd(-1.2f, -0.4f), rnd(0, kTau), 0}), k);
        leaves.quad(s, s, xform(at, {rnd(0.4f, 1.2f), rnd(0, kTau), 0}), k);
    }
}

// A boulder: a lumpy, rounded stone, smoothly shaded, darker and mossier towards the ground.
void add_rock(MeshBuilder& mb, const Obstacle& o) {
    const int SEG = 14, RING = 8;
    auto dir = [&](int ring, int k) {
        float th = kPi * ring / RING, a = kTau * (k % SEG) / SEG;
        return Vector3{std::sin(th) * std::cos(a), std::cos(th), std::sin(th) * std::sin(a)};
    };
    auto pt = [&](int ring, int k) {
        Vector3 d = dir(ring, k);
        float bump = 1.0f + 0.16f * (fbm(d.x * 1.4f + o.c.x, d.z * 1.4f + d.y * 1.1f + o.c.z, 3) - 0.5f) * 2.0f;
        return Vector3{o.c.x + d.x * o.r * bump * 1.1f, o.c.y + d.y * o.r * bump * 0.92f, o.c.z + d.z * o.r * bump};
    };
    auto col = [&](int ring) {
        float up = std::cos(kPi * ring / RING);  // 1 on top .. -1 underneath
        float k = 0.95f + 0.25f * up;
        return rgb(k, k, k * 0.97f);
    };
    auto uv = [](Vector3 p) { return Vector2{(p.x + p.y * 0.7f) / 1.6f, (p.z - p.y * 0.5f) / 1.6f}; };
    for (int ring = 0; ring < RING; ring++)
        for (int k = 0; k < SEG; k++) {
            Vector3 a = pt(ring, k), b = pt(ring, k + 1), c = pt(ring + 1, k), d = pt(ring + 1, k + 1);
            Vector3 na = dir(ring, k), nb = dir(ring, k + 1), nc = dir(ring + 1, k), nd = dir(ring + 1, k + 1);
            if (ring > 0) mb.vtri(a, c, b, na, nc, nb, col(ring), col(ring + 1), col(ring), uv(a), uv(c), uv(b));
            if (ring < RING - 1) mb.vtri(b, c, d, nb, nc, nd, col(ring), col(ring + 1), col(ring + 1), uv(b), uv(c), uv(d));
        }
}

// A fern: fronds arching out from the centre, each in two panels so it bends over.
void add_fern(MeshBuilder& mb, const Plant& p, bool low) {
    int fronds = low ? 5 : 7;
    Color base = scale_col(WHITE, rnd(0.75f, 1.05f));
    for (int i = 0; i < fronds; i++) {
        float a = p.yaw + kTau * i / fronds + rnd(-0.25f, 0.25f);
        float len = p.size * rnd(0.85f, 1.15f), w = len * 0.55f;
        Vector3 out = {std::cos(a), 0, std::sin(a)}, side = {-std::sin(a), 0, std::cos(a)};
        float rise1 = rnd(0.9f, 1.3f), rise2 = rnd(0.1f, 0.4f);
        Vector3 p0 = p.p;
        Vector3 p1 = Vector3Add(p0, Vector3Add(Vector3Scale(out, len * 0.45f), {0, len * 0.45f * rise1 * 0.8f, 0}));
        Vector3 p2 = Vector3Add(p1, Vector3Add(Vector3Scale(out, len * 0.55f), {0, len * 0.55f * rise2 - 0.05f, 0}));
        Vector3 nUp = Vector3Normalize(Vector3Add({0, 1, 0}, Vector3Scale(out, 0.3f)));
        float hw0 = w * 0.18f, hw1 = w * 0.5f, hw2 = w * 0.18f;
        Vector3 q[6] = {Vector3Add(p0, Vector3Scale(side, -hw0)), Vector3Add(p0, Vector3Scale(side, hw0)),
                        Vector3Add(p1, Vector3Scale(side, -hw1)), Vector3Add(p1, Vector3Scale(side, hw1)),
                        Vector3Add(p2, Vector3Scale(side, -hw2)), Vector3Add(p2, Vector3Scale(side, hw2))};
        Color dark = scale_col(base, 0.6f);
        mb.vtri(q[0], q[1], q[2], nUp, nUp, nUp, dark, dark, base, {0.32f, 1}, {0.68f, 1}, {0, 0.5f});
        mb.vtri(q[1], q[3], q[2], nUp, nUp, nUp, dark, base, base, {0.68f, 1}, {1, 0.5f}, {0, 0.5f});
        mb.vtri(q[2], q[3], q[4], nUp, nUp, nUp, base, base, base, {0, 0.5f}, {1, 0.5f}, {0.32f, 0});
        mb.vtri(q[3], q[5], q[4], nUp, nUp, nUp, base, base, base, {1, 0.5f}, {0.68f, 0}, {0.32f, 0});
    }
}

void add_card_cluster(MeshBuilder& mb, Vector3 at, float w, float h, int cards, float yaw, float shade_lo) {
    for (int i = 0; i < cards; i++) {
        float a = yaw + kPi * i / cards;
        Color k = scale_col(WHITE, rnd(shade_lo, 1.0f));
        Matrix m = xform({at.x, at.y + h * 0.5f, at.z}, {rnd(-0.15f, 0.15f), a, 0});
        // Normals tilted upwards so the cluster lights like a soft volume, not flat cards.
        Matrix nm = MatrixIdentity();
        Vector3 n = {0, 1, 0};
        Vector3 c0 = Vector3Transform({-w / 2, -h / 2, 0}, m), c1 = Vector3Transform({w / 2, -h / 2, 0}, m);
        Vector3 c2 = Vector3Transform({w / 2, h / 2, 0}, m), c3 = Vector3Transform({-w / 2, h / 2, 0}, m);
        Color lo = scale_col(k, 0.55f);
        mb.vtri(c0, c1, c2, n, n, n, lo, lo, k, {0, 1}, {1, 1}, {1, 0});
        mb.vtri(c0, c2, c3, n, n, n, lo, k, k, {0, 1}, {1, 0}, {0, 0});
        (void)nm;
    }
}

// A band across the trail: a creek (dark water between muddy banks) or a mud pool (wet, brown,
// with glinting puddles). Flat, just above the trail so it covers it.
void add_band(MeshBuilder& water, MeshBuilder& ground, const Obstacle& o) {
    Vector3 across = o.axis, along = {-o.axis.z, 0, o.axis.x}, up = {0, 1, 0};
    auto P = [&](float a, float l, float lift) {
        Vector3 p = Vector3Add(o.c, Vector3Add(Vector3Scale(across, a), Vector3Scale(along, l)));
        p.y = o.c.y + lift;
        return p;
    };
    auto rect = [&](MeshBuilder& mb, float a0, float a1, float l0, float l1, float lift, Color c0, Color c1, bool world_uv) {
        const int N = 8;
        for (int i = 0; i < N; i++) {
            float x0 = lerpf(a0, a1, (float)i / N), x1 = lerpf(a0, a1, (float)(i + 1) / N);
            Vector3 p00 = P(x0, l0, lift), p10 = P(x1, l0, lift), p01 = P(x0, l1, lift), p11 = P(x1, l1, lift);
            auto uv = [&](Vector3 q) { return world_uv ? Vector2{q.x / 4.0f, q.z / 4.0f} : Vector2{0.5f, 0.5f}; };
            mb.vtri(p00, p01, p10, up, up, up, c0, c1, c0, uv(p00), uv(p01), uv(p10));
            mb.vtri(p10, p01, p11, up, up, up, c0, c1, c1, uv(p10), uv(p01), uv(p11));
        }
    };
    float half = o.len * 0.5f;
    if (o.kind == ObKind::Creek) {
        Color deep = rgb(0.06f, 0.10f, 0.10f), shallow = rgb(0.13f, 0.16f, 0.13f), bank = rgb(0.20f, 0.15f, 0.10f);
        rect(water, -half, half, -o.r, 0, 0.05f, shallow, deep, false);
        rect(water, -half, half, 0, o.r, 0.05f, deep, shallow, false);
        for (int k = 0; k < 7; k++) {  // ripples catching the light: faint, tapering streaks
            float l = rnd(-o.r * 0.7f, o.r * 0.7f), a = rnd(-half * 0.6f, half * 0.5f), len = rnd(0.6f, 1.6f), w = rnd(0.04f, 0.08f);
            Color glint = rgb(0.15f, 0.20f, 0.19f), clear = rgb(0.10f, 0.14f, 0.13f);
            const int S = 6;
            for (int q = 0; q < S; q++) {  // a lens shape, widest in the middle
                float f0 = (float)q / S, f1 = (float)(q + 1) / S, w0 = w * std::sin(kPi * f0), w1 = w * std::sin(kPi * f1);
                // (3 cm above the water: any closer and the depth buffer can't tell them apart)
                Vector3 a0 = P(a + len * f0, l - w0, 0.08f), b0 = P(a + len * f0, l + w0, 0.08f);
                Vector3 a1 = P(a + len * f1, l - w1, 0.08f), b1 = P(a + len * f1, l + w1, 0.08f);
                Vector2 z = {0.5f, 0.5f};
                water.vtri(a0, b0, a1, up, up, up, clear, clear, clear, z, z, z);
                water.vtri(a1, b0, b1, up, up, up, clear, glint, glint, z, z, z);
            }
        }
        rect(ground, -half, half, -o.r - 0.9f, -o.r, 0.05f, rgb(0.5f, 0.42f, 0.32f), bank, true);
        rect(ground, -half, half, o.r, o.r + 0.9f, 0.05f, bank, rgb(0.5f, 0.42f, 0.32f), true);
    } else {
        // Churned mud: one sheet across the trail, wet brown in the middle, drying to the trail's
        // colour at ragged edges, its sides sinking into the ground beyond the trail. (One sheet,
        // so no seams for the ground to show through.) Dark puddles lie in it, a few cm up.
        const int NA = 12, NL = 10;
        float reach = o.r + 0.7f, side = half - 1.0f;
        auto edge = [&](float a) { return o.r + 0.3f * std::sin(a * 1.7f + o.c.x) + 0.2f * std::sin(a * 3.1f + o.c.z); };
        Color wet = rgb(0.40f, 0.30f, 0.20f), dry = rgb(0.80f, 0.72f, 0.58f);
        auto vert = [&](int i, int j, Vector3& p, Color& col) {
            float a = lerpf(-half, half, (float)i / NA), f = (float)j / NL * 2 - 1;  // f: -1 .. 1 along the trail
            float e = edge(a), l = f * (std::fabs(f) > 0.99f ? e + 0.6f : reach);
            float sink = smooth(side, half, std::fabs(a)) * 0.25f;  // down into the ground beyond the trail
            p = P(a, l, 0.075f - sink);
            col = Color{0, 0, 0, 255};
            float m = 1.0f - smooth(e - 0.35f, e + 0.25f, std::fabs(l));
            Color c = {(unsigned char)lerpf(dry.r, wet.r, m), (unsigned char)lerpf(dry.g, wet.g, m), (unsigned char)lerpf(dry.b, wet.b, m), 255};
            col = c;
        };
        for (int i = 0; i < NA; i++)
            for (int j = 0; j < NL; j++) {
                Vector3 p00, p10, p01, p11;
                Color c00, c10, c01, c11;
                vert(i, j, p00, c00), vert(i + 1, j, p10, c10), vert(i, j + 1, p01, c01), vert(i + 1, j + 1, p11, c11);
                auto uv = [](Vector3 q) { return Vector2{q.x / 4.0f, q.z / 4.0f}; };
                ground.vtri(p00, p01, p10, up, up, up, c00, c01, c10, uv(p00), uv(p01), uv(p10));
                ground.vtri(p10, p01, p11, up, up, up, c10, c01, c11, uv(p10), uv(p01), uv(p11));
            }
        for (int k = 0; k < 12; k++) {  // puddles: uneven pools
            float l = rnd(-o.r * 0.7f, o.r * 0.7f), a = rnd(-side * 0.85f, side * 0.85f), ra = rnd(0.4f, 1.1f), rl = rnd(0.25f, 0.55f);
            Color mid = rgb(0.09f, 0.10f, 0.09f), rim = rgb(0.17f, 0.15f, 0.11f);
            Vector3 c = P(a, l, 0.11f);
            const int S = 12;
            for (int q = 0; q < S; q++) {
                float t0 = kTau * q / S, t1 = kTau * (q + 1) / S;
                auto at = [&](float t) {
                    float w = 1.0f + 0.18f * std::sin(t * 3.0f + k) + 0.1f * std::sin(t * 5.0f + 2 * k);
                    return P(a + std::cos(t) * ra * w, l + std::sin(t) * rl * w, 0.11f);
                };
                water.vtri(c, at(t0), at(t1), up, up, up, mid, rim, rim, {0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f});
            }
        }
    }
}

// Points along a curve from a to b, bowed `lift` upwards in the middle.
std::vector<Vector3> arc(Vector3 a, Vector3 b, float lift, int n) {
    std::vector<Vector3> c;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / n;
        Vector3 p = Vector3Lerp(a, b, t);
        p.y += lift * 4 * t * (1 - t);
        c.push_back(p);
    }
    return c;
}

// A bank of brambles: thorny canes arching over each other, dark leaves among them, red berries.
void add_bramble(MeshBuilder& canes, MeshBuilder& leaves, const Course& co, const Obstacle& o, bool low) {
    Vector3 across = o.axis, along = {-o.axis.z, 0, o.axis.x};
    float half = o.len * 0.5f;
    auto at = [&](float a, float l) {
        Vector3 p = Vector3Add(o.c, Vector3Add(Vector3Scale(across, a), Vector3Scale(along, l)));
        p.y = co.ground(p.x, p.z, o.idx);
        return p;
    };
    auto cane = [](Vector3 p, Vector3 n) {
        float k = 0.55f + 0.2f * smooth(-0.5f, 0.6f, n.y);
        (void)p;
        return rgb(k * 1.2f, k * 0.5f, k * 0.5f);
    };
    int n = (int)(o.len * (low ? 5.0f : 8.0f));
    for (int i = 0; i < n; i++) {
        float a = rnd(-half, half), l = rnd(-o.r, o.r), reach = rnd(0.8f, 1.7f), turn = rnd(0, kTau);
        float a2 = clampf(a + std::cos(turn) * reach, -half, half), l2 = clampf(l + std::sin(turn) * reach, -o.r, o.r);
        std::vector<Vector3> c = arc(at(a, l), at(a2, l2), rnd(0.35f, kBrambleTop + 0.1f), 4);
        tube(canes, c, {0.03f, 0.03f, 0.028f, 0.024f, 0.018f}, 3, cane, false);
    }
    for (float a = -half + 0.6f; a < half; a += 1.3f)
        for (float l : {-o.r * 0.5f, o.r * 0.5f}) {
            Vector3 p = at(a + rnd(-0.3f, 0.3f), l + rnd(-0.2f, 0.2f));
            add_card_cluster(leaves, p, 1.5f, kBrambleTop + 0.2f, 2, rnd(0, kTau), 0.4f);
        }
}

// A flood's leavings piled across the trail, too tall to leap, with a way through at `gap`: whole
// trunks washed in at every angle, crooked and tapering, some with their root plates still on,
// some sticking up out of the heap. Nothing level, nothing evenly spaced. Most are bleached
// silver-grey (`pale`: plain colour), the rest still dark and wet (`dark`: bark), all of it packed
// with silt (`silt`), so it reads as solid.
void add_jam(MeshBuilder& pale, MeshBuilder& dark, MeshBuilder& silt, const Course& co, const Obstacle& o, bool low) {
    Vector3 across = o.axis, along = {-o.axis.z, 0, o.axis.x};
    float half = o.len * 0.5f, gap0 = o.gap - o.gap_w, gap1 = o.gap + o.gap_w;
    auto at = [&](float a, float l, float up) {
        Vector3 p = Vector3Add(o.c, Vector3Add(Vector3Scale(across, a), Vector3Scale(along, l)));
        p.y = co.ground(p.x, p.z, o.idx) + up;
        return p;
    };
    int seg = low ? 4 : 7;
    for (int side = 0; side < 2; side++) {
        float a0 = side ? gap1 : -half, a1 = side ? half : gap0;
        // The heap's height: lumpy, highest by the trail, dying away into the forest.
        float ph = rnd(0, kTau);
        auto heap = [&](float a) {
            float lump = 1.0f + 0.16f * std::sin(a * 1.3f + ph) + 0.1f * std::sin(a * 2.9f + ph * 2);
            return 1.85f * lump * (0.3f + 0.7f * (1.0f - smooth(half - 5.0f, half, std::fabs(a))));
        };
        // One trunk from `s` (the thick end) to `e`, bent along the way.
        auto trunk = [&](Vector3 s, Vector3 e, float r, bool roots) {
            bool bleached = rnd() < 0.6f;
            float shade = rnd(0.68f, 1.0f), warm = rnd(0.88f, 0.96f);
            auto col = [&](Vector3 p, Vector3 n) {
                float k = shade * (0.74f + 0.2f * smooth(-0.5f, 0.8f, n.y) + 0.05f * std::sin(p.x * 3.1f + p.y * 4.3f + p.z * 2.3f));
                k *= bleached ? 0.66f : 1.1f;
                return rgb(k, k * warm, k * warm * warm);
            };
            MeshBuilder& mb = bleached ? pale : dark;
            Vector3 dir = Vector3Normalize(Vector3Subtract(e, s));
            Vector3 bend = Vector3Normalize(Vector3CrossProduct(dir, {rnd(-1, 1), 1, rnd(-1, 1)}));
            float crook = rnd(-0.35f, 0.35f), kink = rnd(-0.12f, 0.12f);
            std::vector<Vector3> c;
            std::vector<float> w;
            for (int i = 0; i <= 4; i++) {
                float t = i / 4.0f;
                Vector3 p = Vector3Add(Vector3Lerp(s, e, t), Vector3Scale(bend, crook * std::sin(t * kPi) + kink * std::sin(t * kTau)));
                c.push_back(p);
                w.push_back(r * lerpf(1.0f, 0.22f, std::pow(t, 0.8f)));
            }
            tube(mb, c, w, seg, col, true);
            if (!roots) return;
            // The root plate: a spray of thick roots round the butt, reaching back and out.
            Vector3 u = Vector3Normalize(Vector3CrossProduct(dir, {0, 1, 0})), v = Vector3CrossProduct(u, dir);
            int n = low ? 5 : 7;
            for (int i = 0; i < n; i++) {
                float ang = kTau * i / n + rnd(-0.3f, 0.3f), reach = rnd(0.6f, 1.25f);
                Vector3 out = Vector3Add(Vector3Scale(u, std::cos(ang)), Vector3Scale(v, std::sin(ang)));
                Vector3 mid = Vector3Add(s, Vector3Add(Vector3Scale(out, reach * 0.55f), Vector3Scale(dir, -0.12f)));
                Vector3 tip = Vector3Add(s, Vector3Add(Vector3Scale(out, reach), Vector3Scale(dir, rnd(-0.55f, 0.1f))));
                tube(mb, {s, mid, tip}, {r * 0.5f, r * 0.3f, 0.025f}, 4, col, false);
            }
        };
        // The silt bank the wood is caught in: a lumpy ridge, ending steeply at the gap.
        {
            const float L[6] = {-1.5f, -0.95f, -0.35f, 0.35f, 0.95f, 1.5f}, Hh[6] = {0.0f, 0.62f, 1.0f, 1.0f, 0.62f, 0.0f};
            auto bank = [&](float a, int j) {
                float end = smooth(0, 0.7f, a - a0) * smooth(0, 0.7f, a1 - a);
                float h = 0.4f * heap(a) * Hh[j] * end * (1.0f + 0.14f * std::sin(a * 3.7f + j * 1.9f + ph));
                return at(a, L[j] + 0.14f * std::sin(a * 1.9f + j), h - 0.05f);
            };
            Color wet = rgb(0.5f, 0.44f, 0.36f), top = rgb(0.72f, 0.66f, 0.56f);
            auto uv = [](Vector3 q) { return Vector2{(q.x + q.y) / 4.0f, (q.z + q.y) / 4.0f}; };
            for (float a = a0; a < a1 - 0.01f;) {
                float b = std::min(a1, a + 0.8f);
                for (int j = 0; j < 5; j++) {
                    Vector3 p00 = bank(a, j), p10 = bank(b, j), p01 = bank(a, j + 1), p11 = bank(b, j + 1);
                    Vector3 nr = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p01, p00), Vector3Subtract(p10, p00)));
                    if (nr.y < 0) nr = Vector3Negate(nr);
                    Color c0 = Hh[j] > 0.7f ? top : wet, c1 = Hh[j + 1] > 0.7f ? top : wet;
                    silt.vtri(p00, p10, p01, nr, nr, nr, c0, c0, c1, uv(p00), uv(p10), uv(p01));
                    silt.vtri(p10, p11, p01, nr, nr, nr, c0, c1, c1, uv(p10), uv(p11), uv(p01));
                }
                a = b;
            }
        }
        // Inside this side of the gap, so the way through stays clear.
        auto keep = [&](float a) { return clampf(a, a0 + 0.12f, a1 - 0.12f); };
        int n = (int)((a1 - a0) * (low ? 2.4f : 3.4f));
        for (int i = 0; i < n; i++) {
            float a = rnd(a0, a1), top = heap(a), y = top * lerpf(0.26f, 1.0f, std::pow(rnd(), 1.4f));  // (more of them low down, on the silt)
            if (low && std::fabs(a) > 6.0f && rnd() < 0.6f) continue;  // (handhelds: thinner out among the trees)
            float len = rnd(2.4f, 5.6f), turn = rnd(-1.15f, 1.15f), tilt = rnd(-0.32f, 0.32f);
            if (rnd() < 0.5f) turn += kPi;  // butt end either way round
            float da = std::cos(turn) * len * 0.5f, dl = std::sin(turn) * len * 0.5f, dy = std::sin(tilt) * len * 0.5f;
            float l = rnd(-0.55f, 0.55f);
            Vector3 s = at(keep(a - da), clampf(l - dl, -1.3f, 1.3f), std::max(0.08f, y - dy));
            Vector3 e = at(keep(a + da), clampf(l + dl, -1.3f, 1.3f), clampf(y + dy, 0.05f, top + 0.3f));
            float r = rnd(0.16f, 0.34f) * (y < 0.7f ? 1.25f : 1.0f);
            bool room = std::fabs((a - da) - keep(a - da)) < 0.01f && (a - da) > a0 + 1.3f && (a - da) < a1 - 1.3f;
            trunk(s, e, r, room && rnd() < 0.24f);
        }
        // A few snags thrust up out of the heap at odd angles.
        for (int i = 0, m = (int)((a1 - a0) / 3.2f); i < m; i++) {
            float a = rnd(a0 + 0.5f, a1 - 0.5f), l = rnd(-0.6f, 0.6f), top = heap(a);
            Vector3 s = at(a, l, top * 0.25f);
            Vector3 e = at(keep(a + rnd(-1.6f, 1.6f)), l + rnd(-1.0f, 1.0f), top + rnd(0.2f, 0.9f));
            trunk(s, e, rnd(0.09f, 0.17f), false);
        }
    }
}

// A root gate: two great trees either side of the trail whose roots have grown out to meet. Thick
// roots hump across the ground (to be leapt) and others climb from the trunks and arch over at
// `gap` (to be ducked), twisting round each other, knotted and tapering, thin ones trailing.
void add_gate(MeshBuilder& mb, const Course& co, const Obstacle& o, bool low) {
    const PathPt& q = co.pts[o.idx];
    Vector3 side = {-o.axis.z, 0, o.axis.x};
    float foot = o.len * 0.5f, lane = q.half + 0.15f, base = q.p.y;
    float ground_top = o.c.y + o.r - 0.08f;  // where the leap has to clear
    auto flat = [&](float a, float l) { return Vector3Add(o.c, Vector3Add(Vector3Scale(o.axis, a), Vector3Scale(side, l))); };
    auto ground_at = [&](Vector3 p) { return co.ground(p.x, p.z, o.idx); };
    auto wood = [](Vector3 p, Vector3 n) {
        float ao = 0.55f + 0.45f * smooth(-0.6f, 0.4f, n.y);
        float grain = 0.9f + 0.1f * std::sin(p.x * 5.1f + p.y * 6.3f + p.z * 4.7f);
        Vector3 k = Vector3Lerp({1.0f, 0.84f, 0.66f}, {0.55f, 0.72f, 0.30f}, smooth(0.55f, 0.98f, n.y) * 0.7f);
        return rgb(k.x * ao * grain, k.y * ao * grain, k.z * ao * grain);
    };
    int seg = low ? 5 : 7;
    // Knots and swellings along a taper, so no root is a clean cylinder.
    auto girth = [](float t, float r0, float r1, float ph) {
        return lerpf(r0, r1, std::pow(t, 0.75f)) * (1.0f + 0.16f * std::sin(t * 13.0f + ph) + 0.08f * std::sin(t * 29.0f + ph * 2));
    };
    int pts = low ? 8 : 11;
    for (int e = -1; e <= 1; e += 2) {
        float from = e * foot;
        // Over the top: from the trunk, up steeply, then out across the trail, twisting.
        for (int i = 0, n = 3; i < n; i++) {
            // The lowest root sets the height to duck; the others arch higher, so they spread
            // into a tangle against the light rather than one beam.
            float to = -e * lane * (i == 0 ? rnd(0.2f, 0.6f) : rnd(0.7f, 1.2f)), r0 = rnd(0.2f, 0.28f) - 0.03f * i, ph = rnd(0, kTau);
            float lift = i ? rnd(0.1f, 0.3f) : 0.03f, bow = i == 0 ? 0.0f : i == 1 ? rnd(0.5f, 0.8f) : rnd(1.1f, 1.6f);
            float wander = rnd(0.3f, 0.55f), l0 = rnd(-0.45f, 0.45f), freq = rnd(3.5f, 6.5f);
            std::vector<Vector3> c;
            std::vector<float> w;
            for (int k = 0; k <= pts; k++) {
                float t = std::pow((float)k / pts, 1.25f), a = lerpf(from, to, t), r = girth(t, r0, 0.035f, ph);
                float l = l0 * (1 - t) + wander * std::sin(t * freq + ph) * smooth(0.0f, 0.25f, t);
                Vector3 p = flat(a, l);
                float over = clampf(1.0f - (a / lane) * (a / lane), 0, 1);
                float g = ground_at(p), crown = base + o.gap + r * 1.05f + lift + bow * over + 0.09f + 0.09f * std::sin(t * 9.0f + ph);
                float climb = clampf((foot - a * e) / std::max(0.4f, foot - lane), 0, 1);
                p.y = lerpf(g + 0.15f, crown, std::pow(smooth(0, 1, climb), 0.85f));
                c.push_back(p);
                w.push_back(r);
            }
            tube(mb, c, w, seg, wood, false);
            if (low && i == 2) continue;
            // A fork leaving it near the top of the climb, reaching out and on.
            int at = pts / 2;
            Vector3 s = c[at], tip = Vector3Add(s, {rnd(-0.3f, 0.3f) - o.axis.x * e * rnd(0.8f, 1.6f), rnd(0.15f, 0.5f), rnd(-0.3f, 0.3f) - o.axis.z * e * rnd(0.8f, 1.6f)});
            Vector3 tip_side = Vector3Add(tip, Vector3Scale(side, rnd(-0.7f, 0.7f)));
            Vector3 mid = Vector3Add(Vector3Lerp(s, tip_side, 0.5f), {0, 0.14f, 0});
            tube(mb, {s, mid, tip_side}, {w[at] * 0.6f, w[at] * 0.36f, 0.02f}, 4, wood, false);
        }
        // Across the ground: humped over the trail to just the height to leap, then diving in.
        for (int i = 0, n = low ? 2 : 3; i < n; i++) {
            // One root at the full height of the leap, down the middle; lower ones snake about it.
            float to = -e * (lane + rnd(0.2f, 1.0f)), r0 = i ? rnd(0.13f, 0.19f) : rnd(0.26f, 0.32f), ph = rnd(0, kTau), l0 = rnd(-0.3f, 0.3f);
            float wander = i ? rnd(0.7f, 1.1f) : rnd(0.2f, 0.3f), freq = i ? rnd(4.0f, 6.5f) : rnd(6.0f, 9.0f), under = i ? rnd(0.18f, 0.3f) : 0.0f;
            std::vector<Vector3> c;
            std::vector<float> w;
            for (int k = 0; k <= pts; k++) {
                float t = (float)k / pts, a = lerpf(from, to, t), r = girth(t, r0, 0.07f, ph);
                Vector3 p = flat(a, l0 * (1 - t) + wander * std::sin(t * freq + ph));
                float g = ground_at(p), over = 1.0f - smooth(lane, foot, std::fabs(a));
                float dive = smooth(0.82f, 1.0f, t);
                float hump = i ? 0.1f * std::sin(t * 11.0f + ph) : 0.07f * std::sin(t * 8.0f + ph) - 0.05f;
                p.y = lerpf(g + r * 0.5f, std::max(g + r * 0.6f, ground_top - r - under + hump), over) - dive * 0.45f;
                c.push_back(p);
                w.push_back(r);
            }
            tube(mb, c, w, seg, wood, false);
            if (i) continue;
            for (int f = 0; f < 4; f++) {  // side roots, spreading along the ground
                int at = pts * (2 + f) / 7;
                float reach = (f % 2 ? 1.0f : -1.0f) * rnd(1.8f, 3.0f), a = lerpf(from, to, (float)at / pts);
                Vector3 s = c[at], m = flat(a + rnd(-0.9f, 0.9f), reach * 0.45f), tip = flat(a + rnd(-1.6f, 1.6f), reach);
                m.y = ground_at(m) + 0.09f, tip.y = ground_at(tip) - 0.06f;
                tube(mb, {s, m, tip}, {w[at] * 0.5f, 0.07f, 0.02f}, 4, wood, false);
            }
        }
        // Buttresses flaring from the trunk, and thin roots trailing at the trail's edges.
        for (int i = 0; i < (low ? 3 : 5); i++) {
            float turn = rnd(0, kTau), reach = rnd(1.0f, 2.1f);
            Vector3 s = flat(from, 0), t = Vector3Add(s, {std::cos(turn) * reach, 0, std::sin(turn) * reach}), m = Vector3Lerp(s, t, 0.5f);
            s.y = ground_at(s) + 0.75f, m.y = ground_at(m) + 0.22f, t.y = ground_at(t) - 0.12f;
            tube(mb, {s, m, t}, {0.26f, 0.17f, 0.05f}, 4, wood, false);
        }
        for (int i = 0; i < (low ? 2 : 4); i++) {
            float a = e * rnd(lane - 0.3f, lane + 0.9f), l = rnd(-0.4f, 0.4f), drop = rnd(0.8f, 1.5f);
            Vector3 s = flat(a, l);
            s.y = base + o.gap + 0.3f;
            Vector3 m = Vector3Add(s, {rnd(-0.1f, 0.1f), -drop * 0.55f, rnd(-0.1f, 0.1f)}), t = Vector3Add(s, {rnd(-0.16f, 0.16f), -drop, rnd(-0.16f, 0.16f)});
            tube(mb, {s, m, t}, {0.03f, 0.02f, 0.008f}, 3, wood, false);
        }
    }
}

// A sinkhole: wet mud turning in towards a dark pit. Flat on the ground; the colour does the work.
void add_sinkhole(MeshBuilder& mud, MeshBuilder& reeds, const Course& co, const Obstacle& o) {
    const int RINGS = 7, SEG = 22;
    Vector3 up = {0, 1, 0};
    float reach = o.r * 0.95f;
    auto pt = [&](float rad, float a, float lift) {
        float w = 1.0f + 0.07f * std::sin(a * 3.0f + o.c.x) + 0.05f * std::sin(a * 5.0f + o.c.z);
        Vector3 p = {o.c.x + std::cos(a) * rad * w, 0, o.c.z + std::sin(a) * rad * w};
        p.y = co.ground(p.x, p.z, o.idx) + lift;
        return p;
    };
    Color dry = rgb(0.80f, 0.72f, 0.58f), wet = rgb(0.56f, 0.43f, 0.29f), dark = rgb(0.06f, 0.045f, 0.03f);
    auto col = [&](float f, float a) {  // f: 0 at the pit .. 1 at the edge, with a spiral turning in
        float swirl = 0.5f + 0.5f * std::sin(a * 3.0f + f * 7.0f);
        float rim = smooth(0.55f, 1.0f, f);
        auto mix = [](Color x, Color y, float t) {
            return Color{(unsigned char)lerpf(x.r, y.r, t), (unsigned char)lerpf(x.g, y.g, t), (unsigned char)lerpf(x.b, y.b, t), 255};
        };
        return mix(mix(dark, wet, swirl), dry, rim);
    };
    auto uv = [](Vector3 q) { return Vector2{q.x / 4.0f, q.z / 4.0f}; };
    for (int j = 0; j < RINGS; j++)
        for (int k = 0; k < SEG; k++) {
            float f0 = (float)j / RINGS, f1 = (float)(j + 1) / RINGS, a0 = kTau * k / SEG, a1 = kTau * (k + 1) / SEG;
            float r0 = lerpf(o.len * 0.7f, reach, f0), r1 = lerpf(o.len * 0.7f, reach, f1);
            Vector3 p00 = pt(r0, a0, 0.078f), p10 = pt(r1, a0, 0.078f), p01 = pt(r0, a1, 0.078f), p11 = pt(r1, a1, 0.078f);
            Color c00 = col(f0, a0), c10 = col(f1, a0), c01 = col(f0, a1), c11 = col(f1, a1);
            mud.vtri(p00, p01, p10, up, up, up, c00, c01, c10, uv(p00), uv(p01), uv(p10));
            mud.vtri(p10, p01, p11, up, up, up, c10, c01, c11, uv(p10), uv(p01), uv(p11));
        }
    Color mid = rgb(0.0f, 0.0f, 0.0f), lip = rgb(0.03f, 0.025f, 0.02f);
    Vector3 c = pt(0, 0, 0.1f);
    Vector2 z = {0.5f, 0.5f};
    for (int k = 0; k < SEG; k++) {
        float a0 = kTau * k / SEG, a1 = kTau * (k + 1) / SEG;
        Vector3 p0 = pt(o.len * 1.05f, a0, 0.1f), p1 = pt(o.len * 1.05f, a1, 0.1f);
        mud.vtri(c, p1, p0, up, up, up, mid, lip, lip, z, z, z);
    }
    // Pale dead reeds round the far side of the pit: flat mud is hard to see coming, these aren't.
    const PathPt& q = co.pts[o.idx];
    float towards = std::atan2(o.c.z - q.p.z, o.c.x - q.p.x);  // from the trail's middle to the pit
    auto pale = [](Vector3 p, Vector3) {
        float k = 1.15f + 0.1f * std::sin(p.x * 9.0f + p.z * 7.0f);
        return rgb(k, k * 0.98f, k * 0.72f);
    };
    for (int k = 0; k < 9; k++) {
        float a = towards + lerpf(-2.0f, 2.0f, k / 8.0f), rad = o.len + rnd(0.45f, 0.8f);
        for (int s = 0; s < 3; s++) {
            Vector3 foot = pt(rad + rnd(-0.12f, 0.12f), a + rnd(-0.07f, 0.07f), 0);
            float h = rnd(1.3f, 1.9f), lean = rnd(0.1f, 0.35f);
            Vector3 tip = {foot.x + std::cos(a + kPi) * lean + rnd(-0.1f, 0.1f), foot.y + h, foot.z + std::sin(a + kPi) * lean + rnd(-0.1f, 0.1f)};
            tube(reeds, {foot, tip}, {0.06f, 0.02f}, 3, pale, false);
        }
    }
}

// Old bone: yellowed, stained darker by the earth towards the ground, rougher in patches.
Color bone_col(Vector3 p, Vector3 n, float ground) {
    float k = lerpf(0.46f, 0.9f, smooth(0.0f, 1.0f, p.y - ground)) + 0.07f * n.y;
    k *= 0.9f + 0.1f * std::sin(p.x * 3.7f + p.z * 2.9f) * std::sin(p.y * 4.3f + p.z * 1.7f);
    return rgb(k, k * 0.9f, k * 0.72f);
}

// What's left of a huge dead animal, lying along the trail: just the ribcage. The backbone
// bridges it, low enough to duck, a ridge of tall spines along the top. Barrel ribs hang from it,
// biggest in the middle, swept back, curving in again at the tips, and no two alike: some broken
// short, some gone, the two sides out of step, the whole thing leaning a little as it settled.
// (Whole ribs at both ends, so the backbone always stands on something.)
void add_ribs(MeshBuilder& mb, const Course& co, const Obstacle& o, bool low) {
    Vector3 side = {-o.axis.z, 0, o.axis.x};
    float top = kRibsHeadroom + 0.17f, end = o.len * 0.5f, ph = rnd(0, kTau);
    float lean = rnd(0.06f, 0.14f) * (rnd() < 0.5f ? -1.0f : 1.0f);
    auto ground_at = [&](Vector3 p) { return co.ground(p.x, p.z, o.idx); };
    auto spine = [&](float l) {
        Vector3 p = Vector3Add(o.c, Vector3Scale(o.axis, l));
        p.y = ground_at(p) + top + 0.05f * std::sin(l * 0.8f + ph);
        return p;
    };
    auto bone = [&](Vector3 p, Vector3 n) { return bone_col(p, n, ground_at(p)); };
    // The backbone: separate vertebrae, each with its spine.
    int seg = low ? 5 : 7;
    for (float l = -end; l <= end + 0.1f; l += low ? 0.74f : 0.58f) {
        float size = lerpf(1.0f, 0.72f, smooth(end - 2.0f, end, std::fabs(l))) * rnd(0.92f, 1.06f);
        Vector3 p = spine(l), dir = Vector3Normalize(Vector3Subtract(spine(l + 0.2f), spine(l - 0.2f)));
        float half = 0.21f * size;
        tube(mb, {Vector3Subtract(p, Vector3Scale(dir, half * 1.35f)), p, Vector3Add(p, Vector3Scale(dir, half * 1.35f))}, {0.02f, 0.17f * size, 0.02f}, seg, bone, false);
        if (rnd() < 0.08f) continue;  // (snapped off)
        float tall = rnd(0.5f, 0.8f) * size * size * (1.0f + 0.25f * std::sin(l * 0.33f + 1.4f));
        Vector3 tip = Vector3Add(p, Vector3Add(Vector3Scale(dir, tall * 0.42f), {rnd(-0.05f, 0.05f), tall, rnd(-0.05f, 0.05f)}));
        tube(mb, {p, Vector3Lerp(p, tip, 0.55f), tip}, {0.08f * size, 0.06f * size, 0.018f}, 4, bone, false);
    }
    // The ribs.
    int pts = low ? 6 : 8;
    for (int e = -1; e <= 1; e += 2)
        for (float l = -end + rnd(0.0f, 0.25f); l <= end; l += rnd(0.95f, 1.4f)) {
            bool whole = std::fabs(l) > end - 1.3f;
            if (!whole && rnd() < 0.1f) continue;
            float size = 0.8f + 0.2f * std::sin(kPi * (l + end) / o.len), wide = (o.r + 0.62f) * size * rnd(0.95f, 1.05f);
            float last = !whole && rnd() < 0.24f ? rnd(0.45f, 0.78f) : 1.0f, sweep = rnd(0.55f, 0.85f), twist = rnd(-0.1f, 0.1f);
            std::vector<Vector3> c;
            std::vector<float> r;
            for (int i = 0; i <= pts; i++) {
                float t = last * i / pts, out = wide * std::pow(std::sin(t * kPi * 0.63f), 0.8f);
                Vector3 p = Vector3Add(o.c, Vector3Add(Vector3Scale(o.axis, l + sweep * t * t + twist * std::sin(t * 5.0f)), Vector3Scale(side, e * (out + 0.1f))));
                float g = ground_at(p);
                p.y = g + std::max(0.05f, top * (1.0f - std::pow(t, 1.8f)) + e * lean * out * (1.0f - t));
                c.push_back(p);
                r.push_back(lerpf(0.105f, 0.05f, t));
            }
            r.back() = 0.02f;
            tube(mb, c, r, low ? 4 : 5, bone, false);
        }
    // A rib or two come away, lying in the leaves.
    for (int i = 0; i < 3; i++) {
        float l = rnd(-end, end), e = rnd() < 0.5f ? -1.0f : 1.0f, a = e * rnd(2.3f, 3.4f), turn = rnd(0, kTau);
        Vector3 s = Vector3Add(o.c, Vector3Add(Vector3Scale(o.axis, l), Vector3Scale(side, a)));
        Vector3 t = Vector3Add(s, {std::cos(turn) * 1.5f, 0, std::sin(turn) * 1.5f}), m = Vector3Lerp(s, t, 0.5f);
        s.y = ground_at(s) + 0.05f, t.y = ground_at(t) + 0.05f, m.y = ground_at(m) + 0.24f;
        tube(mb, {s, m, t}, {0.08f, 0.07f, 0.04f}, 4, bone, true);
    }
}

// A great leg bone across the trail: a long shaft, flaring at both ends, a ball at one and the
// two knuckles of the knee at the other. To be leapt.
void add_leg_bone(MeshBuilder& mb, const Course& co, const Obstacle& o, bool low) {
    Vector3 side = {-o.axis.z, 0, o.axis.x};
    float flip = rnd() < 0.5f ? -1.0f : 1.0f, half = o.len * 0.5f, r = o.r;
    auto ground_at = [&](Vector3 p) { return co.ground(p.x, p.z, o.idx); };
    auto bone = [&](Vector3 p, Vector3 n) { return bone_col(p, n, ground_at(p) - 0.75f); };
    auto at = [&](float a, float l, float up) {
        Vector3 p = Vector3Add(o.c, Vector3Add(Vector3Scale(o.axis, flip * a), Vector3Scale(side, l)));
        p.y = ground_at(p) + up;
        return p;
    };
    float y = o.c.y - co.pts[o.idx].p.y;
    std::vector<Vector3> c;
    std::vector<float> w;
    for (int i = 0; i <= 10; i++) {
        float t = i / 10.0f * 2 - 1, f = std::fabs(t);  // -1 .. 1 along it
        c.push_back(at(t * half, 0.16f * (1 - t * t), y + 0.03f * std::sin(t * 4.0f)));
        w.push_back(r * lerpf(0.8f, 2.0f, std::pow(f, 3.2f)));
    }
    tube(mb, c, w, low ? 6 : 9, bone, true);
    Vector3 neck = at(half - 0.1f, 0.1f, y + 0.1f), ball = at(half + r * 0.9f, 0.45f, y + r * 1.1f);  // the head of the bone, on its neck
    tube(mb, {neck, Vector3Lerp(neck, ball, 0.5f), ball, Vector3Add(ball, Vector3Scale(Vector3Normalize(Vector3Subtract(ball, neck)), r * 1.5f))}, {r * 1.3f, r * 0.95f, r * 1.9f, r * 0.15f}, low ? 7 : 9, bone, false);
    for (int k = -1; k <= 1; k += 2) {  // the knuckles
        Vector3 s = at(-half + 0.1f, k * r * 0.9f, y + 0.1f), e = at(-half - r * 1.5f, k * r * 1.35f, y + 0.02f);
        tube(mb, {s, Vector3Lerp(s, e, 0.5f), e, Vector3Add(e, Vector3Scale(Vector3Normalize(Vector3Subtract(e, s)), r * 0.9f))}, {r * 1.2f, r * 1.55f, r * 1.2f, r * 0.12f}, low ? 7 : 8, bone, false);
    }
}

// A dead tree, standing at the origin: grey and barkless in places, broken branch stubs, no leaves.
Faller make_faller(int index, const Obstacle& o, bool low) {
    MeshBuilder mb;
    std::vector<Vector3> c;
    std::vector<float> r;
    for (float y : {0.0f, 0.5f, 2.0f, 5.0f, 9.0f, 12.5f, o.len}) {
        c.push_back({0.06f * std::sin(y * 0.5f), y, 0.05f * std::sin(y * 0.3f + 1.0f)});
        r.push_back(o.r * (1.0f + 0.5f * std::exp(-y * 2.0f)) * lerpf(1.0f, 0.45f, y / o.len));
    }
    auto dead = [&](Vector3 p, Vector3 n) {
        float k = 0.62f + 0.18f * std::sin(p.y * 2.3f + n.x * 3.0f) + 0.15f * smooth(0.0f, 1.5f, p.y);
        return rgb(k, k * 0.96f, k * 0.92f);
    };
    tube(mb, c, r, low ? 7 : 9, dead, true);
    for (int k = 0; k < 5; k++) {  // broken branches
        float y = rnd(5.0f, o.len - 1.5f), a = rnd(0, kTau), l = rnd(0.8f, 2.2f);
        Vector3 s = {std::cos(a) * o.r * 0.6f, y, std::sin(a) * o.r * 0.6f};
        Vector3 e = Vector3Add(s, {std::cos(a) * l, l * 0.6f, std::sin(a) * l});
        tube(mb, {s, e}, {o.r * 0.25f, o.r * 0.12f}, 5, dead, true);
    }
    Faller f;
    f.obstacle = index;
    f.trunk = mb.build();
    f.base = o.c;
    f.axis = o.axis;
    f.lift = 0.1f + o.r * 0.85f;
    return f;
}

}  // namespace

Mesh make_boulder(float r) {
    MeshBuilder mb;
    Obstacle o{};
    o.kind = ObKind::Rock;
    o.c = {0, r * 0.3f, 0};
    o.r = r;
    add_rock(mb, o);
    return mb.build();
}

Mesh make_dead_tree(float r, float len, bool low) {
    Obstacle o{};
    o.kind = ObKind::Faller;
    o.axis = {1, 0, 0};
    o.r = r, o.len = len;
    return make_faller(0, o, low).trunk;
}

void World::build(const Course& co, bool low) {
    course = &co;
    int npts = (int)co.pts.size();
    int nchunks = (npts + CHUNK - 1) / CHUNK;
    chunks.assign(nchunks, Chunk{});
    std::vector<MeshBuilder> mb((size_t)nchunks * Chunk::LayerCount);
    auto B = [&](int idx, int layer) -> MeshBuilder& {
        int ch = clampf(idx / CHUNK, 0, nchunks - 1);
        return mb[(size_t)ch * Chunk::LayerCount + layer];
    };

    // Terrain: a grid over everything within reach of the trail. Each vertex remembers the
    // nearest trail sample (found by stamping a disc round every sample).
    const float CELL = low ? 3.0f : 2.0f, REACH = 44.0f;
    float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
    for (const PathPt& p : co.pts) minx = std::min(minx, p.p.x), maxx = std::max(maxx, p.p.x), minz = std::min(minz, p.p.z), maxz = std::max(maxz, p.p.z);
    minx -= REACH + CELL * 2, minz -= REACH + CELL * 2, maxx += REACH + CELL * 2, maxz += REACH + CELL * 2;
    int gw = (int)((maxx - minx) / CELL) + 2, gh = (int)((maxz - minz) / CELL) + 2;
    std::vector<float> gd((size_t)gw * gh, 1e9f), gy((size_t)gw * gh, 0);
    std::vector<int> gi((size_t)gw * gh, -1);
    int rad = (int)(REACH / CELL) + 2;
    for (int i = 0; i < npts; i += 1) {
        const PathPt& p = co.pts[i];
        int cx = (int)((p.p.x - minx) / CELL), cz = (int)((p.p.z - minz) / CELL);
        for (int z = std::max(0, cz - rad); z <= std::min(gh - 1, cz + rad); z++)
            for (int x = std::max(0, cx - rad); x <= std::min(gw - 1, cx + rad); x++) {
                float wx = minx + x * CELL, wz = minz + z * CELL;
                float d = std::sqrt((wx - p.p.x) * (wx - p.p.x) + (wz - p.p.z) * (wz - p.p.z));
                size_t k = (size_t)z * gw + x;
                if (d < gd[k]) gd[k] = d, gi[k] = i;
            }
    }
    for (int z = 0; z < gh; z++)
        for (int x = 0; x < gw; x++) {
            size_t k = (size_t)z * gw + x;
            float wx = minx + x * CELL, wz = minz + z * CELL;
            gy[k] = gi[k] >= 0 ? co.ground(wx, wz, gi[k]) : co.raw_height(wx, wz);
        }
    auto H = [&](int x, int z) { return gy[(size_t)clampf(z, 0, gh - 1) * gw + (size_t)clampf(x, 0, gw - 1)]; };
    auto vtx = [&](int x, int z, Vector3& p, Vector3& n, Color& c, Vector2& uv) {
        float wx = minx + x * CELL, wz = minz + z * CELL;
        p = {wx, H(x, z), wz};
        n = Vector3Normalize({H(x - 1, z) - H(x + 1, z), 2 * CELL, H(x, z - 1) - H(x, z + 1)});
        float v = fbm(wx * 0.04f, wz * 0.04f, 3, 5);
        float wet = smooth(0.55f, 0.75f, fbm(wx * 0.02f + 50, wz * 0.02f, 3, 6));
        Vector3 k = Vector3Lerp({1.05f, 1.0f, 0.92f}, {0.78f, 0.86f, 0.70f}, v);
        k = Vector3Scale(k, 1.0f - 0.25f * wet);
        c = rgb(k.x, k.y, k.z);
        uv = {wx / 4.0f, wz / 4.0f};
    };
    for (int z = 0; z + 1 < gh; z++)
        for (int x = 0; x + 1 < gw; x++) {
            size_t k = (size_t)z * gw + x;
            if (gd[k] > REACH || gd[k + 1] > REACH || gd[k + gw] > REACH || gd[k + gw + 1] > REACH) continue;
            MeshBuilder& m = B(gi[k], Chunk::Terrain);
            Vector3 p[4], n[4];
            Color c[4];
            Vector2 uv[4];
            vtx(x, z, p[0], n[0], c[0], uv[0]);
            vtx(x + 1, z, p[1], n[1], c[1], uv[1]);
            vtx(x, z + 1, p[2], n[2], c[2], uv[2]);
            vtx(x + 1, z + 1, p[3], n[3], c[3], uv[3]);
            m.vtri(p[0], p[2], p[1], n[0], n[2], n[1], c[0], c[2], c[1], uv[0], uv[2], uv[1]);
            m.vtri(p[1], p[2], p[3], n[1], n[2], n[3], c[1], c[2], c[3], uv[1], uv[2], uv[3]);
        }

    // The trail: a strip a little wider than the trail, its ragged edge cut out by the texture.
    const int ACROSS = low ? 3 : 6;
    for (int i = 0; i + 1 < npts; i++) {
        MeshBuilder& m = B(i, Chunk::Trail);
        const PathPt &a = co.pts[i], &b = co.pts[i + 1];
        Vector3 ra = heading_right(a.yaw), rb = heading_right(b.yaw);
        Vector3 up = {0, 1, 0};
        for (int k = 0; k < ACROSS; k++) {
            float u0 = (float)k / ACROSS, u1 = (float)(k + 1) / ACROSS;
            auto P = [&](const PathPt& q, Vector3 r, float u) {
                Vector3 p = Vector3Add(q.p, Vector3Scale(r, (u - 0.5f) * 2.0f * q.half * 1.25f));
                p.y = q.p.y + 0.04f;
                return p;
            };
            Vector3 p00 = P(a, ra, u0), p10 = P(a, ra, u1), p01 = P(b, rb, u0), p11 = P(b, rb, u1);
            Color c = WHITE;
            Vector2 t00 = {u0, a.s / 6.0f}, t10 = {u1, a.s / 6.0f}, t01 = {u0, b.s / 6.0f}, t11 = {u1, b.s / 6.0f};
            m.vtri(p00, p01, p10, up, up, up, c, c, c, t00, t01, t10);
            m.vtri(p10, p01, p11, up, up, up, c, c, c, t10, t01, t11);
        }
    }

    for (const Tree& t : co.trees) {
        add_tree(B(t.idx, Chunk::Bark), B(t.idx, Chunk::Leaves), co, t, low);
        chunks[(int)clampf(t.idx / CHUNK, 0, nchunks - 1)].spots.push_back({t.p.x, t.p.z});
    }

    for (int oi = 0; oi < (int)co.obstacles.size(); oi++) {
        const Obstacle& o = co.obstacles[oi];
        if (o.kind == ObKind::Rock) {
            add_rock(B(o.idx, Chunk::Rock), o);
            continue;
        }
        if (o.kind == ObKind::Creek || o.kind == ObKind::Mud) {
            add_band(B(o.idx, Chunk::Water), B(o.idx, Chunk::Terrain), o);
            continue;
        }
        if (o.kind == ObKind::Faller) {
            fallers.push_back(make_faller(oi, o, low));
            continue;
        }
        if (o.kind == ObKind::Bramble) {
            add_bramble(B(o.idx, Chunk::Bark), B(o.idx, Chunk::Leaves), co, o, low);
            continue;
        }
        if (o.kind == ObKind::Gate) {
            add_gate(B(o.idx, Chunk::Bark), co, o, low);
            continue;
        }
        if (o.kind == ObKind::Jam) {
            add_jam(B(o.idx, Chunk::Water), B(o.idx, Chunk::Bark), B(o.idx, Chunk::Terrain), co, o, low);
            continue;
        }
        if (o.kind == ObKind::Sinkhole) {
            add_sinkhole(B(o.idx, Chunk::Terrain), B(o.idx, Chunk::Bark), co, o);
            continue;
        }
        if (o.kind == ObKind::Ribs) {
            add_ribs(B(o.idx, Chunk::Water), co, o, low);  // (plain colour: bone has no bark or stone in it)
            continue;
        }
        if (o.kind == ObKind::Log && o.gap > 0) {
            add_leg_bone(B(o.idx, Chunk::Water), co, o, low);
            continue;
        }
        if (o.kind == ObKind::Roller) {
            MeshBuilder rock;
            Obstacle at_origin = o;
            at_origin.c = {0, 0, 0};
            add_rock(rock, at_origin);
            rollers.push_back({oi, rock.build()});
            continue;
        }
        // Logs and branches: a slightly crooked tube, mossy on top; branches carry leaves.
        std::vector<Vector3> c;
        std::vector<float> r;
        int segs = 6;
        for (int k = 0; k <= segs; k++) {
            float t = (float)k / segs - 0.5f;
            Vector3 p = Vector3Add(o.c, Vector3Scale(o.axis, t * o.len));
            p.y += (o.kind == ObKind::Branch ? -0.25f * (1.0f - 4.0f * t * t) + 0.15f : 0.0f) + 0.04f * std::sin(t * 9.0f);
            if (o.kind == ObKind::Log) p.y = std::min(p.y, co.ground(p.x, p.z, o.idx) + o.r * 0.85f);
            c.push_back(p);
            r.push_back(o.r * (1.0f - 0.25f * std::fabs(t)));
        }
        tube(B(o.idx, Chunk::Bark), c, r, 9,
             [&](Vector3, Vector3 n) {
                 float moss = smooth(0.2f, 0.8f, n.y);
                 float ao = 0.6f + 0.4f * smooth(-0.6f, 0.4f, n.y);
                 Vector3 k = Vector3Lerp({0.85f, 0.78f, 0.70f}, {0.42f, 0.60f, 0.22f}, moss * (o.kind == ObKind::Log ? 0.9f : 0.5f));
                 return rgb(k.x * ao, k.y * ao, k.z * ao);
             },
             o.kind == ObKind::Log);
        if (o.kind == ObKind::Branch)
            for (int k = 0; k < 5; k++) {
                float t = rnd(-0.45f, 0.45f);
                Vector3 p = Vector3Add(o.c, Vector3Scale(o.axis, t * o.len));
                p.y -= 0.6f;
                add_card_cluster(B(o.idx, Chunk::Leaves), p, 1.3f, 1.0f, 2, rnd(0, kTau), 0.6f);
            }
    }

    for (const Plant& p : co.plants) {
        switch (p.kind) {
            case PlantKind::Fern: add_fern(B(p.idx, Chunk::Fern), p, low); break;
            case PlantKind::Grass: add_card_cluster(B(p.idx, Chunk::Grass), p.p, p.size * 1.4f, p.size * 0.7f, 3, p.yaw, 0.7f); break;
            case PlantKind::Bush: add_card_cluster(B(p.idx, Chunk::Leaves), {p.p.x, p.p.y - 0.2f, p.p.z}, p.size * 1.3f, p.size, 4, p.yaw, 0.55f); break;
        }
    }

    for (int ch = 0; ch < nchunks; ch++) {
        Chunk& C = chunks[ch];
        int i0 = ch * CHUNK, i1 = std::min(npts - 1, i0 + CHUNK);
        C.center = Vector3Scale(Vector3Add(co.pts[i0].p, co.pts[i1].p), 0.5f);
        C.half_len = Vector3Distance(co.pts[i0].p, co.pts[i1].p) * 0.5f;
        C.radius = C.half_len + 34.0f;  // (trees reach 34 m from the trail)
        for (int l = 0; l < Chunk::LayerCount; l++) {
            MeshBuilder& m = mb[(size_t)ch * Chunk::LayerCount + l];
            if (m.empty()) continue;
            C.mesh[l] = m.build();
            C.has[l] = true;
        }
    }
    if (getenv("RR_LAYERS")) {  // (diagnostics) triangles per layer over the whole course
        static const char* names[] = {"terrain", "trail", "bark", "rock", "fern", "leaves", "grass", "water"};
        for (int l = 0; l < Chunk::LayerCount; l++) {
            long n = 0;
            long v = 0;
            for (const Chunk& c : chunks) n += c.has[l] ? c.mesh[l].triangleCount : 0, v += c.has[l] ? c.mesh[l].vertexCount : 0;
            printf("[layers] %-8s %7ld triangles, %7ld vertices, %5.0f triangles per chunk\n", names[l], n, v, (double)n / chunks.size());
        }
    }

    // The snout, in head space (x right, y up, z back): the top of a long upper jaw reaching
    // forward and down from below the eyes to a blunt, rounded tip.
    MeshBuilder sm, tm;
    const int RINGS = 16, AROUND = 14;
    auto section = [&](float f, float& cy, float& w, float& h) {
        float taper = std::pow(clampf(1.0f - f, 0, 1), 0.55f);
        cy = -0.315f - 0.09f * f;  // only the last part, near the tip, comes into view
        w = 0.19f * (0.25f + 0.75f * taper);
        h = 0.12f * (0.4f + 0.6f * taper);
        if (f > 0.9f) {  // round off the tip
            float k = (f - 0.9f) / 0.1f;
            float round = std::sqrt(std::max(0.0f, 1.0f - k * k));
            w *= round;
            h *= 0.4f + 0.6f * round;
        }
    };
    auto spt = [&](int i, int k, Vector3& p, Vector3& n, Vector2& uv) {
        float f = 0.08f + 0.92f * (float)i / RINGS;
        float th = -0.45f + (kPi + 0.9f) * k / AROUND;  // from the right jaw line over the top to the left
        float cy, w, h;
        section(f, cy, w, h);
        float flat = std::sin(th) > 0 ? 0.8f : 1.0f;  // flatter on top
        p = {w * std::cos(th), cy + h * std::sin(th) * flat, -0.12f - 0.95f * f};
        n = Vector3Normalize({std::cos(th) / std::max(w, 0.01f), std::sin(th) / std::max(h * flat, 0.01f), -0.15f - f * 0.3f});
        uv = {(th + 0.45f) / (kPi + 0.9f) * 0.5f, f * 3.0f};
    };
    for (int i = 0; i < RINGS; i++)
        for (int k = 0; k < AROUND; k++) {
            Vector3 p[4], n[4];
            Vector2 uv[4];
            spt(i, k, p[0], n[0], uv[0]);
            spt(i, k + 1, p[1], n[1], uv[1]);
            spt(i + 1, k, p[2], n[2], uv[2]);
            spt(i + 1, k + 1, p[3], n[3], uv[3]);
            Color c = WHITE;
            sm.vtri(p[0], p[2], p[1], n[0], n[2], n[1], c, c, c, uv[0], uv[2], uv[1]);
            sm.vtri(p[1], p[2], p[3], n[1], n[2], n[3], c, c, c, uv[1], uv[2], uv[3]);
        }
    // Nostrils: two dark, sunken slits near the tip.
    for (int s = -1; s <= 1; s += 2) {
        float f = 0.925f, cy, w, h;
        section(f, cy, w, h);
        Vector3 at = {s * w * 0.5f, cy + h * 0.8f * 0.83f, -0.12f - 0.95f * f};
        sm.sphere(0.0065f, 0.004f, 8, 3, xform(at, {0, s * 0.35f, 0}, {1.0f, 1.0f, 2.2f}), rgb(0.16f, 0.12f, 0.08f));
    }
    // Teeth along both jaw lines, longer towards the back.
    for (int s = -1; s <= 1; s += 2)
        for (float f = 0.22f; f < 0.86f; f += 0.055f) {
            float cy, w, h;
            section(f, cy, w, h);
            float len = 0.022f + 0.02f * (1.0f - f);
            Vector3 at = {s * w * 0.93f, cy - h * 0.38f - len * 0.5f, -0.12f - 0.95f * f};
            tm.cylinder(0.0f, 0.0065f, len, 6, xform(at, {0, 0, s * 0.25f}), rgb(0.86f, 0.82f, 0.70f, 0.3f));
        }
    snout = sm.build();
    teeth = tm.build();
}

void World::draw(const Camera3D& cam, const DrawRange& range, const std::vector<float>& fall, const Biome& tint) {
    drawn = 0;
    triangles = 0;
    // Chunks within reach and inside the view's horizontal angle (with room for their size).
    std::vector<int> vis;
    std::vector<float> along;  // distance to the nearest part of each visible chunk
    Vector3 look = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector2 flat_look = Vector2Normalize({look.x, look.z});
    const float half_view = 40.0f * DEG2RAD;
    for (int i = 0; i < (int)chunks.size(); i++) {
        const Chunk& c = chunks[i];
        Vector3 d = Vector3Subtract(c.center, cam.position);
        float dist = Vector3Length(d);
        // How far off its nearest part is. On a bend the trees beside a stretch of trail can be
        // much nearer than the trail itself, so go by the trees: they mustn't appear in plain view.
        float nearest = dist - c.half_len;
        if (nearest - 34.0f > range.far) continue;
        if (nearest > 0) {
            float best = nearest * nearest;
            for (const Vector2& s : c.spots) {
                float dx = s.x - cam.position.x, dz = s.y - cam.position.z;
                best = std::min(best, dx * dx + dz * dz);
            }
            nearest = std::sqrt(best);
        }
        if (nearest > range.far) continue;
        if (dist > c.radius) {
            Vector2 fd = Vector2Normalize({d.x, d.z});
            float angle = std::acos(clampf(Vector2DotProduct(fd, flat_look), -1, 1));
            if (angle > half_view + std::asin(clampf(c.radius / dist, 0, 1))) continue;
        }
        vis.push_back(i);
        along.push_back(std::max(0.0f, nearest));
    }
    const Textures& t = gfx.tex;
    for (int i : vis) {
        Chunk& c = chunks[i];
        Matrix id = MatrixIdentity();
        if (c.has[Chunk::Terrain]) gfx.draw(c.mesh[Chunk::Terrain], id, t.floor, tint.ground);
        if (c.has[Chunk::Bark]) gfx.draw(c.mesh[Chunk::Bark], id, t.bark, tint.bark);
        if (c.has[Chunk::Rock]) gfx.draw(c.mesh[Chunk::Rock], id, t.rock, tint.bark);
        if (c.has[Chunk::Water]) gfx.draw(c.mesh[Chunk::Water], id, t.white, WHITE, {0.02f, 0.03f, 0.035f});
        for (int l = 0; l < Chunk::LayerCount; l++)
            if (c.has[l]) triangles += c.mesh[l].triangleCount;
    }
    for (size_t n = 0; n < vis.size(); n++) {
        Chunk& c = chunks[vis[n]];
        Matrix id = MatrixIdentity();
        bool plants = along[n] <= range.plants, canopy = along[n] <= range.canopy;
        if (c.has[Chunk::Trail]) gfx.draw_cut(c.mesh[Chunk::Trail], id, t.path);
        if (plants && c.has[Chunk::Fern]) gfx.draw_cut(c.mesh[Chunk::Fern], id, t.fern, tint.leaves, range.plants);
        if (plants && c.has[Chunk::Grass]) gfx.draw_cut(c.mesh[Chunk::Grass], id, t.grass, tint.leaves, range.plants);
        if (canopy && c.has[Chunk::Leaves]) gfx.draw_cut(c.mesh[Chunk::Leaves], id, t.leaves, tint.leaves, range.canopy);
        if (!plants) triangles -= (c.has[Chunk::Fern] ? c.mesh[Chunk::Fern].triangleCount : 0) + (c.has[Chunk::Grass] ? c.mesh[Chunk::Grass].triangleCount : 0);
        if (!canopy && c.has[Chunk::Leaves]) triangles -= c.mesh[Chunk::Leaves].triangleCount;
    }
    // Falling trees: pivot at the base towards `axis`, settling half sunk once down.
    for (const Faller& f : fallers) {
        if (Vector3Distance(f.base, cam.position) > range.far + 16.0f) continue;
        float th = f.obstacle < (int)fall.size() ? fall[f.obstacle] : 0.0f;
        Vector3 k = Vector3Normalize(Vector3CrossProduct({0, 1, 0}, f.axis));
        Matrix m = compose(MatrixRotate(k, th), MatrixTranslate(f.base.x, f.base.y + f.lift * std::sin(th), f.base.z));
        gfx.draw(f.trunk, m, t.bark, tint.bark);
        triangles += f.trunk.triangleCount;
    }
    // Rolling boulders, turning as they go.
    for (const Roller& r : rollers) {
        const Obstacle& o = course->obstacles[r.obstacle];
        float t = r.obstacle < (int)fall.size() ? fall[r.obstacle] : 0.0f;
        Vector3 p = course->roller_at(o, t);
        if (Vector3Distance(p, cam.position) > range.far) continue;
        float rolled = o.len * t / o.r;
        Matrix m = compose(MatrixRotate(Vector3CrossProduct({0, 1, 0}, o.axis), rolled), MatrixTranslate(p.x, p.y, p.z));
        gfx.draw(r.rock, m, gfx.tex.rock, tint.bark);
        triangles += r.rock.triangleCount;
    }
    drawn = (int)vis.size();
}

void World::draw_snout(const Matrix& head) {
    gfx.draw(snout, head, gfx.tex.skin, WHITE, {0.05f, 0.045f, 0.03f});  // (a little light of its own, so it reads in shade)
    gfx.draw(teeth, head, gfx.tex.white);
}

void World::unload() {
    for (Chunk& c : chunks)
        for (int l = 0; l < Chunk::LayerCount; l++)
            if (c.has[l]) UnloadMesh(c.mesh[l]);
    for (Faller& f : fallers) UnloadMesh(f.trunk);
    fallers.clear();
    for (Roller& r : rollers) UnloadMesh(r.rock);
    rollers.clear();
    UnloadMesh(snout);
    UnloadMesh(teeth);
}

}  // namespace rr
