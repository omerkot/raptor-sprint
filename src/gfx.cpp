#include "gfx.hpp"

#include <cstring>

#if defined(PLATFORM_ANDROID)
#include <GLES2/gl2.h>
#endif

namespace rr {

Gfx gfx;

static uint32_t rng_state = 0x9e3779b9u;
float randf(float lo, float hi) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return lo + (hi - lo) * ((rng_state & 0xffffff) / 16777215.0f);
}

// ------------------------------------------------------------------ mesh builder

void MeshBuilder::tri(const Matrix& m, const Matrix& nm, Vector3 a, Vector3 b, Vector3 c, Vector3 na, Vector3 nb,
                      Vector3 nc, Color col, Vector2 ua, Vector2 ub, Vector2 uc) {
    Vector3 p[3] = {Vector3Transform(a, m), Vector3Transform(b, m), Vector3Transform(c, m)};
    Vector3 n[3] = {Vector3Normalize(xf_dir(nm, na)), Vector3Normalize(xf_dir(nm, nb)), Vector3Normalize(xf_dir(nm, nc))};
    Vector2 u[3] = {ua, ub, uc};
    // Keep counter-clockwise winding facing the normals (also fixes mirrored transforms).
    Vector3 face = Vector3CrossProduct(Vector3Subtract(p[1], p[0]), Vector3Subtract(p[2], p[0]));
    if (Vector3DotProduct(face, Vector3Add(Vector3Add(n[0], n[1]), n[2])) < 0) {
        std::swap(p[1], p[2]);
        std::swap(n[1], n[2]);
        std::swap(u[1], u[2]);
    }
    if (world_uv) {
        Vector3 fn = Vector3Normalize(Vector3Add(Vector3Add(n[0], n[1]), n[2]));
        for (int i = 0; i < 3; i++) {
            float s, t;
            if (std::fabs(fn.y) > 0.7f) { s = p[i].x; t = p[i].z; }
            else if (std::fabs(fn.x) > std::fabs(fn.z)) { s = p[i].z; t = p[i].y; }
            else { s = p[i].x; t = p[i].y; }
            u[i] = {s / uv_size.x, -t / uv_size.y};
        }
    }
    for (int i = 0; i < 3; i++) {
        pos_.insert(pos_.end(), {p[i].x, p[i].y, p[i].z});
        nrm_.insert(nrm_.end(), {n[i].x, n[i].y, n[i].z});
        uv_.insert(uv_.end(), {u[i].x, u[i].y});
        col_.insert(col_.end(), {col.r, col.g, col.b, col.a});
    }
}

void MeshBuilder::vtri(Vector3 a, Vector3 b, Vector3 c, Vector3 na, Vector3 nb, Vector3 nc, Color ca, Color cb, Color cc,
                       Vector2 ua, Vector2 ub, Vector2 uc) {
    Vector3 p[3] = {a, b, c}, n[3] = {na, nb, nc};
    Color k[3] = {ca, cb, cc};
    Vector2 u[3] = {ua, ub, uc};
    Vector3 face = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
    if (Vector3DotProduct(face, Vector3Add(Vector3Add(na, nb), nc)) < 0) {
        std::swap(p[1], p[2]);
        std::swap(n[1], n[2]);
        std::swap(k[1], k[2]);
        std::swap(u[1], u[2]);
    }
    for (int i = 0; i < 3; i++) {
        pos_.insert(pos_.end(), {p[i].x, p[i].y, p[i].z});
        nrm_.insert(nrm_.end(), {n[i].x, n[i].y, n[i].z});
        uv_.insert(uv_.end(), {u[i].x, u[i].y});
        col_.insert(col_.end(), {k[i].r, k[i].g, k[i].b, k[i].a});
    }
}

void MeshBuilder::triangle(Vector3 a, Vector3 b, Vector3 c, Color col) {
    Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
    Matrix id = MatrixIdentity();
    tri(id, id, a, b, c, n, n, n, col);
}

