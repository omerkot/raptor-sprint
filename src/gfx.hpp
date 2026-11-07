// Rendering: a mesh builder for primitives merged into a few
// meshes, one lit shader (sun dappled by the canopy, sky/ground ambient, fog that glows towards
// the sun, a filmic tone curve), a cut-out variant for leaves, and a sky.
#pragma once
#include <functional>

#include "core.hpp"
#include "regions.hpp"

namespace rr {

class MeshBuilder {
public:
    // World-mapped UVs (for tiling textures such as the forest floor): one repeat per uv_size metres.
    bool world_uv = false;
    Vector2 uv_size = {1, 1};

    void box(Vector3 size, const Matrix& m, Color c);
    void cylinder(float r_top, float r_bottom, float h, int seg, const Matrix& m, Color c, bool caps = true);
    // Ellipsoid of radius r and total height h; a hemisphere spans y = 0..h with its base at the origin.
    void sphere(float r, float h, int seg, int rings, const Matrix& m, Color c, bool hemisphere = false);
    void quad(float w, float h, const Matrix& m, Color c, Vector2 uv0 = {0, 0}, Vector2 uv1 = {1, 1});  // XY plane, faces +Z
    void triangle(Vector3 a, Vector3 b, Vector3 c, Color col);  // already in final space

    size_t vertex_count() const { return pos_.size() / 3; }
    bool empty() const { return pos_.empty(); }
    Mesh build();  // uploads to the GPU

    // One triangle in local space, transformed by m (positions) and nm (normals).
    void tri(const Matrix& m, const Matrix& nm, Vector3 a, Vector3 b, Vector3 c, Vector3 na, Vector3 nb, Vector3 nc,
             Color col, Vector2 ua = {0, 0}, Vector2 ub = {0, 0}, Vector2 uc = {0, 0});
    // A triangle already in world space with per-vertex normals, colours and UVs.
    void vtri(Vector3 a, Vector3 b, Vector3 c, Vector3 na, Vector3 nb, Vector3 nc, Color ca, Color cb, Color cc,
              Vector2 ua, Vector2 ub, Vector2 uc);

private:
    std::vector<float> pos_, nrm_, uv_;
    std::vector<unsigned char> col_;
};

// Painted at startup (textures.cpp).
struct Textures {
    Texture2D white{}, floor{}, path{}, bark{}, rock{}, fern{}, leaves{}, dapple{}, skin{}, raptor{}, sky{}, grass{};
    void paint(bool small, bool large = false);
    void unload();
};

struct Gfx {
    Shader lit{}, cut{}, sky{};
    Material lit_mat{}, cut_mat{}, sky_mat{};
    Textures tex;
    Mesh sky_mesh{};
    int loc_cam[2] = {-1, -1}, loc_time[2] = {-1, -1}, loc_reach[2] = {-1, -1}, loc_emissive[2] = {-1, -1}, loc_fade = -1, loc_wind = -1;
    bool gles = false;
    bool rich = false;  // the full treatment, for GPUs with power to spare: light per pixel, leaf
                        // edges smoothed, then bloom and shafts of light over the finished view
    bool full = false;     // the desktop picture (gfx_full.cpp): high dynamic range, multisampled, real
                           // sun shadows, contact shading, light scattered through the fog
    bool casting = false;  // drawing into the shadow maps: draw() and draw_cut() only leave depth
    Vector3 sun_dir = {0, -1, 0};  // direction the light travels
    Vector3 fog_col = {0.5f, 0.55f, 0.5f};

    void init(bool gles, bool small_textures, bool rich = false, bool full = false);
    // The light and air of a region: sun, ambient, fog.
    void set_environment(const Biome& b);
    void begin_frame(const Camera3D& cam, float time, float reach = 1e4f);
    void draw_sky(const Camera3D& cam);
    // Opaque geometry.
    void draw(const Mesh& mesh, const Matrix& m, Texture2D tex, Color tint = WHITE, Vector3 emissive = {0, 0, 0});
    // Leaves, ferns and the path's ragged edges: cut out by the texture's alpha, lit from both sides.
    // Thins out over the last 30% of `fade_to` metres from the camera and is gone past it.
    // `wind` (full only): how far it sways, all over and towards the tip of each card.
    void draw_cut(const Mesh& mesh, const Matrix& m, Texture2D tex, Color tint = WHITE, float fade_to = 1e4f, Vector2 wind = {0, 0});
    // The finishing passes (rich only): `sun` is where the sun falls on the view (0..1, y up), `shafts`
    // how strongly its light streams through. Then the finished view, in place of drawing it plainly.
    void post_prepare(Texture2D view, Vector2 sun, float shafts);
    void post_draw(Texture2D view, Rectangle to);
    void unload();

    // The full picture. full_target() is the view to draw into (and it's kept here: don't unload it).
    // cast_shadows() calls back for each shadow map with where it's centred, how far it reaches and
    // which it is (0 the nearest), before the view is drawn; full_post() follows the view, and
    // full_draw() puts the result on the screen.
    RenderTexture2D full_target(int w, int h, int samples);
    void cast_shadows(const Camera3D& cam, const std::function<void(Vector3, float, int)>& draw_casters);
    void full_post();
    void full_draw(Rectangle to);

private:
    void full_init();
    void full_environment(const Biome& b);
    void full_frame(float time);
    void cast(const Mesh& mesh, const Matrix& m);
    void cast_cut_mesh(const Mesh& mesh, const Matrix& m, Texture2D tex, Vector2 wind);
    void cut_begin();
    void cut_end();
    void full_unload();
    Shader bright{}, radial{}, blur{}, merge{}, finish{};
    RenderTexture2D low_[3] = {};
    int loc_sun = -1, loc_amount = -1, loc_step = -1, loc_extra = -1, loc_glow = -1;
};

extern Gfx gfx;
extern const char* const kSway;  // the wind, as a GLSL function

}  // namespace rr
