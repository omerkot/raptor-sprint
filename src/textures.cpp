// Textures painted in code at startup. Everything aims at natural colour: muted, varied, with
// fine detail, rather than flat fills.
#include "gfx.hpp"
#include "noise.hpp"

namespace rr {

namespace {

struct Canvas {
    int w, h;
    std::vector<Vector3> c;
    std::vector<float> a;
    Canvas(int w_, int h_, Vector3 fill = {0, 0, 0}, float alpha = 1.0f) : w(w_), h(h_), c(w_ * h_, fill), a(w_ * h_, alpha) {}
    Vector3& at(int x, int y) { return c[wrapi(y, h) * w + wrapi(x, w)]; }
    float& alpha(int x, int y) { return a[wrapi(y, h) * w + wrapi(x, w)]; }
};

Vector3 mix3(Vector3 a, Vector3 b, float t) { return Vector3Lerp(a, b, clampf(t, 0, 1)); }
Vector3 mul3(Vector3 a, float f) { return Vector3Scale(a, f); }

// A filled, rotated ellipse with soft shading, blended over the canvas (wrapping round the edges).
// `rib` darkens a midrib along the long axis (leaves).
void stamp_leaf(Canvas& cv, float cx, float cy, float rx, float ry, float ang, Vector3 col, float rib, float alpha_to = -1) {
    int r = (int)std::ceil(std::max(rx, ry)) + 1;
    float ca = std::cos(ang), sa = std::sin(ang);
    for (int y = (int)cy - r; y <= (int)cy + r; y++)
        for (int x = (int)cx - r; x <= (int)cx + r; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float u = (dx * ca + dy * sa) / rx, v = (-dx * sa + dy * ca) / ry;
            // A leaf: pointed at both ends.
            float half = std::sqrt(std::max(0.0f, 1.0f - u * u)) * (1.0f - 0.35f * std::fabs(u));
            if (std::fabs(v) > half) continue;
            float edge = 1.0f - std::fabs(v) / std::max(half, 1e-3f);
            Vector3 k = mul3(col, 0.82f + 0.25f * edge - (rib > 0 && std::fabs(v) < 0.12f ? rib : 0.0f) + 0.1f * u);
            cv.at(x, y) = k;
            if (alpha_to >= 0) cv.alpha(x, y) = alpha_to;
        }
}

void stamp_disc(Canvas& cv, float cx, float cy, float r, Vector3 col, float alpha_to = -1) {
    int R = (int)std::ceil(r) + 1;
    for (int y = (int)cy - R; y <= (int)cy + R; y++)
        for (int x = (int)cx - R; x <= (int)cx + R; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            if (dx * dx + dy * dy > r * r) continue;
            cv.at(x, y) = col;
            if (alpha_to >= 0) cv.alpha(x, y) = alpha_to;
        }
}

uint32_t g_seed = 12345;
float rnd(float lo = 0, float hi = 1) {
    g_seed = g_seed * 1664525u + 1013904223u;
    return lo + (hi - lo) * ((g_seed >> 8) / 16777215.0f);
}

// Cut-out textures lose their shapes in the smaller mipmaps as alpha averages away; lift alpha
// level by level so distant leaves keep roughly the same coverage.
Texture2D upload(const Canvas& cv, bool repeat, bool cutout = false, bool mipmaps = true) {
    std::vector<Color> px(cv.w * cv.h);
    Vector3 avg = {0, 0, 0};
    float n = 0;
    for (int i = 0; i < cv.w * cv.h; i++)
        if (cv.a[i] > 0.5f) avg = Vector3Add(avg, cv.c[i]), n++;
    if (n > 0) avg = Vector3Scale(avg, 1.0f / n);
    for (int i = 0; i < cv.w * cv.h; i++) {
        // Transparent pixels take the average colour, so filtering doesn't fringe leaves with black.
        Vector3 k = (cutout && cv.a[i] <= 0.5f) ? avg : cv.c[i];
        px[i] = rgb(k.x, k.y, k.z);
        px[i].a = (unsigned char)(clampf(cv.a[i], 0, 1) * 255);
    }
    Image img = {px.data(), cv.w, cv.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    Image copy = ImageCopy(img);
    if (mipmaps) {
        ImageMipmaps(&copy);
        if (cutout) {
            unsigned char* d = (unsigned char*)copy.data;
            int w = cv.w, h = cv.h;
            size_t off = 0;
            for (int l = 0; l < copy.mipmaps; l++) {
                if (l > 0) {
                    float boost = 1.0f + 0.45f * l;
                    for (int i = 0; i < w * h; i++) d[off + i * 4 + 3] = (unsigned char)std::min(255.0f, d[off + i * 4 + 3] * boost);
                }
                off += (size_t)w * h * 4;
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
            }
        }
    }
    Texture2D t = LoadTextureFromImage(copy);
    UnloadImage(copy);
    SetTextureFilter(t, mipmaps ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(t, repeat ? TEXTURE_WRAP_REPEAT : TEXTURE_WRAP_CLAMP);
    return t;
}

// Leaf litter over dark soil, with moss, twigs and a few pale stones. Tiles every 4 m.
Texture2D paint_floor(int N) {
    Canvas cv(N, N);
    float s = N / 512.0f;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float u = (float)x / N, v = (float)y / N;
            float n = pfbm(u * 8, v * 8, 8, 8, 4, 1);
            cv.at(x, y) = mix3({0.10f, 0.075f, 0.05f}, {0.22f, 0.17f, 0.11f}, n);
        }
    const Vector3 leafc[] = {{0.42f, 0.28f, 0.13f}, {0.33f, 0.20f, 0.10f}, {0.50f, 0.37f, 0.18f},
                             {0.28f, 0.24f, 0.12f}, {0.38f, 0.33f, 0.17f}, {0.24f, 0.15f, 0.08f}};
    for (int i = 0; i < (int)(2600 * s * s); i++) {
        Vector3 c = mul3(leafc[(int)rnd(0, 5.99f)], rnd(0.75f, 1.15f));
        float L = rnd(5, 11) * s;
        stamp_leaf(cv, rnd(0, N), rnd(0, N), L, L * rnd(0.35f, 0.55f), rnd(0, kTau), c, 0.12f);
    }
    for (int i = 0; i < (int)(70 * s); i++) {  // twigs
        float x = rnd(0, N), y = rnd(0, N), ang = rnd(0, kTau), len = rnd(15, 45) * s;
        Vector3 c = mul3({0.20f, 0.14f, 0.09f}, rnd(0.8f, 1.2f));
        for (float t = 0; t < len; t += 0.5f) stamp_disc(cv, x + std::cos(ang) * t, y + std::sin(ang) * t, 0.9f * s + 0.3f, c);
    }
    for (int y = 0; y < N; y++)  // moss in patches, and shade in the hollows
        for (int x = 0; x < N; x++) {
            float u = (float)x / N, v = (float)y / N;
            float m = smooth(0.58f, 0.72f, pfbm(u * 4, v * 4, 4, 4, 5, 7));
            float fine = pnoise(u * 160, v * 160, 160, 160, 9);
            Vector3& k = cv.at(x, y);
            k = mix3(k, mul3({0.20f, 0.27f, 0.09f}, 0.7f + 0.5f * fine), m * 0.85f);
            k = mul3(k, 0.8f + 0.35f * pfbm(u * 6, v * 6, 6, 6, 3, 3));
        }
    return upload(cv, true);
}

// Packed earth for the trail: u runs across it, v along it (tiles every 6 m along). Ragged,
// mossy edges are cut out by alpha.
Texture2D paint_path(int N) {
    int W = N / 2, H = N;
    Canvas cv(W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float u = (x + 0.5f) / W, v = (y + 0.5f) / H;
            float n = pfbm(u * 3, v * 6, 0, 6, 5, 21), fine = pnoise(u * 90, v * 180, 0, 180, 22);
            Vector3 k = mix3({0.25f, 0.19f, 0.13f}, {0.42f, 0.33f, 0.23f}, n * 0.8f + fine * 0.2f);
            // Two worn ruts, darker and damp.
            for (float rc : {0.36f, 0.64f}) {
                float d = std::fabs(u - rc + 0.04f * (pfbm(0, v * 6, 0, 6, 3, 23) - 0.5f));
                k = mul3(k, 1.0f - 0.25f * (1.0f - smooth(0.0f, 0.09f, d)));
            }
            float e = std::fabs(u - 0.5f) * 2.0f;
            float rag = 0.80f + 0.16f * (pfbm(u * 2, v * 10, 0, 10, 4, 24) - 0.5f) * 2.0f;
            k = mix3(k, {0.17f, 0.18f, 0.08f}, smooth(rag - 0.25f, rag, e) * 0.8f);  // moss creeps in at the edges
            cv.at(x, y) = k;
            cv.alpha(x, y) = e < rag ? 1.0f : 0.0f;
        }
    for (int i = 0; i < N * 2; i++) {  // pebbles
        float x = rnd(0, W), y = rnd(0, H), r = rnd(0.6f, 2.2f) * N / 512.0f + 0.4f;
        if (cv.alpha((int)x, (int)y) < 0.5f) continue;
        float g = rnd(0.35f, 0.6f);
        stamp_disc(cv, x + 0.6f, y + 0.8f, r, mul3(cv.at((int)x, (int)y), 0.6f));
        stamp_disc(cv, x, y, r, {g, g * 0.95f, g * 0.88f});
    }
    for (int i = 0; i < N / 3; i++) {  // fallen leaves, mostly near the edges
        float u = rnd() < 0.7f ? (rnd() < 0.5f ? rnd(0.0f, 0.25f) : rnd(0.75f, 1.0f)) : rnd(0, 1);
        float x = u * W, y = rnd(0, H), L = rnd(4, 8) * N / 512.0f;
        if (cv.alpha((int)x, (int)y) < 0.5f) continue;
        stamp_leaf(cv, x, y, L, L * 0.45f, rnd(0, kTau), mul3({0.40f, 0.27f, 0.12f}, rnd(0.7f, 1.1f)), 0.1f);
    }
    return upload(cv, true, true);
}

// Bark with deep vertical fissures, a little lichen. u wraps round the trunk, v runs up it (3 m).
Texture2D paint_bark(int N) {
    int W = N / 2, H = N;
    Canvas cv(W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float u = (float)x / W, v = (float)y / H;
            float ridge = pfbm(u * 10, v * 2, 10, 2, 4, 31);
            float fiss = 1.0f - smooth(0.0f, 0.18f, std::fabs(ridge - 0.5f));
            float plate = pnoise(u * 20, v * 10, 20, 10, 32);
            Vector3 k = mix3({0.26f, 0.22f, 0.18f}, {0.40f, 0.36f, 0.30f}, plate * 0.6f + pnoise(u * 80, v * 60, 80, 60, 33) * 0.4f);
            k = mul3(k, 1.0f - fiss * 0.7f);
            float lichen = smooth(0.66f, 0.74f, pfbm(u * 6, v * 4, 6, 4, 4, 34)) * (1.0f - fiss);
            k = mix3(k, {0.50f, 0.54f, 0.42f}, lichen * 0.7f);
            cv.at(x, y) = k;
        }
    return upload(cv, true);
}

// Grey granite with dark speckles and lichen.
Texture2D paint_rock(int N) {
    Canvas cv(N, N);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float u = (float)x / N, v = (float)y / N;
            float n = pfbm(u * 5, v * 5, 5, 5, 5, 41);
            Vector3 k = mix3({0.30f, 0.30f, 0.28f}, {0.55f, 0.54f, 0.50f}, n);
            float sp = hash2(x, y, 42);
            if (sp > 0.93f) k = mul3(k, 0.55f);
            else if (sp < 0.04f) k = mul3(k, 1.25f);
            float lichen = smooth(0.62f, 0.68f, pfbm(u * 9, v * 9, 9, 9, 3, 43));
            k = mix3(k, {0.42f, 0.47f, 0.30f}, lichen * 0.6f);
            float moss = smooth(0.6f, 0.75f, pfbm(u * 3, v * 3, 3, 3, 4, 44));
            k = mix3(k, {0.16f, 0.24f, 0.08f}, moss * 0.8f);
            cv.at(x, y) = k;
        }
    return upload(cv, true);
}

// One fern frond on a transparent ground: a curving stem with leaflets that shorten to the tip.
// The frond grows from the bottom centre towards the top.
Texture2D paint_fern(int N) {
    Canvas cv(N, N, {0.2f, 0.3f, 0.1f}, 0.0f);
    float s = N / 256.0f;
    const int STEPS = 300;
    for (int i = 0; i < STEPS; i++) {
        float t = (float)i / STEPS;
        float sx = N * (0.5f + 0.06f * std::sin(t * 2.5f)), sy = N * (1.0f - t * 0.97f);
        Vector3 stem = {0.22f, 0.30f, 0.10f};
        stamp_disc(cv, sx, sy, (1.6f - t) * s, stem, 1.0f);
        if (i % 11 != 0 || t < 0.06f) continue;
        float len = N * 0.40f * std::sin(std::min(1.0f, t * 1.15f) * kPi * 0.92f + 0.12f) * (1.0f - t * 0.55f);
        for (int side = -1; side <= 1; side += 2) {
            float ang = -kPi / 2 + side * (1.2f - 0.3f * t);
            float ca = std::cos(ang), sa = std::sin(ang);
            for (float d = 0; d < len; d += 1.0f) {
                float k = d / len;
                float w = (1.9f * (1.0f - k) + 0.45f) * s * (1.0f + 0.3f * std::sin(d * 1.3f / s));  // lobed edge
                float px = sx + ca * d - side * 0.0f, py = sy + sa * d + d * d * 0.004f / s;
                Vector3 c = mix3({0.13f, 0.27f, 0.07f}, {0.36f, 0.48f, 0.15f}, k * 0.7f + hash2(i, (int)d, 51) * 0.15f);
                stamp_disc(cv, px, py, w, c, 1.0f);
            }
        }
    }
    return upload(cv, false, true);
}

// A spray of leaves on a transparent ground (canopy cards and bushes).
Texture2D paint_leaves(int N) {
    Canvas cv(N, N, {0.15f, 0.25f, 0.08f}, 0.0f);
    float s = N / 256.0f;
    for (int i = 0; i < (int)(420 * s * s); i++) {
        float ang = rnd(0, kTau), r = std::sqrt(rnd()) * N * 0.44f;
        float x = N * 0.5f + std::cos(ang) * r, y = N * 0.5f + std::sin(ang) * r;
        float L = rnd(7, 13) * s;
        Vector3 c = mix3({0.10f, 0.20f, 0.05f}, {0.32f, 0.44f, 0.12f}, rnd());
        int R = (int)(L + 2);
        if (x < R || y < R || x > N - R || y > N - R) continue;
        stamp_leaf(cv, x, y, L, L * 0.42f, rnd(0, kTau), c, 0.1f, 1.0f);
    }
    return upload(cv, false, true);
}

// Grass blades for tufts at the trail's edge.
Texture2D paint_grass(int N) {
    Canvas cv(N, N / 2, {0.2f, 0.3f, 0.1f}, 0.0f);
    int H = N / 2;
    for (int i = 0; i < N / 3; i++) {
        float x = rnd(N * 0.05f, N * 0.95f), lean = rnd(-0.5f, 0.5f), len = rnd(0.5f, 0.98f) * H;
        Vector3 c = mix3({0.16f, 0.26f, 0.07f}, {0.45f, 0.50f, 0.20f}, rnd());
        for (float d = 0; d < len; d += 0.7f) {
            float k = d / len;
            stamp_disc(cv, x + lean * d * k, H - d, (1.0f - k) * 1.6f * N / 256.0f + 0.4f, mul3(c, 0.7f + 0.4f * k), 1.0f);
        }
    }
    return upload(cv, false, true);
}

// Where sunlight gets through the canopy: bright, irregular gaps in shade (tiles every 20 m).
Texture2D paint_dapple(int N) {
    Canvas cv(N, N);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float u = (float)x / N, v = (float)y / N;
            float n = pfbm(u * 6, v * 6, 6, 6, 4, 61);
            float spots = smooth(0.62f, 0.70f, pfbm(u * 24, v * 24, 24, 24, 2, 62));
            float g = clampf(smooth(0.42f, 0.62f, n) * 0.8f + spots * 0.5f * smooth(0.35f, 0.5f, n), 0, 1);
            cv.at(x, y) = {g, g, g};
        }
    return upload(cv, true);
}

// Snout skin: small pebbly scales (a cell pattern), tan above with darker bands running
// along the snout and a paler underside. u runs round the snout, v along it.
Texture2D paint_skin(int N) {
    Canvas cv(N, N);
    const int G = 28;  // scale cells across
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float u = (float)x / N * G, v = (float)y / N * G;
            int iu = (int)std::floor(u), iv = (int)std::floor(v);
            float d1 = 9, d2 = 9;
            int best = 0;
            for (int j = -1; j <= 1; j++)
                for (int i = -1; i <= 1; i++) {
                    int cu = iu + i, cv2 = iv + j;
                    float px = cu + hash2(wrapi(cu, G), wrapi(cv2, G), 71), py = cv2 + hash2(wrapi(cu, G), wrapi(cv2, G), 72);
                    float d = std::sqrt((px - u) * (px - u) + (py - v) * (py - v));
                    if (d < d1) d2 = d1, d1 = d, best = wrapi(cu, G) * 131 + wrapi(cv2, G);
                    else if (d < d2) d2 = d;
                }
            float edge = smooth(0.0f, 0.12f, d2 - d1);
            float tone = hash2(best, 0, 73);
            float uu = (float)x / N;
            // Dark bands along the top of the snout (u near 0.25 is the top centre).
            float band = 0.0f;
            for (float c : {0.17f, 0.33f}) band += 1.0f - smooth(0.02f, 0.05f, std::fabs(uu - c + 0.02f * std::sin(v * 0.9f)));
            Vector3 k = mix3({0.50f, 0.34f, 0.19f}, {0.70f, 0.53f, 0.33f}, tone);
            k = mix3(k, {0.15f, 0.10f, 0.06f}, clampf(band, 0, 1) * 0.75f);
            float under = smooth(0.55f, 0.75f, uu) * (1.0f - smooth(0.95f, 1.0f, uu));
            k = mix3(k, {0.84f, 0.74f, 0.56f}, under * 0.6f);
            k = mul3(k, 0.55f + 0.45f * edge + 0.08f * (1.0f - d1));
            cv.at(x, y) = k;
        }
    return upload(cv, true);
}