void MeshBuilder::box(Vector3 s, const Matrix& m, Color c) {
    Matrix nm = MatrixTranspose(MatrixInvert(m));
    Vector3 h = {s.x * 0.5f, s.y * 0.5f, s.z * 0.5f};
    struct Face { Vector3 n, u, v; };
    const Face faces[6] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };
    for (const Face& f : faces) {
        Vector3 c0 = {f.n.x * h.x, f.n.y * h.y, f.n.z * h.z};
        Vector3 du = {f.u.x * h.x, f.u.y * h.y, f.u.z * h.z};
        Vector3 dv = {f.v.x * h.x, f.v.y * h.y, f.v.z * h.z};
        Vector3 q[4] = {c0 - du - dv, c0 + du - dv, c0 + du + dv, c0 - du + dv};
        tri(m, nm, q[0], q[1], q[2], f.n, f.n, f.n, c, {0, 1}, {1, 1}, {1, 0});
        tri(m, nm, q[0], q[2], q[3], f.n, f.n, f.n, c, {0, 1}, {1, 0}, {0, 0});
    }
}

void MeshBuilder::cylinder(float rt, float rb, float h, int seg, const Matrix& m, Color c, bool caps) {
    Matrix nm = MatrixTranspose(MatrixInvert(m));
    float slope = (rb - rt) / h;
    for (int i = 0; i < seg; i++) {
        // (The last step wraps to exactly angle 0, so the ring closes without a hairline gap.)
        float a0 = kTau * i / seg, a1 = kTau * ((i + 1) % seg) / seg;
        float u0 = (float)i / seg, u1 = (float)(i + 1) / seg;
        Vector3 b0 = {rb * std::cos(a0), -h / 2, rb * std::sin(a0)}, b1 = {rb * std::cos(a1), -h / 2, rb * std::sin(a1)};
        Vector3 t0 = {rt * std::cos(a0), h / 2, rt * std::sin(a0)}, t1 = {rt * std::cos(a1), h / 2, rt * std::sin(a1)};
        Vector3 n0 = Vector3Normalize({std::cos(a0), slope, std::sin(a0)}), n1 = Vector3Normalize({std::cos(a1), slope, std::sin(a1)});
        tri(m, nm, b0, t0, t1, n0, n0, n1, c, {u0, 1}, {u0, 0}, {u1, 0});
        tri(m, nm, b0, t1, b1, n0, n1, n1, c, {u0, 1}, {u1, 0}, {u1, 1});
        if (caps) {
            if (rt > 0) tri(m, nm, {0, h / 2, 0}, t1, t0, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, c);
            if (rb > 0) tri(m, nm, {0, -h / 2, 0}, b0, b1, {0, -1, 0}, {0, -1, 0}, {0, -1, 0}, c);
        }
    }
}

void MeshBuilder::sphere(float r, float h, int seg, int rings, const Matrix& m, Color c, bool hemisphere) {
    Matrix nm = MatrixTranspose(MatrixInvert(m));
    float ry = hemisphere ? h : h * 0.5f;
    float span = hemisphere ? kPi * 0.5f : kPi;
    int n = hemisphere ? rings : rings * 2;
    auto pt = [&](int k, int i) {
        float t = span * k / n, a = kTau * (i % seg) / seg;
        return Vector3{r * std::sin(t) * std::cos(a), ry * std::cos(t), r * std::sin(t) * std::sin(a)};
    };
    auto nr = [&](int k, int i) {
        float t = span * k / n, a = kTau * (i % seg) / seg;
        Vector2 nn = Vector2Normalize({std::sin(t) / r, std::cos(t) / ry});
        return Vector3{nn.x * std::cos(a), nn.y, nn.x * std::sin(a)};
    };
    for (int k = 0; k < n; k++)
        for (int i = 0; i < seg; i++) {
            Vector2 uv00 = {(float)i / seg, (float)k / n}, uv10 = {(float)(i + 1) / seg, (float)k / n};
            Vector2 uv01 = {(float)i / seg, (float)(k + 1) / n}, uv11 = {(float)(i + 1) / seg, (float)(k + 1) / n};
            if (k > 0) tri(m, nm, pt(k, i), pt(k + 1, i), pt(k, i + 1), nr(k, i), nr(k + 1, i), nr(k, i + 1), c, uv00, uv01, uv10);
            tri(m, nm, pt(k, i + 1), pt(k + 1, i), pt(k + 1, i + 1), nr(k, i + 1), nr(k + 1, i), nr(k + 1, i + 1), c, uv10, uv01, uv11);
        }
}

