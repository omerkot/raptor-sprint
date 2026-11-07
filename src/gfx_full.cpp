// The desktop picture (Mac and Windows): everything on top of the rich tier that needs desktop OpenGL. The view is drawn
// multisampled in high dynamic range; the sun casts real shadows from three nested maps;
// then contact shading, light scattered through the fog along the shadows, and bloom are worked out
// from the finished depth, and the tone curve comes last. Nothing here is built for the handhelds.
#include "gfx.hpp"

#if defined(__APPLE__) || defined(_WIN32)

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#else
#include "gl_windows.hpp"
#endif

namespace rr {

namespace {

constexpr int kShadowSize = 2048;  // each map; four fit in one square texture, of which three are used
constexpr int kCascades = 3;
// How far ahead of the camera each map is centred, and how far round that it reaches (metres).
constexpr float kAhead[kCascades] = {9, 26, 60};
constexpr float kReach[kCascades] = {15, 36, 86};
constexpr float kLightDepth = 420;  // the depth range of all three

const char* QUAD_VS = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
uniform mat4 mvp;
out vec2 vUV;
void main() {
    vUV = vertexTexCoord;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* CAST_VS = R"(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
void main() { gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
const char* CAST_FS = R"(#version 330
void main() {}
)";
const char* CAST_CUT_FS = R"(#version 330
in vec2 vUV;
uniform sampler2D texture0;
void main() { if (texture(texture0, vUV).a < 0.45) discard; }
)";

// Shading where things meet: how much of the sky each point can't see, from the depth alone.
const char* AO_FS = R"(#version 330
in vec2 vUV;
out vec4 fragOut;
uniform sampler2D texture0;  // depth
uniform vec2 proj;           // the projection's x and y scale
uniform vec2 clip;           // near, far
uniform vec2 pixel;
float linear_depth(float d) {
    float z = d * 2.0 - 1.0;
    return 2.0 * clip.x * clip.y / (clip.y + clip.x - z * (clip.y - clip.x));
}
vec3 at(vec2 uv) {
    float z = linear_depth(texture(texture0, uv).r);
    return vec3((uv * 2.0 - 1.0) / proj * z, -z);
}
void main() {
    float d = texture(texture0, vUV).r;
    if (d >= 0.99999) { fragOut = vec4(1.0); return; }
    vec3 P = at(vUV);
    vec3 N = normalize(cross(at(vUV + vec2(pixel.x, 0.0)) - P, at(vUV + vec2(0.0, pixel.y)) - P));
    float radius = 0.55, dist = -P.z;
    vec2 reach = radius * proj * 0.5 / dist;
    float spin = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float sum = 0.0;
    const int TAPS = 8;
    for (int i = 0; i < TAPS; i++) {
        float t = (float(i) + 0.5) / float(TAPS), a = spin + float(i) * 2.399963;
        vec2 uv = vUV + vec2(cos(a), sin(a)) * reach * sqrt(t);
        vec3 v = at(uv) - P;
        float len2 = dot(v, v);
        float fall = 1.0 - smoothstep(radius * radius * 1.5, radius * radius * 6.0, len2);
        sum += max(dot(v, N) - 0.012 * dist * 0.1 - 0.01, 0.0) * inversesqrt(len2 + 0.0001) * fall;
    }
    float ao = 1.0 - sum / float(TAPS) * 2.1;
    // (It only shows where the air hasn't already washed the scene out.)
    ao = mix(1.0, clamp(ao, 0.0, 1.0), exp(-dist * 0.028));
    fragOut = vec4(ao, ao, ao, 1.0);
}
)";
// Smoothing that stops at edges, so the shading stays with the thing that casts it.
const char* AO_BLUR_FS = R"(#version 330
in vec2 vUV;
out vec4 fragOut;
uniform sampler2D texture0;
uniform sampler2D depth;
uniform vec2 stepBy;
void main() {
    float mid = texture(depth, vUV).r, sum = 0.0, total = 0.0;
    for (int i = -2; i <= 2; i++) {
        vec2 uv = vUV + stepBy * (float(i) * 1.5);
        float w = exp(-abs(texture(depth, uv).r - mid) * 2500.0) * (1.0 - abs(float(i)) * 0.25);
        sum += texture(texture0, uv).r * w;
        total += w;
    }
    float v = sum / max(total, 0.0001);
    fragOut = vec4(v, v, v, 1.0);
}
)";

