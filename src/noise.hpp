// Value noise for painted textures and terrain. The p* variants repeat every `period` units,
// so textures painted with them tile without seams.
#pragma once
#include "core.hpp"

namespace rr {

inline float hash2(int x, int y, int seed = 0) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + (uint32_t)seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xffffff) / 16777215.0f;
}

inline int wrapi(int v, int p) { return p > 0 ? ((v % p) + p) % p : v; }

inline float pnoise(float x, float y, int px = 0, int py = 0, int seed = 0) {
    int ix = (int)std::floor(x), iy = (int)std::floor(y);
    float fx = x - ix, fy = y - iy;
    float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    int x0 = wrapi(ix, px), x1 = wrapi(ix + 1, px), y0 = wrapi(iy, py), y1 = wrapi(iy + 1, py);
    float a = hash2(x0, y0, seed), b = hash2(x1, y0, seed), c = hash2(x0, y1, seed), d = hash2(x1, y1, seed);
    return lerpf(lerpf(a, b, ux), lerpf(c, d, ux), uy);
}

inline float pfbm(float x, float y, int px, int py, int octaves, int seed = 0) {
    float v = 0, a = 0.5f, norm = 0;
    for (int k = 0; k < octaves; k++) {
        v += a * pnoise(x, y, px, py, seed + k);
        norm += a;
        x *= 2.0f;
        y *= 2.0f;
        px *= 2;
        py *= 2;
        a *= 0.5f;
    }
    return v / norm;
}

inline float fbm(float x, float y, int octaves, int seed = 0) { return pfbm(x, y, 0, 0, octaves, seed); }

}  // namespace rr