void MeshBuilder::quad(float w, float h, const Matrix& m, Color c, Vector2 uv0, Vector2 uv1) {
    Matrix nm = MatrixTranspose(MatrixInvert(m));
    Vector3 n = {0, 0, 1};
    Vector3 a = {-w / 2, -h / 2, 0}, b = {w / 2, -h / 2, 0}, cc = {w / 2, h / 2, 0}, d = {-w / 2, h / 2, 0};
    tri(m, nm, a, b, cc, n, n, n, c, {uv0.x, uv1.y}, {uv1.x, uv1.y}, {uv1.x, uv0.y});
    tri(m, nm, a, cc, d, n, n, n, c, {uv0.x, uv1.y}, {uv1.x, uv0.y}, {uv0.x, uv0.y});
}

Mesh MeshBuilder::build() {
    Mesh mesh = {};
    int n = (int)vertex_count();
    // Triangles are added with their own three corners, but most corners are shared with a
    // neighbour: keep each distinct vertex once and index it, so the GPU shades a third as many.
    size_t size = 16;
    while (size < (size_t)n * 2) size *= 2;
    std::vector<int> table(size, -1), first;
    std::vector<unsigned short> index(n);
    first.reserve(n / 2);
    auto same = [&](int a, int b) {
        return !std::memcmp(&pos_[a * 3], &pos_[b * 3], 12) && !std::memcmp(&nrm_[a * 3], &nrm_[b * 3], 12) &&
               !std::memcmp(&uv_[a * 2], &uv_[b * 2], 8) && !std::memcmp(&col_[a * 4], &col_[b * 4], 4);
    };
    auto hash = [&](int a) {
        uint32_t h = 2166136261u, w[3];
        std::memcpy(w, &pos_[a * 3], 12);
        for (uint32_t x : w) h = (h ^ x) * 16777619u;
        std::memcpy(w, &nrm_[a * 3], 12);
        for (uint32_t x : w) h = (h ^ x) * 16777619u;
        return h ^ (h >> 15);
    };
    bool shared = true;
    for (int i = 0; i < n && shared; i++) {
        size_t slot = hash(i) & (size - 1);
        while (table[slot] >= 0 && !same(first[table[slot]], i)) slot = (slot + 1) & (size - 1);
        if (table[slot] < 0) {
            table[slot] = (int)first.size();
            first.push_back(i);
            shared = first.size() <= 65535;  // (indices are 16 bits)
        }
        if (shared) index[i] = (unsigned short)table[slot];
    }
    int v = shared ? (int)first.size() : n;
    mesh.vertexCount = v;
    mesh.triangleCount = n / 3;
    mesh.vertices = (float*)MemAlloc(sizeof(float) * 3 * v);
    mesh.normals = (float*)MemAlloc(sizeof(float) * 3 * v);
    mesh.texcoords = (float*)MemAlloc(sizeof(float) * 2 * v);
    mesh.colors = (unsigned char*)MemAlloc(4 * v);
    for (int i = 0; i < v; i++) {
        int from = shared ? first[i] : i;
        std::memcpy(mesh.vertices + i * 3, &pos_[from * 3], 12);
        std::memcpy(mesh.normals + i * 3, &nrm_[from * 3], 12);
        std::memcpy(mesh.texcoords + i * 2, &uv_[from * 2], 8);
        std::memcpy(mesh.colors + i * 4, &col_[from * 4], 4);
    }
    if (shared) {
        mesh.indices = (unsigned short*)MemAlloc(sizeof(unsigned short) * n);
        std::memcpy(mesh.indices, index.data(), sizeof(unsigned short) * n);
    }
    UploadMesh(&mesh, false);
    return mesh;
}

// ------------------------------------------------------------------ shaders