// The whole raptor's skin, one map with a strip for each part (raptor.cpp lays the mesh on it).
// A strip's width runs from the middle of the back (0) round to the middle of the belly (1), so
// both sides of the animal share it; its height runs along the part. Rust and tan above, broken
// by dark bars that turn to bold bands down the tail; cream below; fine scales all over, dark in
// the creases. The face has a dark mask through a yellow-green eye.
Texture2D paint_raptor(int N) {
    Canvas cv(N, N);
    const Vector3 rust = {0.40f, 0.19f, 0.07f}, orange = {0.72f, 0.38f, 0.13f}, bar = {0.09f, 0.055f, 0.035f};
    const Vector3 tan = {0.70f, 0.53f, 0.33f}, cream = {0.88f, 0.78f, 0.60f}, brown = {0.34f, 0.21f, 0.12f};
    const Vector3 mouth = {0.25f, 0.07f, 0.07f};
    // Scales: a pattern of cells. Returns the crease (0 in it, 1 on a scale) and a tone per scale.
    auto scales = [&](float u, float v, float& tone) {
        int iu = (int)std::floor(u), iv = (int)std::floor(v), best = 0;
        float d1 = 9, d2 = 9;
        for (int j = -1; j <= 1; j++)
            for (int i = -1; i <= 1; i++) {
                int cu = iu + i, cw = iv + j;
                float px = cu + hash2(cu, cw, 91), py = cw + hash2(cu, cw, 92);
                float d = std::sqrt((px - u) * (px - u) + (py - v) * (py - v));
                if (d < d1) d2 = d1, d1 = d, best = cu * 131 + cw;
                else if (d < d2) d2 = d;
            }
        tone = hash2(best, 0, 93);
        return smooth(0.0f, 0.16f, d2 - d1);
    };
    auto ellipse = [](float x, float y, float cx, float cy, float rx, float ry) {
        float a = (x - cx) / rx, b = (y - cy) / ry;
        return std::sqrt(a * a + b * b);
    };
    // Bars: dark where a wobbling wave along `v` peaks; `width` 0..1.
    auto bars = [](float v, float d, float period, float width, int seed) {
        float warp = (fbm(v * 9.0f, d * 5.0f, 2, seed) - 0.5f) * 1.5f * period;
        float ph = (v + warp) / period;
        float f = std::fabs(ph - std::floor(ph) - 0.5f) * 2.0f;
        return 1.0f - smooth(width - 0.12f, width + 0.12f, f);
    };
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float fx = (x + 0.5f) / N, fy = (y + 0.5f) / N;
            Vector3 k;
            float tone, crease;
            if (fx < 0.5f) {
                // Neck (v < 0.2), body, tail (v > 0.45).
                float d = fx * 2, v = fy;
                crease = scales(d * 58, v * 170, tone);
                float blot = fbm(d * 6, v * 22, 3, 94);
                float neck = 1.0f - smooth(0.17f, 0.26f, v), tail = smooth(0.42f, 0.52f, v);
                k = mix3(rust, orange, smooth(0.3f, 0.7f, blot) * 0.8f + 0.25f * smooth(0.3f, 0.6f, d));
                k = mix3(k, mix3(tan, orange, 0.25f), neck * 0.75f);
                // Bars over the back, down the flanks.
                float broken = smooth(0.34f, 0.56f, fbm(d * 12, v * 34, 2, 99));
                k = mix3(k, brown, (1.0f - smooth(0.05f, 0.42f, d)) * 0.6f);
                float fine = bars(v, d, 0.02f, 0.32f, 95) * broken * (1.0f - smooth(0.4f, 0.64f, d)) * (1.0f - tail) * (1.0f - neck);
                float band = bars(v, d * 0.5f, 0.05f, 0.4f + 0.25f * smooth(0.6f, 1.0f, v), 96) * (0.45f + 0.55f * broken) * (1.0f - smooth(0.62f, 0.92f, d)) * tail;
                float streak = (1.0f - smooth(0.3f, 0.5f, d)) * neck * smooth(0.5f, 0.68f, fbm(d * 10, v * 40, 2, 97));
                k = mix3(k, bar, clampf(fine * 0.8f + band * 0.9f + streak * 0.75f, 0, 1));
                // Pale below, freckled up the throat.
                float pale = smooth(0.52f, 0.76f, d + 0.1f * (blot - 0.5f)) * (1.0f - 0.55f * smooth(0.5f, 0.75f, v));
                k = mix3(k, cream, pale);
                float freckle = smooth(0.66f, 0.72f, pfbm(d * 22, v * 70, 0, 0, 2, 98)) * neck * (1.0f - 0.6f * smooth(0.7f, 0.95f, d));
                k = mix3(k, brown, freckle * 0.8f);
            } else if (fy < 0.40f) {
                // The upper head, nose (v 0) to the back of the skull (v 1).
                float d = (fx - 0.5f) * 2, v = fy / 0.40f;
                crease = 0.4f + 0.6f * scales(d * 50, v * 74, tone);
                float blot = fbm(d * 8, v * 11, 3, 101);
                k = mix3(mix3(tan, orange, 0.3f), cream, smooth(0.6f, 0.1f, v) * 0.55f);
                k = mix3(k, mix3(rust, brown, blot), (1.0f - smooth(0.08f, 0.4f, d)) * 0.8f);
                k = mix3(k, brown, smooth(0.6f, 0.72f, fbm(d * 26, v * 30, 2, 107)) * 0.7f * (1.0f - smooth(0.5f, 0.62f, d)));
                // The mask: from the nostril back through the eye, widening behind it.
                float line = 0.32f - 0.04f * std::sin(v * 7.0f);
                float mask = 1.0f - smooth(0.05f + 0.06f * smooth(0.3f, 0.75f, v), 0.14f + 0.1f * smooth(0.3f, 0.75f, v), std::fabs(d - line) + 0.2f * (blot - 0.5f));
                k = mix3(k, bar, mask * smooth(0.2f, 0.45f, v) * (1.0f - 0.55f * smooth(0.6f, 0.95f, v)) * 0.8f);
                k = mix3(k, bar, (1.0f - smooth(0.6f, 1.2f, ellipse(d, v, 0.42f, 0.85f, 0.075f, 0.03f) + 0.8f * (blot - 0.5f))) * 0.8f);  // the ear
                k = mix3(k, cream, smooth(0.54f, 0.64f, d) * 0.85f);  // the lip
                k = mix3(k, bar, smooth(0.71f, 0.75f, d));            // the mouth line
                k = mix3(k, mix3(bar, mouth, smooth(0.08f, 0.3f, v)), smooth(0.8f, 0.86f, d));
                k = mix3(k, bar, (1.0f - smooth(0.7f, 1.2f, ellipse(d, v, 0.30f, 0.055f, 0.075f, 0.022f))));  // the nostril
                // The eye.
                float e = ellipse(d, v, 0.33f, 0.515f, 0.115f, 0.054f);
                k = mix3(k, bar, 1.0f - smooth(1.0f, 1.45f, e));
                k = mix3(k, mix3({0.86f, 0.84f, 0.34f}, {0.52f, 0.56f, 0.16f}, e), 1.0f - smooth(0.74f, 0.84f, e));
                k = mix3(k, {0.03f, 0.03f, 0.02f}, 1.0f - smooth(0.7f, 1.0f, ellipse(d, v, 0.33f, 0.515f, 0.042f, 0.033f)));
                k = mix3(k, {1, 1, 0.92f}, 1.0f - smooth(0.6f, 1.0f, ellipse(d, v, 0.295f, 0.53f, 0.018f, 0.009f)));
                if (e < 1.0f) crease = 1;
            } else if (fy < 0.52f) {
                // The lower jaw: its top is the mouth; pale outside.
                float d = (fx - 0.5f) * 2, v = (fy - 0.40f) / 0.12f;
                crease = 0.5f + 0.5f * scales(d * 50, v * 36, tone);
                k = mix3(tan, cream, smooth(0.35f, 0.7f, d));
                k = mix3(k, brown, smooth(0.6f, 0.75f, fbm(d * 14, v * 9, 2, 102)) * 0.7f * (1.0f - smooth(0.6f, 0.85f, d)));
                k = mix3(k, cream, 1.0f - smooth(0.3f, 0.38f, d));
                k = mix3(k, bar, 1.0f - smooth(0.25f, 0.29f, d));
                k = mix3(k, mix3(bar, mouth, smooth(0.1f, 0.35f, v)), 1.0f - smooth(0.16f, 0.21f, d));
            } else if (fy < 0.80f) {
                // The leg, hip to toes.
                float d = (fx - 0.5f) * 2, v = (fy - 0.52f) / 0.28f;
                crease = scales(d * 40, v * 120, tone);
                float thigh = 1.0f - smooth(0.36f, 0.5f, v), shank = smooth(0.46f, 0.6f, v), blot = fbm(d * 6, v * 12, 3, 103);
                k = mix3(mix3(rust, orange, smooth(0.3f, 0.7f, blot)), tan, smooth(0.3f, 0.6f, v) * 0.7f);
                k = mix3(k, bar, bars(v, d, 0.05f, 0.24f, 104) * smooth(0.34f, 0.56f, fbm(d * 9, v * 26, 2, 108)) * 0.75f * thigh);
                k = mix3(k, cream, smooth(0.72f, 0.92f, d) * thigh * 0.8f);
                float ring = std::fabs(v * 44 - std::floor(v * 44) - 0.5f) * 2;
                k = mix3(k, mix3(brown, tan, blot * 0.6f), shank * 0.85f);
                k = mix3(k, bar, shank * (1.0f - smooth(0.2f, 0.5f, ring)) * 0.85f);
            } else if (fy < 0.92f) {
                // The arm, shoulder to wrist.
                float d = (fx - 0.5f) * 2, v = (fy - 0.80f) / 0.12f;
                crease = scales(d * 34, v * 54, tone);
                k = mix3(mix3(rust, orange, fbm(d * 6, v * 8, 2, 105)), mix3(tan, brown, 0.45f), 0.35f + 0.65f * smooth(0.15f, 0.6f, v));
                k = mix3(k, bar, bars(v, d, 0.11f, 0.32f, 106) * 0.8f * (1.0f - smooth(0.6f, 0.9f, d)));
                k = mix3(k, cream, smooth(0.62f, 0.86f, d) * (1.0f - smooth(0.2f, 0.8f, v)));
            } else {
                // Toes and fingers (and, in the corner, plain white for claws and teeth).
                float d = (fx - 0.5f) * 2, v = (fy - 0.92f) / 0.08f;
                crease = 1;
                tone = 0.5f;
                float ring = std::fabs(v * 6 - std::floor(v * 6) - 0.5f) * 2;
                k = mix3(mix3(tan, brown, 0.6f), bar, (1.0f - smooth(0.1f, 0.5f, ring)) * 0.6f);
                k = mix3(k, cream, smooth(0.6f, 0.9f, d) * 0.5f);
                if (fx > 0.94f && fy > 0.975f) k = {1, 1, 1}, tone = 0.5f;
            }
            cv.at(x, y) = mul3(k, (0.5f + 0.5f * crease) * (0.88f + 0.24f * tone));
        }
    return upload(cv, false);
}