// Sunlight scattered by the fog: walks along each view ray and asks the shadow maps whether the sun
// reaches each step, so the beams really are the gaps between the trunks.
const char* VOLUME_FS = R"(#version 330
in vec2 vUV;
out vec4 fragOut;
uniform sampler2D texture0;  // depth
uniform sampler2DShadow shadowMap;
uniform mat4 shadowVP[3];
uniform mat4 invViewProj;
uniform vec3 camPos;
uniform vec3 toSun;
uniform vec3 sunCol;
uniform float fogDensity;
uniform float strength;
float seen(vec3 p) {
    for (int i = 1; i < 3; i++) {
        vec3 q = (shadowVP[i] * vec4(p, 1.0)).xyz * 0.5 + 0.5;
        if (all(greaterThan(q.xy, vec2(0.005))) && all(lessThan(q.xy, vec2(0.995))))
            return texture(shadowMap, vec3(q.xy * 0.5 + vec2(float(i & 1), float(i >> 1)) * 0.5, q.z));
    }
    return 0.5;
}
void main() {
    float d = texture(texture0, vUV).r;
    vec4 w = invViewProj * vec4(vUV * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec3 end = w.xyz / w.w - camPos;
    float dist = length(end);
    vec3 dir = end / dist;
    float len = min(dist, 80.0);
    const int STEPS = 16;
    float jitter = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    float sum = 0.0;
    // (Steps grow with distance: the near beams are the ones the eye can follow.)
    for (int i = 0; i < STEPS; i++) {
        float a = (float(i) + jitter) / float(STEPS), b = (float(i) + 1.0 + jitter) / float(STEPS);
        float t0 = a * a * len, t1 = b * b * len;
        float t = 0.5 * (t0 + t1);
        sum += seen(camPos + dir * t) * exp(-fogDensity * t) * (t1 - t0);
    }
    float c = dot(dir, toSun);
    float phase = 0.32 + 1.1 * pow(max(c * 0.5 + 0.5, 0.0), 6.0);
    fragOut = vec4(sunCol * (sum * fogDensity * phase * strength), 1.0);
}
)";

const char* BRIGHT_FS = R"(#version 330
in vec2 vUV;
out vec4 fragOut;
uniform sampler2D texture0;
void main() {
    vec3 c = texture(texture0, vUV).rgb;
    float l = dot(c, vec3(0.3, 0.59, 0.11));
    fragOut = vec4(min(c, vec3(6.0)) * smoothstep(0.85, 1.9, l), 1.0);
}
)";
const char* BLUR_FS = R"(#version 330
in vec2 vUV;
out vec4 fragOut;
uniform sampler2D texture0;
uniform vec2 stepBy;
void main() {
    vec3 c = texture(texture0, vUV).rgb * 0.227;
    c += (texture(texture0, vUV + stepBy * 1.385).rgb + texture(texture0, vUV - stepBy * 1.385).rgb) * 0.316;
    c += (texture(texture0, vUV + stepBy * 3.231).rgb + texture(texture0, vUV - stepBy * 3.231).rgb) * 0.070;
    fragOut = vec4(c, 1.0);
}
)";
const char* MERGE_FS = R"(#version 330
in vec2 vUV;
out vec4 fragOut;
uniform sampler2D texture0;  // tight glow
uniform sampler2D wide;
uniform sampler2D beams;
void main() {
    vec3 g = texture(texture0, vUV).rgb * 0.30 + texture(wide, vUV).rgb * 0.42;
    fragOut = vec4(g + texture(beams, vUV).rgb, 1.0);
}
)";
// Everything together, then the tone curve, the grade and a little film grain (which also stops the
// long soft gradients of the fog from banding).
const char* FINISH_FS = R"(#version 330
in vec2 vUV;
out vec4 fragOut;
uniform sampler2D texture0;
uniform sampler2D glow;
uniform sampler2D shade;
uniform float time;
void main() {
    vec3 c = texture(texture0, vUV).rgb;
    c *= texture(shade, vUV).r;
    c += texture(glow, vUV).rgb;
    c = vec3(1.0) - exp(-max(c, vec3(0.0)) * 1.25);
    float l = dot(c, vec3(0.3, 0.59, 0.11));
    c = mix(vec3(l), c, 0.92);
    c = pow(c, vec3(0.84));
    c = mix(c, c * c * (3.0 - 2.0 * c), 0.42);
    l = dot(c, vec3(0.3, 0.59, 0.11));
    c = mix(vec3(l), c, 1.16);
    vec2 q = vUV - 0.5;
    c *= 1.0 - dot(q, q) * 0.34;
    float n = fract(52.9829189 * fract(dot(gl_FragCoord.xy + time * 61.0, vec2(0.06711056, 0.00583715))));
    c += (n - 0.5) * (1.6 / 255.0);
    fragOut = vec4(c, 1.0);
}
)";