// Lighting, worked out per vertex (the handhelds are short of pixel power, not vertices): sky and
// ground ambient, sunlight, and exponential fog that glows towards the sun. vLight holds how much
// of each light falls on the front (xy) and back (zw); the pixels only add the dapple and the curve.
static const char* LIT_VS = R"(
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec3 vertexNormal;
attribute vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
uniform vec4 colDiffuse;
uniform vec3 sunDir;
uniform mediump vec3 sunCol;
uniform vec3 camPos;
uniform vec3 fogCol;
uniform float fogDensity;
uniform float time;
uniform float reach;  // how far the world is drawn
#ifdef RICH
varying mediump vec3 vN;
varying mediump vec3 vView;
#endif
varying highp vec2 vUV;
varying highp vec2 vDap;
varying vec4 vC;
varying vec4 vFog;  // what the fog adds (rgb), how much of the scene shows through it (a)
varying vec4 vLight;
#ifdef CUTOUT
varying highp vec3 vW;
varying float vDist;
#endif
void main() {
    vec3 W = (matModel * vec4(vertexPosition, 1.0)).xyz;
    vec3 N = normalize((matNormal * vec4(vertexNormal, 0.0)).xyz);
    vec3 d = W - camPos;
    float dist = length(d);
    vec3 dir = d / max(dist, 0.001);
    float s = max(dot(dir, -sunDir), 0.0), glow = pow(s, 5.0);
    vec3 fc = fogCol + sunCol * glow * 0.22;
    // Past 70% of its reach the world sinks into the haze, and above the horizon into the sky
    // behind it (as painted in textures.cpp), so what's about to be drawn is never seen to start.
    float edge = clamp((reach - dist) / (reach * 0.3), 0.0, 1.0);
    float up = max(dir.y, 0.0);
    vec3 sky = mix(vec3(0.62, 0.68, 0.64), vec3(0.44, 0.52, 0.52), pow(up, 0.6)) + sunCol * (glow * s * 0.25);
    fc = mix(mix(fc, sky, smoothstep(0.0, 0.35, up)), fc, edge);
    float clear = exp(-fogDensity * dist) * edge;
    vFog = vec4(fc * (1.0 - clear), clear);
    float nl = dot(N, -sunDir);
    vLight = vec4(N.y * 0.5 + 0.5, max((nl + 0.25) / 1.25, 0.0), 0.5 - N.y * 0.5, max((0.25 - nl) / 1.25, 0.0));
#ifdef CUTOUT
    // Thin leaves let light through from behind.
    vLight.y += max(-nl, 0.0) * 0.45;
    vLight.w += max(nl, 0.0) * 0.45;
    vW = W;
    vDist = dist;
#endif
#ifdef RICH
    vN = N;
    vView = -dir;
#endif
    vDap = W.xz * 0.05 + vec2(time * 0.006, time * 0.004);
    vC = vertexColor * colDiffuse;
    vUV = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// Sunlight broken up by the canopy (a drifting dapple pattern), then the fog and a soft filmic curve.
static const char* LIGHTING = R"(
varying highp vec2 vUV;
varying highp vec2 vDap;
varying vec4 vC;
varying vec4 vFog;
varying vec4 vLight;
uniform sampler2D texture0;
uniform sampler2D texture1;
uniform mediump vec3 sunCol;
uniform vec3 skyAmb;
uniform vec3 groundAmb;
uniform vec3 emissive;
vec3 shade(vec3 albedo, vec2 light, float sheen) {
    float through = mix(0.10, 1.3, texture2D(texture1, vDap).r);
    vec3 col = albedo * (mix(groundAmb, skyAmb, light.x) + sunCol * (light.y * through)) + emissive;
    col += sunCol * (sheen * through);
    col = col * vFog.a + vFog.rgb;
    col = vec3(1.0) - exp(-col * 1.25);
    float l = dot(col, vec3(0.3, 0.59, 0.11));
    return mix(vec3(l), col, 0.86);
}
)";

static const char* LIT_FS = R"(
#ifdef RICH
varying mediump vec3 vN;
varying mediump vec3 vView;
uniform mediump vec3 toSun;
float height(highp vec2 uv) { return dot(texture2D(texture0, uv).rgb, vec3(0.33)); }
void main() {
    vec3 tex = texture2D(texture0, vUV).rgb;
    // The texture's own light and dark, read as bumps, so the surface catches the light in detail.
    float h = dot(tex, vec3(0.33));
    vec3 N = normalize(vN);
    vec3 T = normalize(cross(N, vec3(0.31, 0.12, 0.94))), B = cross(N, T);
    N = normalize(N + (T * (h - height(vUV + vec2(0.004, 0.0))) + B * (h - height(vUV + vec2(0.0, 0.004)))) * 3.5);
    float nl = dot(N, toSun);
    vec2 light = vec2(N.y * 0.5 + 0.5, max((nl + 0.25) / 1.25, 0.0));
    float glance = max(dot(N, normalize(toSun + normalize(vView))), 0.0);
    float sheen = pow(glance, 28.0) * 0.16;
    gl_FragColor = vec4(shade(tex * vC.rgb, light, sheen), 1.0);
}
#else
void main() {
    gl_FragColor = vec4(shade(texture2D(texture0, vUV).rgb * vC.rgb, vLight.xy, 0.0), 1.0);
}
#endif
)";

