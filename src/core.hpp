// Shared helpers. Y up, -Z forward, right-handed; rotations in
// xform() are angles applied Z, then X, then Y.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

namespace rr {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTau = 2.0f * kPi;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float smooth(float e0, float e1, float x) {
    float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
inline float approach(float v, float target, float step) {
    if (std::fabs(target - v) <= step) return target;
    return v + (target > v ? step : -step);
}
// Shortest signed difference from one angle to another.
inline float angle_diff(float from, float to) {
    float d = std::fmod(to - from + kPi, kTau);
    if (d < 0) d += kTau;
    return d - kPi;
}
inline float lerp_angle(float a, float b, float t) { return a + angle_diff(a, b) * t; }
// Easing: curve > 1 eases in, 0..1 eases out, negative is in-out.
inline float ease(float x, float c) {
    x = clampf(x, 0.0f, 1.0f);
    if (c > 0) return c < 1 ? 1.0f - std::pow(1.0f - x, 1.0f / c) : std::pow(x, c);
    if (c < 0) {
        if (x < 0.5f) return std::pow(x * 2.0f, -c) * 0.5f;
        return (1.0f - std::pow(1.0f - (x - 0.5f) * 2.0f, -c)) * 0.5f + 0.5f;
    }
    return 0.0f;
}

float randf(float lo = 0.0f, float hi = 1.0f);

// Where the game's files are: assets/ next to the program (in the browser, the preloaded /assets),
// and the settings file (in the browser, /save, kept in the browser's storage).
inline std::string asset_path(const char* rel) {
#if defined(__EMSCRIPTEN__)
    return std::string("/assets/") + rel;
#else
    return std::string(GetApplicationDirectory()) + "assets/" + rel;
#endif
}
inline std::string save_path(const char* name) {
#if defined(__EMSCRIPTEN__)
    return std::string("/save/") + name;
#else
    return std::string(GetApplicationDirectory()) + name;
#endif
}

// Transforms. compose(first, then) applies `first`, then `then` (raymath's convention).
inline Matrix compose(const Matrix& first, const Matrix& then) { return MatrixMultiply(first, then); }
inline Matrix xform(Vector3 pos, Vector3 rot = {0, 0, 0}, Vector3 scale = {1, 1, 1}) {
    Matrix m = MatrixScale(scale.x, scale.y, scale.z);
    m = compose(m, MatrixRotateZ(rot.z));
    m = compose(m, MatrixRotateX(rot.x));
    m = compose(m, MatrixRotateY(rot.y));
    return compose(m, MatrixTranslate(pos.x, pos.y, pos.z));
}
inline Vector3 origin_of(const Matrix& m) { return {m.m12, m.m13, m.m14}; }
inline Vector3 xf_dir(const Matrix& m, Vector3 d) {
    return {m.m0 * d.x + m.m4 * d.y + m.m8 * d.z, m.m1 * d.x + m.m5 * d.y + m.m9 * d.z, m.m2 * d.x + m.m6 * d.y + m.m10 * d.z};
}
// A matrix from basis columns and an origin.
inline Matrix basis(Vector3 x, Vector3 y, Vector3 z, Vector3 o) {
    Matrix m = MatrixIdentity();
    m.m0 = x.x; m.m1 = x.y; m.m2 = x.z;
    m.m4 = y.x; m.m5 = y.y; m.m6 = y.z;
    m.m8 = z.x; m.m9 = z.y; m.m10 = z.z;
    m.m12 = o.x; m.m13 = o.y; m.m14 = o.z;
    return m;
}
inline Vector3 flat(Vector3 v) { return {v.x, 0.0f, v.z}; }

// Colours from 0..1 floats. `shine` goes in alpha: 0 matte .. 1 polished metal.
inline Color rgb(float r, float g, float b, float shine = 0.0f) {
    auto c = [](float v) { return (unsigned char)(clampf(v, 0, 1) * 255.0f + 0.5f); };
    return Color{c(r), c(g), c(b), c(1.0f - shine)};
}
inline Color darken(Color c, float f) {
    return Color{(unsigned char)(c.r * (1 - f)), (unsigned char)(c.g * (1 - f)), (unsigned char)(c.b * (1 - f)), c.a};
}
inline Color lighten(Color c, float f) {
    return Color{(unsigned char)(c.r + (255 - c.r) * f), (unsigned char)(c.g + (255 - c.g) * f), (unsigned char)(c.b + (255 - c.b) * f), c.a};
}
inline Vector3 to_vec(Color c) { return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f}; }

}  // namespace rr