struct State {
    Shader cast{}, cast_cut{}, ao{}, ao_blur{}, volume{}, bright{}, blur{}, merge{}, finish{};
    Material cast_mat{}, cast_cut_mat{};
    GLuint shadow_tex = 0, shadow_fbo = 0;
    Matrix shadow_vp[kCascades] = {};
    Vector3 right{}, up{}, ahead{};

    // The view: drawn multisampled, then resolved into these.
    GLuint ms_fbo = 0, ms_color = 0, ms_depth = 0, out_fbo = 0, out_color = 0, out_depth = 0;
    int w = 0, h = 0, samples = 0;
    Texture2D color{}, depth{};
    RenderTexture2D ao_rt[2] = {}, beams[2] = {}, bloom[2] = {}, wide[2] = {}, glow = {};

    Matrix view{}, proj{};
    Vector3 cam{};
    float time = 0, fog_density = 0.03f;
    Vector3 sun_col{1, 1, 1}, to_sun{0, 1, 0};
    int frame = 0;
} F;

RenderTexture2D make_target(int w, int h, int format) {
    RenderTexture2D t = {};
    t.id = rlLoadFramebuffer();
    t.texture = {rlLoadTexture(nullptr, w, h, format, 1), w, h, 1, format};
    rlFramebufferAttach(t.id, t.texture.id, RL_ATTACHMENT_COLOR_CHANNEL0, RL_ATTACHMENT_TEXTURE2D, 0);
    rlDisableFramebuffer();
    SetTextureFilter(t.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(t.texture, TEXTURE_WRAP_CLAMP);
    return t;
}

void drop_target(RenderTexture2D& t) {
    if (t.id) {
        rlUnloadFramebuffer(t.id);
        rlUnloadTexture(t.texture.id);
    }
    t = RenderTexture2D{};
}

template <class Fn>
void pass(RenderTexture2D& out, Shader& s, Texture2D in, Fn&& uniforms) {
    BeginTextureMode(out);
    BeginShaderMode(s);
    uniforms();
    DrawTexturePro(in, {0, 0, (float)in.width, -(float)in.height}, {0, 0, (float)out.texture.width, (float)out.texture.height}, {0, 0}, 0, WHITE);
    EndShaderMode();
    EndTextureMode();
}

void set_float(Shader& s, const char* name, float v) { SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_FLOAT); }
void set_vec2(Shader& s, const char* name, Vector2 v) { SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_VEC2); }
void set_vec3(Shader& s, const char* name, Vector3 v) { SetShaderValue(s, GetShaderLocation(s, name), &v, SHADER_UNIFORM_VEC3); }

}  // namespace

void Gfx::full_init() {
#if defined(_WIN32)
    gl_load();
#endif
    auto quad = [](const char* fs) { return LoadShaderFromMemory(QUAD_VS, fs); };
    F.ao = quad(AO_FS), F.ao_blur = quad(AO_BLUR_FS), F.volume = quad(VOLUME_FS);
    F.bright = quad(BRIGHT_FS), F.blur = quad(BLUR_FS), F.merge = quad(MERGE_FS), F.finish = quad(FINISH_FS);
    std::string cut_vs = std::string("#version 330\n") + kSway +
                         "in vec3 vertexPosition;\nin vec2 vertexTexCoord;\nuniform mat4 mvp;\nuniform float time;\nuniform vec2 wind;\nout vec2 vUV;\n"
                         "void main() {\n    vUV = vertexTexCoord;\n    gl_Position = mvp * vec4(sway(vertexPosition, vertexTexCoord, wind, time), 1.0);\n}\n";
    F.cast = LoadShaderFromMemory(CAST_VS, CAST_FS);
    F.cast_cut = LoadShaderFromMemory(cut_vs.c_str(), CAST_CUT_FS);
    F.cast_mat = LoadMaterialDefault();
    F.cast_mat.shader = F.cast;
    F.cast_cut_mat = LoadMaterialDefault();
    F.cast_cut_mat.shader = F.cast_cut;

    int size = kShadowSize * 2;
    glGenTextures(1, &F.shadow_tex);
    glBindTexture(GL_TEXTURE_2D, F.shadow_tex);
    // (16 bits is 6 mm over the maps' depth, finer than their pixels, and half the memory traffic.)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, size, size, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &F.shadow_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, F.shadow_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, F.shadow_tex, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) TraceLog(LOG_WARNING, "shadow maps: framebuffer incomplete");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    Texture2D map = {F.shadow_tex, size, size, 1, 0};
    for (Shader* s : {&lit, &cut}) s->locs[SHADER_LOC_MAP_NORMAL] = GetShaderLocation(*s, "shadowMap");
    lit_mat.maps[MATERIAL_MAP_NORMAL].texture = map;
    cut_mat.maps[MATERIAL_MAP_NORMAL].texture = map;
}