static const char* CUT_FS = R"(
uniform vec2 fade;  // distance where the layer starts thinning out, and where it's gone
varying highp vec3 vW;
varying float vDist;
void main() {
    // Past its draw distance a layer thins out, a clump at a time, instead of stopping at a line.
    // (Clumps fixed in the world: a pattern fixed to the screen shimmers as the view moves.)
    float keep = clamp((fade.y - vDist) / (fade.y - fade.x), 0.0, 1.0);
    if (keep < 1.0) {
        highp vec3 p3 = fract(floor(vW * 1.6) * 0.1031);
        p3 += dot(p3, p3.zyx + 31.32);
        if (keep <= fract((p3.x + p3.y) * p3.z)) discard;
    }
    vec4 t = texture2D(texture0, vUV);
#ifdef RICH
    // Soft edges: the alpha becomes coverage across the multisamples.
    if (t.a < 0.25) discard;
    gl_FragColor = vec4(shade(t.rgb * vC.rgb, gl_FrontFacing ? vLight.xy : vLight.zw, 0.0), clamp((t.a - 0.35) * 3.3, 0.0, 1.0));
#else
    if (t.a < 0.5) discard;
    gl_FragColor = vec4(shade(t.rgb * vC.rgb, gl_FrontFacing ? vLight.xy : vLight.zw, 0.0), 1.0);
#endif
}
)";

// The finishing passes, at a quarter of the view's size, then over it. (rich only)
static const char* POST_VS = R"(
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
uniform mat4 mvp;
varying highp vec2 vUV;
void main() {
    vUV = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";
// What's bright enough to glow.
static const char* BRIGHT_FS = R"(
varying highp vec2 vUV;
uniform sampler2D texture0;
void main() {
    vec3 c = texture2D(texture0, vUV).rgb;
    float l = dot(c, vec3(0.3, 0.59, 0.11));
    gl_FragColor = vec4(c * smoothstep(0.5, 0.88, l), 1.0);
}
)";
// Light streaming away from the sun, through the gaps between the trunks: the bright pixels of the
// upper view, smeared along the lines leading to the sun.
static const char* RADIAL_FS = R"(
varying highp vec2 vUV;
uniform sampler2D texture0;
uniform vec2 sun;
uniform float amount;
void main() {
    vec2 p = vUV, d = (sun - vUV) * (0.85 / 14.0);
    vec3 c = vec3(0.0);
    float w = 1.0;
    for (int i = 0; i < 14; i++) {
        c += texture2D(texture0, p).rgb * (w * smoothstep(0.42, 0.72, p.y));
        w *= 0.93;
        p += d;
    }
    gl_FragColor = vec4(c * (amount * 0.17 * smoothstep(0.3, 0.62, vUV.y)), 1.0);  // (in the air, not on the ground)
}
)";
static const char* BLUR_FS = R"(
varying highp vec2 vUV;
uniform sampler2D texture0;
uniform vec2 stepBy;
void main() {
    vec3 c = texture2D(texture0, vUV).rgb * 0.227;
    c += (texture2D(texture0, vUV + stepBy * 1.385).rgb + texture2D(texture0, vUV - stepBy * 1.385).rgb) * 0.316;
    c += (texture2D(texture0, vUV + stepBy * 3.231).rgb + texture2D(texture0, vUV - stepBy * 3.231).rgb) * 0.070;
    gl_FragColor = vec4(c, 1.0);
}
)";
// The blurred glow and the shafts together.
static const char* MERGE_FS = R"(
varying highp vec2 vUV;
uniform sampler2D texture0;
uniform sampler2D extra;
void main() {
    gl_FragColor = vec4(texture2D(texture0, vUV).rgb * 0.6 + texture2D(extra, vUV).rgb, 1.0);
}
)";
// The view with its light added, a little more contrast, and the corners falling away.
static const char* FINISH_FS = R"(
varying highp vec2 vUV;
uniform sampler2D texture0;
uniform sampler2D glow;
void main() {
    vec3 c = texture2D(texture0, vUV).rgb;
    vec3 g = texture2D(glow, vUV).rgb;
    c = pow(c, vec3(0.8));  // (open up the shadows: the forest floor is where the eye is)
    c = vec3(1.0) - (vec3(1.0) - c) * (vec3(1.0) - g);
    c = mix(c, c * c * (3.0 - 2.0 * c), 0.25);
    float l = dot(c, vec3(0.3, 0.59, 0.11));
    c = mix(vec3(l), c, 1.14);
    vec2 q = vUV - 0.5;
    c *= 1.0 - dot(q, q) * 0.28;
    gl_FragColor = vec4(c, 1.0);
}
)";