// A misty, overcast sky, brighter towards the horizon.
Texture2D paint_sky(int W) {
    int H = W / 2;
    Canvas cv(W, H);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float theta = (y + 0.5f) / H * kPi, dy = std::cos(theta);
            float n = pfbm((float)x / W * 8, (float)y / H * 4, 8, 0, 4, 81);
            Vector3 k = dy > 0 ? mix3({0.62f, 0.68f, 0.64f}, {0.44f, 0.52f, 0.52f}, std::pow(dy, 0.6f)) : Vector3{0.40f, 0.45f, 0.40f};
            cv.at(x, y) = mul3(k, 0.92f + 0.12f * n);
        }
    return upload(cv, true, false, false);
}

}  // namespace

void Textures::paint(bool small) {
    int k = small ? 2 : 1;
    std::vector<Color> one = {WHITE};
    Image wimg = {one.data(), 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    white = LoadTextureFromImage(wimg);
    floor = paint_floor(512 / k);
    path = paint_path(512 / k);
    bark = paint_bark(512 / k);
    rock = paint_rock(256 / k);
    fern = paint_fern(256 / k);
    leaves = paint_leaves(256 / k);
    grass = paint_grass(256 / k);
    dapple = paint_dapple(256 / k);
    skin = paint_skin(256);
    raptor = paint_raptor(512);
    sky = paint_sky(512 / k);
}

void Textures::unload() {
    for (Texture2D* t : {&white, &floor, &path, &bark, &rock, &fern, &leaves, &dapple, &skin, &raptor, &sky, &grass}) UnloadTexture(*t);
}

}  // namespace rr