void Gfx::full_environment(const Biome& b) {
    F.sun_col = b.sun_col, F.to_sun = Vector3Negate(sun_dir), F.fog_density = b.fog_density;
    // The maps' axes stay fixed while the sun does, so moving them never turns their pixels.
    F.ahead = sun_dir;
    F.right = Vector3Normalize(Vector3CrossProduct(F.ahead, {0.13f, 1, 0.07f}));
    F.up = Vector3CrossProduct(F.right, F.ahead);
}

RenderTexture2D Gfx::full_target(int w, int h, int samples) {
    if (w == F.w && h == F.h && samples == F.samples && F.ms_fbo) {
        RenderTexture2D same = {};
        same.id = F.ms_fbo, same.texture = F.color;
        return same;
    }
    if (F.ms_fbo) {
        glDeleteFramebuffers(1, &F.ms_fbo);
        glDeleteFramebuffers(1, &F.out_fbo);
        glDeleteRenderbuffers(1, &F.ms_color);
        glDeleteRenderbuffers(1, &F.ms_depth);
        glDeleteTextures(1, &F.out_color);
        glDeleteTextures(1, &F.out_depth);
        for (RenderTexture2D* t : {&F.ao_rt[0], &F.ao_rt[1], &F.beams[0], &F.beams[1], &F.bloom[0], &F.bloom[1], &F.wide[0], &F.wide[1], &F.glow}) drop_target(*t);
    }
    F.w = w, F.h = h, F.samples = samples;
    glGenRenderbuffers(1, &F.ms_color);
    glBindRenderbuffer(GL_RENDERBUFFER, F.ms_color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, w, h);
    glGenRenderbuffers(1, &F.ms_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, F.ms_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_DEPTH_COMPONENT32F, w, h);
    glGenFramebuffers(1, &F.ms_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, F.ms_fbo);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, F.ms_color);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, F.ms_depth);
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;

    auto texture = [&](GLuint* id, GLenum internal, GLenum format, GLenum type) {
        glGenTextures(1, id);
        glBindTexture(GL_TEXTURE_2D, *id);
        glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    texture(&F.out_color, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
    texture(&F.out_depth, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &F.out_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, F.out_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, F.out_color, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, F.out_depth, 0);
    ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (!ok) TraceLog(LOG_WARNING, "view: framebuffer incomplete");

    F.color = {F.out_color, w, h, 1, PIXELFORMAT_UNCOMPRESSED_R16G16B16A16};
    F.depth = {F.out_depth, w, h, 1, 0};
    const int hdr = PIXELFORMAT_UNCOMPRESSED_R16G16B16A16;
    for (int i = 0; i < 2; i++) {
        F.ao_rt[i] = make_target(w / 2, h / 2, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        F.beams[i] = make_target(w / 4, h / 4, hdr);
        F.bloom[i] = make_target(w / 4, h / 4, hdr);
        F.wide[i] = make_target(w / 16, h / 16, hdr);
    }
    F.glow = make_target(w / 4, h / 4, hdr);
    RenderTexture2D t = {};
    t.id = F.ms_fbo, t.texture = F.color;
    return t;
}

void Gfx::full_frame(float time) {
    F.view = rlGetMatrixModelview(), F.proj = rlGetMatrixProjection();
    F.time = time;
}

void Gfx::cast_shadows(const Camera3D& cam, const std::function<void(Vector3, float, int)>& draw_casters) {
    F.cam = cam.position;
    Vector3 look = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    rlDrawRenderBatchActive();
    Matrix keep_view = rlGetMatrixModelview(), keep_proj = rlGetMatrixProjection();
    rlEnableFramebuffer(F.shadow_fbo);
    rlEnableDepthTest();
    rlEnableDepthMask();
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.6f, 3.0f);
    SetShaderValue(F.cast_cut, GetShaderLocation(F.cast_cut, "time"), &F.time, SHADER_UNIFORM_FLOAT);
    casting = true;
    F.frame++;
    for (int i = 0; i < kCascades; i++) {
        // The nearest map every frame, the next every other, the widest every fourth: their pixels
        // are bigger than the ground covered in between.
        if (F.frame > kCascades && ((i == 1 && (F.frame & 1)) || (i == 2 && (F.frame & 3) != 2))) continue;
        float reach = kReach[i], texel = 2 * reach / kShadowSize;
        Vector3 at = Vector3Add(cam.position, Vector3Scale(look, kAhead[i]));
        // Moved only in whole pixels of the map, so shadow edges hold still as the camera runs.
        float x = std::floor(Vector3DotProduct(at, F.right) / texel) * texel;
        float y = std::floor(Vector3DotProduct(at, F.up) / texel) * texel;
        float z = Vector3DotProduct(at, F.ahead);
        Vector3 c = Vector3Add(Vector3Add(Vector3Scale(F.right, x), Vector3Scale(F.up, y)), Vector3Scale(F.ahead, z));
        Vector3 eye = Vector3Subtract(c, Vector3Scale(F.ahead, kLightDepth * 0.5f));
        Matrix view = MatrixLookAt(eye, c, F.up);
        Matrix proj = MatrixOrtho(-reach, reach, -reach, reach, 1.0, kLightDepth);
        F.shadow_vp[i] = MatrixMultiply(view, proj);
        int ox = (i & 1) * kShadowSize, oy = (i >> 1) * kShadowSize;
        rlViewport(ox, oy, kShadowSize, kShadowSize);
        glScissor(ox, oy, kShadowSize, kShadowSize);
        glClear(GL_DEPTH_BUFFER_BIT);
        rlSetMatrixModelview(view);
        rlSetMatrixProjection(proj);
        draw_casters(c, reach * 1.45f, i);
    }
    casting = false;
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_SCISSOR_TEST);
    rlDisableFramebuffer();
    rlSetMatrixModelview(keep_view);
    rlSetMatrixProjection(keep_proj);
    rlViewport(0, 0, GetRenderWidth(), GetRenderHeight());

    for (Shader* s : {&lit, &cut, &F.volume})
        for (int i = 0; i < kCascades; i++) SetShaderValueMatrix(*s, GetShaderLocation(*s, TextFormat("shadowVP[%d]", i)), F.shadow_vp[i]);
}