static const char* SKY_VS = R"(
attribute vec3 vertexPosition;
uniform mat4 mvp;
varying vec3 vDir;
void main() {
    vDir = vertexPosition;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char* SKY_FS = R"(
varying vec3 vDir;
uniform sampler2D texture0;
uniform vec3 sunDir;
uniform vec3 sunCol;
uniform vec3 fogCol;
void main() {
    vec3 d = normalize(vDir);
    float u = atan(d.x, -d.z) / 6.2831853 + 0.5;
    float v = acos(clamp(d.y, -1.0, 1.0)) / 3.1415927;
    vec3 col = texture2D(texture0, vec2(u, v)).rgb;
    float s = max(dot(d, -sunDir), 0.0);
    col += sunCol * (pow(s, 600.0) * 2.0 + pow(s, 6.0) * 0.25);
    col = mix(fogCol + sunCol * pow(s, 5.0) * 0.22, col, smoothstep(0.0, 0.35, d.y));
    col = vec3(1.0) - exp(-col * 1.25);
    float l = dot(col, vec3(0.3, 0.59, 0.11));
    gl_FragColor = vec4(mix(vec3(l), col, 0.86), 1.0);
}
)";

// GLSL 1.00 for OpenGL ES 2 (the handheld); the same source adapted to GLSL 3.30 on the Mac.
static std::string vs_src(const char* body, bool gles) {
    if (gles) return std::string("#version 100\nprecision highp float;\n") + body;
    return std::string("#version 330\n#define attribute in\n#define varying out\n") + body;
}
// `fast`: half precision, twice the speed on the handhelds' GPUs (anything that needs more, such as
// world positions and texture coordinates, says highp).
static std::string fs_src(const char* body, bool gles, bool fast = false) {
    if (gles && fast) return std::string("#version 100\nprecision mediump float;\n") + body;
    if (gles)
        return std::string(
                   "#version 100\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump "
                   "float;\n#endif\n") +
               body;
    return std::string(
               "#version 330\n#define varying in\n#define texture2D texture\nout vec4 fragOut;\n#define gl_FragColor "
               "fragOut\n") +
           body;
}

// ------------------------------------------------------------------ Gfx

static void setup_lit(Shader& s, int idx, const Vector3& sun_dir, const Vector3& sun_col, const Vector3& sky_amb,
                      const Vector3& ground_amb, const Vector3& fog_col, float fog) {
    s.locs[SHADER_LOC_MATRIX_NORMAL] = GetShaderLocation(s, "matNormal");
    s.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(s, "matModel");
    s.locs[SHADER_LOC_MAP_SPECULAR] = GetShaderLocation(s, "texture1");  // (the dapple pattern rides in map 1)
    gfx.loc_cam[idx] = GetShaderLocation(s, "camPos");
    gfx.loc_time[idx] = GetShaderLocation(s, "time");
    gfx.loc_reach[idx] = GetShaderLocation(s, "reach");
    gfx.loc_emissive[idx] = GetShaderLocation(s, "emissive");
    if (idx == 1) gfx.loc_fade = GetShaderLocation(s, "fade");
    SetShaderValue(s, GetShaderLocation(s, "sunDir"), &sun_dir, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, GetShaderLocation(s, "sunCol"), &sun_col, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, GetShaderLocation(s, "skyAmb"), &sky_amb, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, GetShaderLocation(s, "groundAmb"), &ground_amb, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, GetShaderLocation(s, "fogCol"), &fog_col, SHADER_UNIFORM_VEC3);
    SetShaderValue(s, GetShaderLocation(s, "fogDensity"), &fog, SHADER_UNIFORM_FLOAT);
    Vector3 to_sun = Vector3Negate(sun_dir);
    int loc_to_sun = GetShaderLocation(s, "toSun");
    if (loc_to_sun >= 0) SetShaderValue(s, loc_to_sun, &to_sun, SHADER_UNIFORM_VEC3);
}

void Gfx::init(bool gles, bool small_textures, bool rich) {
    this->gles = gles;
    this->rich = rich;
    std::string tier = rich ? "#define RICH\n" : "";
    std::string lit_vs = tier + LIT_VS, lit_fs = tier + LIGHTING + LIT_FS, cut_fs = tier + LIGHTING + CUT_FS;
    std::string cut_vs = tier + "#define CUTOUT\n" + LIT_VS;
    lit = LoadShaderFromMemory(vs_src(lit_vs.c_str(), gles).c_str(), fs_src(lit_fs.c_str(), gles, true).c_str());
    cut = LoadShaderFromMemory(vs_src(cut_vs.c_str(), gles).c_str(), fs_src(cut_fs.c_str(), gles, true).c_str());
    sky = LoadShaderFromMemory(vs_src(SKY_VS, gles).c_str(), fs_src(SKY_FS, gles).c_str());
    tex.paint(small_textures);
    if (rich) {
        // Sharp ground right out to the horizon, however shallow the angle.
        for (Texture2D t : {tex.floor, tex.path, tex.bark, tex.rock, tex.fern, tex.grass, tex.leaves}) SetTextureFilter(t, TEXTURE_FILTER_ANISOTROPIC_8X);
        auto post = [&](const char* fs) { return LoadShaderFromMemory(vs_src(POST_VS, gles).c_str(), fs_src(fs, gles).c_str()); };
        bright = post(BRIGHT_FS), radial = post(RADIAL_FS), blur = post(BLUR_FS), merge = post(MERGE_FS), finish = post(FINISH_FS);
        loc_sun = GetShaderLocation(radial, "sun"), loc_amount = GetShaderLocation(radial, "amount");
        loc_step = GetShaderLocation(blur, "stepBy"), loc_extra = GetShaderLocation(merge, "extra"), loc_glow = GetShaderLocation(finish, "glow");
    }

    set_environment(region(0).biome);

    lit_mat = LoadMaterialDefault();
    lit_mat.shader = lit;
    lit_mat.maps[MATERIAL_MAP_SPECULAR].texture = tex.dapple;
    cut_mat = LoadMaterialDefault();
    cut_mat.shader = cut;
    cut_mat.maps[MATERIAL_MAP_SPECULAR].texture = tex.dapple;
    sky_mat = LoadMaterialDefault();
    sky_mat.shader = sky;
    sky_mat.maps[MATERIAL_MAP_DIFFUSE].texture = tex.sky;

    MeshBuilder sb;
    sb.sphere(300.0f, 600.0f, 24, 12, MatrixIdentity(), WHITE);
    sky_mesh = sb.build();
}

void Gfx::set_environment(const Biome& b) {
    sun_dir = Vector3Negate(Vector3Normalize(b.to_sun));
    fog_col = b.fog;
    setup_lit(lit, 0, sun_dir, b.sun_col, b.sky_amb, b.ground_amb, b.fog, b.fog_density);
    setup_lit(cut, 1, sun_dir, b.sun_col, b.sky_amb, b.ground_amb, b.fog, b.fog_density);
    SetShaderValue(sky, GetShaderLocation(sky, "sunDir"), &sun_dir, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky, GetShaderLocation(sky, "sunCol"), &b.sun_col, SHADER_UNIFORM_VEC3);
    SetShaderValue(sky, GetShaderLocation(sky, "fogCol"), &b.fog, SHADER_UNIFORM_VEC3);
}

void Gfx::begin_frame(const Camera3D& cam, float time, float reach) {
    for (int i = 0; i < 2; i++) {
        Shader& s = i ? cut : lit;
        SetShaderValue(s, loc_cam[i], &cam.position, SHADER_UNIFORM_VEC3);
        SetShaderValue(s, loc_time[i], &time, SHADER_UNIFORM_FLOAT);
        SetShaderValue(s, loc_reach[i], &reach, SHADER_UNIFORM_FLOAT);
    }
}