void Gfx::cast(const Mesh& mesh, const Matrix& m) { DrawMesh(mesh, F.cast_mat, m); }

void Gfx::cast_cut_mesh(const Mesh& mesh, const Matrix& m, Texture2D t, Vector2 wind) {
    SetShaderValue(F.cast_cut, GetShaderLocation(F.cast_cut, "wind"), &wind, SHADER_UNIFORM_VEC2);
    F.cast_cut_mat.maps[MATERIAL_MAP_DIFFUSE].texture = t;
    rlDisableBackfaceCulling();
    DrawMesh(mesh, F.cast_cut_mat, m);
    rlEnableBackfaceCulling();
}

void Gfx::cut_begin() {
    rlDrawRenderBatchActive();
    rlDisableColorBlend();  // (the edges are smoothed by coverage: blending as well would thin them)
    glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
}

void Gfx::cut_end() {
    rlDrawRenderBatchActive();
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    rlEnableColorBlend();
}

void Gfx::full_post() {
    // The multisamples, down to one colour and one depth per pixel.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, F.ms_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, F.out_fbo);
    glBlitFramebuffer(0, 0, F.w, F.h, 0, 0, F.w, F.h, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    Vector2 proj = {F.proj.m0, F.proj.m5};
    Vector2 clip = {(float)rlGetCullDistanceNear(), (float)rlGetCullDistanceFar()};
    Vector2 half = {2.0f / F.w, 2.0f / F.h};
    pass(F.ao_rt[0], F.ao, F.depth, [&] {
        set_vec2(F.ao, "proj", proj);
        set_vec2(F.ao, "clip", clip);
        set_vec2(F.ao, "pixel", half);
    });
    int loc_depth = GetShaderLocation(F.ao_blur, "depth");
    pass(F.ao_rt[1], F.ao_blur, F.ao_rt[0].texture, [&] {
        SetShaderValueTexture(F.ao_blur, loc_depth, F.depth);
        set_vec2(F.ao_blur, "stepBy", {half.x, 0});
    });
    pass(F.ao_rt[0], F.ao_blur, F.ao_rt[1].texture, [&] {
        SetShaderValueTexture(F.ao_blur, loc_depth, F.depth);
        set_vec2(F.ao_blur, "stepBy", {0, half.y});
    });

    Matrix inv = MatrixInvert(MatrixMultiply(F.view, F.proj));
    Texture2D map = {F.shadow_tex, kShadowSize * 2, kShadowSize * 2, 1, 0};
    pass(F.beams[0], F.volume, F.depth, [&] {
        SetShaderValueTexture(F.volume, GetShaderLocation(F.volume, "shadowMap"), map);
        SetShaderValueMatrix(F.volume, GetShaderLocation(F.volume, "invViewProj"), inv);
        set_vec3(F.volume, "camPos", F.cam);
        set_vec3(F.volume, "toSun", F.to_sun);
        set_vec3(F.volume, "sunCol", F.sun_col);
        set_float(F.volume, "fogDensity", F.fog_density);
        set_float(F.volume, "strength", 1.0f);
    });
    auto blur = [&](RenderTexture2D* t, float px, float py) {
        pass(t[1], F.blur, t[0].texture, [&] { set_vec2(F.blur, "stepBy", {px, 0}); });
        pass(t[0], F.blur, t[1].texture, [&] { set_vec2(F.blur, "stepBy", {0, py}); });
    };
    blur(F.beams, 1.0f / F.beams[0].texture.width, 1.0f / F.beams[0].texture.height);

    pass(F.bloom[0], F.bright, F.color, [] {});
    blur(F.bloom, 1.0f / F.bloom[0].texture.width, 1.0f / F.bloom[0].texture.height);
    pass(F.wide[0], F.blur, F.bloom[0].texture, [&] { set_vec2(F.blur, "stepBy", {0, 0}); });
    blur(F.wide, 1.5f / F.wide[0].texture.width, 1.5f / F.wide[0].texture.height);
    pass(F.glow, F.merge, F.bloom[0].texture, [&] {
        SetShaderValueTexture(F.merge, GetShaderLocation(F.merge, "wide"), F.wide[0].texture);
        SetShaderValueTexture(F.merge, GetShaderLocation(F.merge, "beams"), F.beams[0].texture);
    });
}

void Gfx::full_draw(Rectangle to) {
    BeginShaderMode(F.finish);
    SetShaderValueTexture(F.finish, GetShaderLocation(F.finish, "glow"), F.glow.texture);
    SetShaderValueTexture(F.finish, GetShaderLocation(F.finish, "shade"), F.ao_rt[0].texture);
    set_float(F.finish, "time", std::fmod(F.time, 10.0f));
    DrawTexturePro(F.color, {0, 0, (float)F.w, -(float)F.h}, to, {0, 0}, 0, WHITE);
    EndShaderMode();
}

void Gfx::full_unload() {
    for (Shader s : {F.cast, F.cast_cut, F.ao, F.ao_blur, F.volume, F.bright, F.blur, F.merge, F.finish}) UnloadShader(s);
}

}  // namespace rr

#else  // (the handhelds and Android never reach these)

namespace rr {
void Gfx::full_init() {}
void Gfx::full_environment(const Biome&) {}
RenderTexture2D Gfx::full_target(int, int, int) { return {}; }
void Gfx::full_frame(float) {}
void Gfx::cast_shadows(const Camera3D&, const std::function<void(Vector3, float, int)>&) {}
void Gfx::cast(const Mesh&, const Matrix&) {}
void Gfx::cast_cut_mesh(const Mesh&, const Matrix&, Texture2D, Vector2) {}
void Gfx::cut_begin() {}
void Gfx::cut_end() {}
void Gfx::full_post() {}
void Gfx::full_draw(Rectangle) {}
void Gfx::full_unload() {}
}  // namespace rr

#endif