// Drawn after the world, so the depth test spares the pixels the trees and ground already cover.
void Gfx::draw_sky(const Camera3D& cam) {
    rlDisableDepthMask();
    rlDisableBackfaceCulling();
    DrawMesh(sky_mesh, sky_mat, MatrixTranslate(cam.position.x, cam.position.y, cam.position.z));
    rlEnableBackfaceCulling();
    rlEnableDepthMask();
}

void Gfx::draw(const Mesh& mesh, const Matrix& m, Texture2D t, Color tint, Vector3 emissive) {
    SetShaderValue(lit, loc_emissive[0], &emissive, SHADER_UNIFORM_VEC3);
    lit_mat.maps[MATERIAL_MAP_DIFFUSE].texture = t;
    lit_mat.maps[MATERIAL_MAP_DIFFUSE].color = tint;
    DrawMesh(mesh, lit_mat, m);
}

void Gfx::draw_cut(const Mesh& mesh, const Matrix& m, Texture2D t, Color tint, float fade_to) {
    Vector3 none = {0, 0, 0};
    SetShaderValue(cut, loc_emissive[1], &none, SHADER_UNIFORM_VEC3);
    Vector2 fade = {fade_to * 0.7f, fade_to};
    SetShaderValue(cut, loc_fade, &fade, SHADER_UNIFORM_VEC2);
    cut_mat.maps[MATERIAL_MAP_DIFFUSE].texture = t;
    cut_mat.maps[MATERIAL_MAP_DIFFUSE].color = tint;
    rlDisableBackfaceCulling();
#if defined(PLATFORM_ANDROID)
    if (rich) glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
#endif
    DrawMesh(mesh, cut_mat, m);
    rlDrawRenderBatchActive();
#if defined(PLATFORM_ANDROID)
    if (rich) glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
#endif
    rlEnableBackfaceCulling();
}

void Gfx::post_prepare(Texture2D view, Vector2 sun, float shafts) {
    int w = view.width / 4, h = view.height / 4;
    if (low_[0].texture.width != w || low_[0].texture.height != h)
        for (RenderTexture2D& t : low_) {
            if (t.id) UnloadRenderTexture(t);
            t = LoadRenderTexture(w, h);
            SetTextureFilter(t.texture, TEXTURE_FILTER_BILINEAR);
            SetTextureWrap(t.texture, TEXTURE_WRAP_CLAMP);
        }
    Rectangle to = {0, 0, (float)w, (float)h};
    auto pass = [&](RenderTexture2D& out, Shader& s, Texture2D in, auto&& uniforms) {
        BeginTextureMode(out);
        BeginShaderMode(s);
        uniforms();
        DrawTexturePro(in, {0, 0, (float)in.width, -(float)in.height}, to, {0, 0}, 0, WHITE);
        EndShaderMode();
        EndTextureMode();
    };
    Vector2 across = {1.0f / w, 0}, down = {0, 1.0f / h};
    pass(low_[0], bright, view, [] {});
    pass(low_[1], radial, low_[0].texture, [&] {
        SetShaderValue(radial, loc_sun, &sun, SHADER_UNIFORM_VEC2);
        SetShaderValue(radial, loc_amount, &shafts, SHADER_UNIFORM_FLOAT);
    });
    pass(low_[2], blur, low_[0].texture, [&] { SetShaderValue(blur, loc_step, &across, SHADER_UNIFORM_VEC2); });
    pass(low_[0], blur, low_[2].texture, [&] { SetShaderValue(blur, loc_step, &down, SHADER_UNIFORM_VEC2); });
    pass(low_[2], merge, low_[0].texture, [&] { SetShaderValueTexture(merge, loc_extra, low_[1].texture); });
}

void Gfx::post_draw(Texture2D view, Rectangle to) {
    BeginShaderMode(finish);
    SetShaderValueTexture(finish, loc_glow, low_[2].texture);
    DrawTexturePro(view, {0, 0, (float)view.width, -(float)view.height}, to, {0, 0}, 0, WHITE);
    EndShaderMode();
}

void Gfx::unload() {
    if (rich) {
        for (Shader s : {bright, radial, blur, merge, finish}) UnloadShader(s);
        for (RenderTexture2D& t : low_)
            if (t.id) UnloadRenderTexture(t);
    }
    UnloadShader(lit);
    UnloadShader(cut);
    UnloadShader(sky);
    tex.unload();
}

}  // namespace rr
