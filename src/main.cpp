// Raptor Sprint: a first-person race through a forest as a velociraptor, built on raylib. On the
// RG DS it uses two screens, the view on top and the blind map (and the menus) below; everywhere
// else, one.
//   --layout dual|single  both screens (on the Mac, stacked in one window, at handheld quality) or
//                         the view alone (the default). The handheld build's dual layout opens a
//                         window per panel.
//   --res WxH             size of one screen, or on the Mac of the window (default 640x480; on the
//                         Mac the whole screen)
//   --pad-position        A is the east button and B the south, whatever the system calls them
//   --low / --high        handheld quality (smaller textures, coarser terrain) or full; --low on the
//                         Mac, with --plain, shows what a handheld shows
//   --render-scale F      render the view at a fraction (or multiple, up to 2) of the screen size
//   --seed N              race this course number straight away (skips the title)
//   --region R            ...in region R (1-6) for --seed
//   --level R-K           race journey trail K of region R straight away (1-based)
//   --start M             start M metres along the course
//   --autoplay            the raptor runs itself (straight into a race)
//   --shot FILE           save a screenshot after --frames N (default 240), then quit
//   --seq DIR             save every --every N'th frame (default 2) into DIR until --frames
//   --screen title|journey|wild|pause|settings  open that screen (screenshots of the menus)
//   --sheet FILE          save the raptor on a plain wall (checking the model); --pose idle|call|
//                         sniff|drink|shake|run|crouch, --view DEGREES round it, --zoom F, --focus X,Y
//   --dump-audio DIR      write every sound as a WAV and quit
//   --mute                no sound
//   --diag                print frame time and draw counts
//   --uncapped            no vsync or frame cap (to measure how long a frame really takes)
//   --rich                the fuller picture for a strong GPU (on Android, always)
//   --plain               without it, or the Mac's picture
//   --detail 0|1|2        high, medium or low detail (saved like the setting)
//   --difficulty 0|1|2    easy, normal or hard (saved like the setting)
#include <sys/stat.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ctime>

#include "audio.hpp"
#include "gfx.hpp"
#include "hud.hpp"
#include "lower_window.hpp"
#include "outro.hpp"
#include "menu.hpp"
#include "player.hpp"
#include "progress.hpp"
#include "raptor.hpp"
#include "regions.hpp"
#include "settings.hpp"
#include "ui.hpp"
#include "web.hpp"
#include "world.hpp"

#if defined(__APPLE__)
#include <objc/message.h>
#include <objc/runtime.h>
#endif

using namespace rr;

// Full screen. On the Mac it's the system's own, the same as the window's green button: a space of
// its own, with the menu bar and the Dock out of the way. (A window the size of the screen stays
// under both.) On Windows a borderless window covering the screen is the way to do it.
static bool is_full_screen() {
#if defined(__APPLE__)
    auto style = (unsigned long (*)(void*, SEL))objc_msgSend;
    return style(GetWindowHandle(), sel_registerName("styleMask")) & (1ul << 14);
#else
    return IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE);
#endif
}
static void toggle_full_screen() {
#if defined(__APPLE__)
    auto toggle = (void (*)(void*, SEL, void*))objc_msgSend;
    toggle(GetWindowHandle(), sel_registerName("toggleFullScreen:"), nullptr);
#else
    ToggleBorderlessWindowed();
#endif
}

#if defined(PLATFORM_ANDROID)
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <android_native_app_glue.h>

extern "C" struct android_app* GetAndroidApp(void);
const char* rr::android_data_dir() { return GetAndroidApp()->activity->internalDataPath; }

// Sound stops when the player leaves the game (the loop waits then, but the forest plays on its own).
static void (*raylib_on_cmd)(struct android_app*, int32_t);
static void on_app_cmd(struct android_app* app, int32_t cmd) {
    if (cmd == APP_CMD_LOST_FOCUS || cmd == APP_CMD_PAUSE) SetMasterVolume(0);
    if (cmd == APP_CMD_GAINED_FOCUS) SetMasterVolume(1);
    raylib_on_cmd(app, cmd);
}

// A view target with multisampling done inside the GPU's tile memory, which costs almost nothing
// on these GPUs. (Falls back on a plain one where that isn't there.)
static RenderTexture2D load_view_target(int w, int h) {
    auto storage = (PFNGLRENDERBUFFERSTORAGEMULTISAMPLEEXTPROC)eglGetProcAddress("glRenderbufferStorageMultisampleEXT");
    auto attach = (PFNGLFRAMEBUFFERTEXTURE2DMULTISAMPLEEXTPROC)eglGetProcAddress("glFramebufferTexture2DMultisampleEXT");
    if (!storage || !attach) return LoadRenderTexture(w, h);
    RenderTexture2D t = {};
    t.id = rlLoadFramebuffer();
    rlEnableFramebuffer(t.id);
    t.texture = {rlLoadTexture(nullptr, w, h, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    t.depth = {0, w, h, 1, 19};
    attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture.id, 0, 4);
    GLuint depth = 0;
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    storage(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24_OES, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    t.depth.id = depth;
    bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    rlDisableFramebuffer();
    TraceLog(LOG_INFO, "view target: %dx%d, multisampled %s", w, h, ok ? "yes" : "no");
    return ok ? t : LoadRenderTexture(w, h);
}
#elif defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>

// A multisampled view target, read back into a plain texture once the view is drawn. (WebGL has no
// multisampling inside the GPU's tile memory, but on a phone's GPU this comes close.)
static GLuint view_color, view_depth, view_plain;
static void unload_view_target(RenderTexture2D t) {
    glDeleteRenderbuffers(1, &view_color);
    glDeleteRenderbuffers(1, &view_depth);
    glDeleteFramebuffers(1, &view_plain);
    rlUnloadFramebuffer(t.id);
    rlUnloadTexture(t.texture.id);
}
static RenderTexture2D load_view_target(int w, int h) {
    RenderTexture2D t = {};
    t.texture = {rlLoadTexture(nullptr, w, h, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, 1), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    glGenFramebuffers(1, &view_plain);
    glBindFramebuffer(GL_FRAMEBUFFER, view_plain);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.texture.id, 0);
    glGenRenderbuffers(1, &view_color);
    glBindRenderbuffer(GL_RENDERBUFFER, view_color);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, w, h);
    glGenRenderbuffers(1, &view_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, view_depth);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24, w, h);
    glGenFramebuffers(1, &t.id);
    glBindFramebuffer(GL_FRAMEBUFFER, t.id);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, view_color);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, view_depth);
    t.depth = {0, w, h, 1, 19};
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return t;
}
static void resolve_view_target(RenderTexture2D t) {
    int w = t.texture.width, h = t.texture.height;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, t.id);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, view_plain);
    glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    const GLenum spent[2] = {GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT};
    glInvalidateFramebuffer(GL_READ_FRAMEBUFFER, 2, spent);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
#else
static RenderTexture2D load_view_target(int w, int h) { return LoadRenderTexture(w, h); }
#endif

struct Options {
    std::string sheet, pose = "idle";
    int end_r = 0, end_k = 0;  // straight into the endings test (1-based; needs kTestTools)
    float view = 0, zoom = 1, focus_x = 0.2f, focus_y = 0.82f;
    bool dual = true, low = false, rich = false, full = false, autoplay = false, diag = false, uncapped = false, mute = false;
    bool sized = false;  // --res was given
    bool upright = false;  // in the browser: a phone held upright, with the menus and the finish card laid out to suit
    int detail = -1, difficulty = -1;
    int width = 640, height = 480, seed = 0, frames = 0, every = 2, region = 0, level_r = -1, level_k = 0;
    float render_scale = 1.0f, start = 0;
    std::string shot, seq, screen, dump_audio;
};

// The endings test (a title menu item, and --ending R-K) and its frame recorder (grab.on). Off in
// normal builds; set to true to bring them back.
constexpr bool kTestTools = false;

enum class Screen { Title, Journey, Wild, Settings, Race, Paused, Endings, Steering };
enum ItemId {
    JOURNEY = 1, WILD, SETTINGS, EXIT, DIFFICULTY = 6,
    J_REGION = 30, J_LEVEL, J_BACK = J_LEVEL + kLevelsPerRegion,
    W_REGION = 40, W_LENGTH, W_RUN, W_BACK,
    ENDINGS = 5, E_REGION = 50, E_LEVEL, E_BACK = E_LEVEL + kLevelsPerRegion,  // the endings test
    FX = 10, FOREST, SPRINT, BOB, FPS, DETAIL, DISPLAY, BACK,
    RESUME = 20, RESTART, PAUSE_SETTINGS, TO_TITLE, PAUSE_EXIT,
    STEERING = 60, ST_SPEED, ST_TRAVEL, ST_CURVE, ST_DEAD, ST_SMOOTH, ST_RESET, ST_BACK,  // (the touch stick, in the browser)
};

static Controls read_controls() {
    Controls in;
    int pad = active_pad();
    bool gp = pad >= 0;
    float stick = gp ? GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_X) : 0.0f;
    if (std::fabs(stick) < 0.2f) stick = 0;
    in.steer = clampf(stick + (IsKeyDown(KEY_RIGHT) ? 1.0f : 0.0f) - (IsKeyDown(KEY_LEFT) ? 1.0f : 0.0f) +
                          (gp && IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) ? 1.0f : 0.0f) -
                          (gp && IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_LEFT) ? 1.0f : 0.0f),
                      -1, 1);
    // A jumps, B ducks.
    in.jump = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_X) || (gp && IsGamepadButtonPressed(pad, pad_a));
    in.duck = IsKeyDown(KEY_Z) || IsKeyDown(KEY_C) || (gp && IsGamepadButtonDown(pad, pad_b));
    in.sprint = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT) ||
                (gp && (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1) || IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2)));
    in.brake = IsKeyDown(KEY_DOWN) || (gp && IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_DOWN));
#if defined(__EMSCRIPTEN__)
    touch_controls(in);
#endif
    return in;
}

// The camera sits in the raptor's head: stride bob and sway, a dip on landing, a lean into turns,
// and a shake when it stumbles. `bob` scales the motion (settings).
static Camera3D head_camera(const Player& p, float time, float bob_scale, Matrix* head) {
    float run = clampf(p.speed / 12.0f, 0, 1) * bob_scale;
    float bob = std::sin(p.stride * 2.0f) * 0.035f * run - p.land * 0.12f * std::max(0.4f, bob_scale);
    float sway = std::sin(p.stride) * 0.025f * run;
    Vector3 f = heading_dir(p.yaw), r = heading_right(p.yaw);
    Vector3 eye = Vector3Add(p.pos, {0, p.eye_height() + bob, 0});
    eye = Vector3Add(eye, Vector3Scale(r, sway));
    if (p.shake > 0) {
        float s = p.shake * 0.07f;
        eye = Vector3Add(eye, {s * std::sin(time * 57.0f), s * std::sin(time * 43.0f + 1.0f), s * std::sin(time * 61.0f + 2.0f)});
    }
    float pitch = -0.085f - p.land * 0.06f + std::sin(p.stride * 2.0f + 0.6f) * 0.01f * run;
    Vector3 look = Vector3Normalize(Vector3Add(Vector3Scale(f, std::cos(pitch)), {0, std::sin(pitch), 0}));
    float roll = -p.steer_vis * 0.05f * clampf(p.speed / 10.0f, 0, 1) + std::sin(p.stride) * 0.008f * run;
    Vector3 up = Vector3RotateByAxisAngle({0, 1, 0}, look, roll);
    Vector3 right = Vector3Normalize(Vector3CrossProduct(look, up));
    up = Vector3CrossProduct(right, look);
    Camera3D cam = {};
    cam.position = eye;
    cam.target = Vector3Add(eye, look);
    cam.up = up;
    cam.fovy = 52.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    *head = basis(right, up, Vector3Negate(look), eye);
    return cam;
}

// A point on the trail `s` metres from the start, on a smooth curve through the 1 m samples.
static Vector3 trail_at(const Course& c, float s) {
    int n = (int)c.pts.size();
    s = clampf(s, 0, n - 1.001f);
    int i = (int)s;
    float t = s - i, t2 = t * t, t3 = t2 * t;
    auto at = [&](int k) { return c.pts[std::clamp(k, 0, n - 1)].p; };
    Vector3 a = at(i - 1), b = at(i), d = at(i + 1), e = at(i + 2);
    auto spline = [&](float a, float b, float d, float e) {
        return 0.5f * (2 * b + (d - a) * t + (2 * a - 5 * b + 4 * d - e) * t2 + (3 * b - a - 3 * d + e) * t3);
    };
    return {spline(a.x, b.x, d.x, e.x), spline(a.y, b.y, d.y, e.y), spline(a.z, b.z, d.z, e.z)};
}

// The journey and wild trail menus: drifting slowly down the trail at head height, looking
// along the curve ahead. Near the end of the trail it starts again; `dark` (0-1) fades the view
// through black around that cut.
static Camera3D title_camera(const Course& c, float t, float* dark) {
    float loop = std::max(50.0f, c.length - 40.0f), u = std::fmod(t * 4.0f, loop), s = u + 5.0f;
    *dark = 1.0f - clampf((std::min(u, loop - u) - 0.5f) / 3.5f, 0, 1);
    Vector3 p = trail_at(c, s), ahead = {0, 0, 0};
    for (float d = 8; d <= 16; d += 2) ahead = Vector3Add(ahead, Vector3Scale(trail_at(c, s + d), 0.2f));
    Camera3D cam = {};
    cam.position = Vector3Add(p, {std::sin(t * 0.3f) * 0.4f, 1.9f + 0.1f * std::sin(t * 0.5f), 0});
    cam.target = Vector3Add(ahead, {0, 1.3f, 0});
    cam.up = {0, 1, 0};
    cam.fovy = 52.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    return cam;
}

// The title's stage: a straight stretch of trail with nothing on it, where the raptor shows
// itself off, and the camera standing back, looking a little to its left so it stands on the right.
// `beside_menu` is the screen's shape when the menu is on the same screen: then the camera turns just
// far enough, on that shape, to put the raptor in the middle of the space right of the menu.
struct Stage {
    Matrix frame;  // x right, y up, z back towards the camera
    Camera3D cam;
};
static Stage title_stage(const Course& c, float t, float beside_menu) {
    int n = (int)c.pts.size(), at = std::min(n - 1, 30);
    for (int i = 30; i + 20 < n; i++) {
        bool clear = std::fabs(angle_diff(c.pts[i - 10].yaw, c.pts[i + 4].yaw)) < 0.15f;
        for (const Obstacle& ob : c.obstacles)
            if (ob.idx > i - 14 && ob.idx < i + 8) clear = false;
        if (clear) {
            at = i;
            break;
        }
    }
    const PathPt& p = c.pts[at];
    Vector3 up = {0, 1, 0}, f = {std::sin(p.yaw), 0, -std::cos(p.yaw)}, r = Vector3CrossProduct(f, up);
    Stage s;
    s.frame = basis(r, up, Vector3Negate(f), Vector3Add(p.p, {0, 0.04f, 0}));  // (on the trail strip, which lies on the ground)
    auto place = [&](float back, float side, float height) {
        return Vector3Add(p.p, Vector3Add(Vector3Scale(f, -back), Vector3Add(Vector3Scale(r, side), Vector3Scale(up, height))));
    };
    s.cam = {};
    s.cam.position = place(5.7f, -0.2f + 0.25f * std::sin(t * 0.21f), 1.5f + 0.08f * std::sin(t * 0.37f));
    s.cam.fovy = 52.0f;
    float look = -1.75f;
    if (beside_menu > 0) {
        float spot = 0.54f;  // (from the middle of the screen to its right edge: the menu ends at 54% across)
        float off = std::atan(spot * beside_menu * std::tan(s.cam.fovy * 0.5f * DEG2RAD));
        look = -0.2f + 6.0f * std::tan(std::atan(0.2f / 5.7f) - off);
    }
    s.cam.target = place(-0.3f, look, 0.7f);
    s.cam.up = up;
    s.cam.projection = CAMERA_PERSPECTIVE;
    return s;
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IOLBF, 0);  // (log.txt stays current, even if the game has to be stopped)
    Options o;
#if defined(PLATFORM_ANDROID)
    o.dual = false, o.rich = true;  // (one screen, and a GPU with power to spare)
    // There's no command line, but a file of arguments in the app's storage does the same job.
    static std::vector<std::string> file_args = {"raptorsprint"};
    static std::vector<char*> file_argv;
    {
        std::ifstream in(save_path("args.txt"));
        for (std::string w; in >> w;) file_args.push_back(w);
        for (std::string& a : file_args) file_argv.push_back(a.data());
        argc = (int)file_argv.size(), argv = file_argv.data();
    }
#elif defined(__EMSCRIPTEN__)
    o.dual = false, o.rich = true;  // (a phone's GPU is at least the RG406H's)
#elif defined(GRAPHICS_API_OPENGL_ES2)
    o.low = true, o.dual = false;
#else
    o.dual = false, o.rich = o.full = true;  // (a Mac or a Windows PC: one window, and everything it can show)
#endif
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--layout") o.dual = std::string(next()) != "single";
        else if (a == "--res") sscanf(next(), "%dx%d", &o.width, &o.height), o.sized = true;
        else if (a == "--pad-position") pad_a = GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, pad_b = GAMEPAD_BUTTON_RIGHT_FACE_DOWN;
        else if (a == "--low") o.low = true;
        else if (a == "--high") o.low = false;
        else if (a == "--rich") o.rich = true;
        else if (a == "--plain") o.rich = o.full = false;
        else if (a == "--render-scale") o.render_scale = clampf((float)atof(next()), 0.25f, 2.0f);
        else if (a == "--seed") o.seed = atoi(next());
        else if (a == "--region") o.region = (int)clampf(atoi(next()) - 1, 0, kRegions - 1);
        else if (a == "--level") {
            int r = 1, k = 1;
            sscanf(next(), "%d-%d", &r, &k);
            o.level_r = (int)clampf(r - 1, 0, kRegions - 1), o.level_k = (int)clampf(k - 1, 0, kLevelsPerRegion - 1);
        }
        else if (a == "--start") o.start = (float)atof(next());
        else if (a == "--autoplay") o.autoplay = true;
        else if (a == "--shot") o.shot = next();
        else if (a == "--ending") sscanf(next(), "%d-%d", &o.end_r, &o.end_k);
        else if (a == "--sheet") o.sheet = next();
        else if (a == "--pose") o.pose = next();
        else if (a == "--view") o.view = (float)atof(next());
        else if (a == "--zoom") o.zoom = (float)atof(next());
        else if (a == "--focus") sscanf(next(), "%f,%f", &o.focus_x, &o.focus_y);
        else if (a == "--seq") o.seq = next();
        else if (a == "--every") o.every = std::max(1, atoi(next()));
        else if (a == "--frames") o.frames = atoi(next());
        else if (a == "--screen") o.screen = next();
        else if (a == "--dump-audio") o.dump_audio = next();
        else if (a == "--mute") o.mute = true;
        else if (a == "--diag") o.diag = true;
        else if (a == "--uncapped") o.uncapped = true;
        else if (a == "--detail") o.detail = (int)clampf(atoi(next()), 0, 2);
        else if (a == "--difficulty") o.difficulty = (int)clampf(atoi(next()), 0, 2);
    }
#if defined(PLATFORM_ANDROID)
    if (o.diag) freopen(save_path("log.txt").c_str(), "w", stdout), setvbuf(stdout, nullptr, _IOLBF, 0);
#endif
    if (!o.dump_audio.empty()) {
        Audio::dump(o.dump_audio);
        return 0;
    }
    if (o.low || o.dual) o.full = false;
    bool capture = !o.shot.empty() || !o.seq.empty();
    if (capture && o.frames <= 0) o.frames = 240;
    bool straight_to_race = o.autoplay || o.seed > 0 || o.level_r >= 0 || (capture && (o.screen.empty() || o.screen == "countdown"));

#if defined(PLATFORM_DESKTOP_SDL)
    bool panels = true;  // try a window per panel
#else
    bool panels = false;
#endif
    int win_w = o.width, win_h = o.dual && !panels ? o.height * 2 : o.height;
#if defined(PLATFORM_ANDROID)
    win_w = win_h = 0;  // the whole screen
#endif
    if (o.full && !o.sized) win_w = 1024, win_h = 640;  // (it goes on to fill the screen, unless told not to)
#if defined(__EMSCRIPTEN__)
    o.width = win_w = web_view_width(), o.height = win_h = web_view_height();
    o.upright = win_h > win_w;
    ui_design_width = o.upright ? 560.0f : 640.0f;
    SetConfigFlags(0);  // (the view has multisampling of its own, and the page does the resizing)
#else
    SetConfigFlags((o.uncapped ? 0 : FLAG_VSYNC_HINT) | (o.full ? FLAG_WINDOW_RESIZABLE : o.low ? 0 : FLAG_MSAA_4X_HINT));
#endif
    SetTraceLogLevel(o.diag ? LOG_INFO : LOG_WARNING);
    InitWindow(win_w, win_h, "Raptor Sprint");
    if (!IsWindowReady()) return 1;
    if (o.full) {
        o.width = GetScreenWidth(), o.height = GetScreenHeight();
        if (o.diag) printf("[window] %dx%d, drawn at %dx%d\n", o.width, o.height, GetRenderWidth(), GetRenderHeight());
    }
#if defined(PLATFORM_ANDROID)
    o.width = GetScreenWidth(), o.height = GetScreenHeight();
    if (o.diag) printf("[screen] %dx%d\n", o.width, o.height);
    if (o.uncapped) eglSwapInterval(eglGetCurrentDisplay(), 0);  // (to measure the true frame time)
#endif
    SetExitKey(KEY_NULL);
    // (Just above 60: the handhelds' screens refresh a little faster, and a cap of exactly 60 fights
    // their vsync and settles on 58.)
#if defined(__EMSCRIPTEN__)
    SetTargetFPS(0);  // (the browser sets the pace: web_wait_frame)
#else
    SetTargetFPS(o.uncapped ? 0 : o.full ? std::max(60, GetMonitorRefreshRate(GetCurrentMonitor())) + 2 : 62);
#endif
    SetRandomSeed((unsigned)time(nullptr));
    LowerWindow lower;
    if (panels) {
        panels = o.dual && lower.open(o.width, o.height);
        o.dual = panels;  // no second panel: the view alone, with a small map over it
        if (o.diag) printf("[panels] %s\n", panels ? "upper and lower windows" : "one window");
    }
    rlSetClipPlanes(0.05, 400.0);
    ui_load_font(asset_path("fonts/Nunito-ExtraBold.ttf"));

    RenderTexture2D lower_rt = {};
    if (panels) lower_rt = LoadRenderTexture(o.width, o.height);
    Rectangle top = {0, 0, (float)o.width, (float)o.height};
    Rectangle lower_rect = {0, (float)o.height, (float)o.width, (float)o.height};

    // A card on both screens while something slow happens (painting textures, growing a course).
    auto loading = [&]() {
        auto card = [&](Rectangle r, const char* line) {
            DrawRectangleRec(r, Color{14, 19, 16, 255});
            float k = ui_scale(r);
            const char* t = "RAPTOR SPRINT";
            ui_draw(t, r.x + r.width / 2 - ui_measure(t, 40 * k) / 2, r.y + r.height * 0.38f, 40 * k, Color{228, 234, 220, 255});
            ui_draw(line, r.x + r.width / 2 - ui_measure(line, 22 * k) / 2, r.y + r.height * 0.38f + 56 * k, 22 * k, Color{128, 150, 124, 255});
        };
        if (panels) {
            BeginTextureMode(lower_rt);
            card({0, 0, (float)o.width, (float)o.height}, "");
            EndTextureMode();
        }
        BeginDrawing();
        ClearBackground(BLACK);
        card(top, "Loading");
        if (o.dual && !panels) card(lower_rect, "");
        EndDrawing();
        if (panels) lower.present(lower_rt.texture);
#if defined(__EMSCRIPTEN__)
        web_wait_frame();  // (nothing reaches the screen until the browser gets a turn)
#endif
    };
    loading();

#if defined(GRAPHICS_API_OPENGL_ES2)
    gfx.init(true, o.low, o.rich);
#else
    gfx.init(false, o.low, o.rich, o.full);
#endif
    // The view's depth range. raylib's default near plane (1 cm) leaves a 16-bit depth buffer only
    // a few centimetres of precision a few metres away, and surfaces close together (a pool on its
    // bank, a shadow on the trail) flicker. Nothing is drawn nearer than the snout, 30 cm off.
    rlSetClipPlanes(0.08, 500.0);
    if (!o.sheet.empty()) {
        Biome wall = region(0).biome;
        wall.fog_density = 0;
        wall.sun_col = {1.15f, 1.1f, 1.0f}, wall.to_sun = {0.2f, 0.8f, 0.6f};
        wall.sky_amb = {0.75f, 0.75f, 0.72f}, wall.ground_amb = {0.5f, 0.46f, 0.42f};
        gfx.set_environment(wall);
        static const std::pair<const char*, RAct> acts[] = {{"idle", RAct::Idle}, {"call", RAct::Call}, {"sniff", RAct::Sniff}, {"drink", RAct::Drink},
                                                            {"shake", RAct::Shake}, {"run", RAct::Run}, {"crouch", RAct::Leap}};
        RaptorShow r;
        RAct act = RAct::Idle;
        for (auto& a : acts)
            if (o.pose == a.first) act = a.second;
        r.place({0, 0}, kPi / 2);
        r.script({{act, {act == RAct::Run ? 400.0f : 0.0f, 0}, act == RAct::Run ? 57.0f : 0.0f}});
        Camera3D cam = {};
        float dist = 5.0f / o.zoom;
        cam.target = {o.focus_x, o.focus_y, 0};
        cam.position = {o.focus_x + dist * std::sin(o.view * DEG2RAD), o.focus_y + 0.08f, dist * std::cos(o.view * DEG2RAD)};
        cam.up = {0, 1, 0}, cam.fovy = 36.0f, cam.projection = CAMERA_PERSPECTIVE;
        for (int i = 0; i < (o.frames > 0 ? o.frames : 60); i++) {
            r.update(1.0f / 60, cam.position);
            if (act == RAct::Run) cam.target.x = r.pos().x * RaptorShow::kScale + o.focus_x, cam.position.x = cam.target.x + dist * std::sin(o.view * DEG2RAD);
            BeginDrawing();
            ClearBackground(Color{236, 234, 228, 255});
            BeginMode3D(cam);
            gfx.begin_frame(cam, 0);
            r.draw(MatrixIdentity());
            EndMode3D();
            EndDrawing();
        }
        Image img = LoadImageFromScreen();
        ExportImage(img, o.sheet.c_str());
        return 0;
    }
    Settings set;
    std::string set_path = save_path("settings.cfg");
    set.load(set_path);
    if (!o.mute) audio.init();
#if defined(PLATFORM_ANDROID)
    if (!o.mute) raylib_on_cmd = GetAndroidApp()->onAppCmd, GetAndroidApp()->onAppCmd = on_app_cmd;
#endif
    auto apply_audio = [&]() {
        audio.fx_volume = set.fx / 10.0f;
        audio.forest_volume = set.forest / 10.0f;
    };
    apply_audio();

    Progress prog;
    std::string prog_path = save_path("journey.txt");
    prog.load(prog_path);
    if (o.difficulty >= 0) set.difficulty = o.difficulty;
    prog.difficulty = set.difficulty;

    // The course on show, and what it was made from.
    Course course;
    World world;
    RaptorShow showcase;  // the raptor on the title screen
    Outro outro;          // and at the end of a trail
    bool world_ready = false;
    int course_seed = -1, course_region = -1, course_difficulty = -1;
    float course_len = 0, course_par = 0;
    std::string course_name;
    Best best;
    std::string best_path;
    int shown_env = -1;  // the region whose light is set
    auto environment = [&](int r) {
        if (r != shown_env) gfx.set_environment(region(r).biome), shown_env = r;
    };
    auto load_course = [&](int seed, float len, int reg, const std::string& name) {
        course_name = name;
        int diff = set.difficulty;  // (each difficulty keeps its own best times; easy's files keep their old names)
        best_path = save_path(diff == 0 ? TextFormat("best_%d_%d.txt", seed, (int)len) : TextFormat("best_%d_%d_%s.txt", seed, (int)len, diff == 1 ? "normal" : "hard"));
        best.load(best_path);
        environment(reg);
        if (seed == course_seed && len == course_len && reg == course_region && diff == course_difficulty && world_ready) return;
        loading();
        double t0 = GetTime();
        course.lush = o.rich;
        course.lusher = o.full;
        course.generate(seed, len, reg, diff);
        if (world_ready) world.unload();
        world = World{};
        world.build(course, o.low, o.full);
        world_ready = true;
        course_seed = seed;
        course_len = len;
        course_region = reg;
        course_difficulty = diff;
        course_par = par_time(course, tuning_for(diff));
        if (o.diag) printf("[load] course %d (%.0f m, region %d) in %.2f s, %d trees, %d plants, %d obstacles, target %.1f s\n", seed, len,
                           reg + 1, GetTime() - t0, (int)course.trees.size(), (int)course.plants.size(), (int)course.obstacles.size(), course_par);
    };
    // What's being raced: a journey trail (region, trail) or a wild one.
    bool journey = true;
    int race_r = 0, race_k = 0;
    auto load_level = [&](int r, int k) {
        journey = true, race_r = r, race_k = k;
        load_course(level_seed(r, k), level_length(r, k), r, TextFormat("%s  %d", region(r).name, k + 1));
    };
    auto load_wild = [&]() {
        journey = false;
        int seed = GetRandomValue(1000, 99999);
        load_course(seed, length_metres(set.length), set.wild_region, TextFormat("Wild trail %d", seed));
    };
    if (!prog.region_open(set.wild_region)) set.wild_region = 0;
    int browse_r = 0, browse_k = 0;
    prog.next_up(&browse_r, &browse_k);
    if (o.region > 0 && o.level_r < 0 && o.seed <= 0) browse_r = o.region;  // (screenshots of the journey menu)
    if (o.level_r >= 0) load_level(o.level_r, o.level_k);
    else if (o.seed > 0) journey = false, load_course(o.seed, 1600.0f, o.region, TextFormat("Trail %d", o.seed));
    else load_level(browse_r, browse_k);  // (the title shows the next trail to run)

    // The view's resolution: --render-scale, else the Detail setting. A single-screen handheld
    // (the RG34XXSP's smaller GPU) starts at Medium.
    bool scale_forced = o.render_scale != 1.0f;
    // (The Mac starts at High, and on first run steps down until it holds its frame rate: see below.)
#if defined(__EMSCRIPTEN__)
    touch_steering(set.steer_speed, set.steer_travel, set.steer_curve, set.steer_dead, set.steer_smooth);
    bool settle_detail = set.detail < 0 && o.detail < 0 && !capture;  // (phones differ more than Macs)
#else
    bool settle_detail = o.full && set.detail < 0 && o.detail < 0 && !capture;
#endif
    if (set.detail < 0) set.detail = (o.low && !panels) ? 1 : 0;
    if (o.detail >= 0) set.detail = o.detail;
    // Detail also sets how far things are drawn: undergrowth is what costs most on a small GPU.
    auto draw_range = [&]() {
        DrawRange r;
        // (On the RG34XXSP the view's pixel count costs more than distance: Low keeps most of the
        // view and saves by resolution.)
        if (o.full) {
            int d = set.detail;
            r.far = d == 0 ? 165 : d == 1 ? 140 : 120, r.plants = d == 0 ? 100 : d == 1 ? 80 : 60;
            r.canopy = d == 0 ? 140 : d == 1 ? 112 : 95, r.grass = d == 0 ? 56 : d == 1 ? 46 : 34;
        }
        else if (set.detail == 0 && o.rich) r.far = 125, r.plants = r.canopy = 105;
        else if (set.detail == 0) r.far = o.low ? 95.0f : 105.0f, r.plants = r.canopy = o.low ? 72.0f : 85.0f;
        else if (set.detail == 1) r.far = 92, r.plants = 40, r.canopy = 56;
        else r.far = 88, r.plants = 32, r.canopy = 48;
        return r;
    };
    int rw = 0, rh = 0;
    RenderTexture2D view = {};
    auto make_view = [&]() {
        float sc = scale_forced ? o.render_scale : o.full ? mac_detail_scale(set.detail) : detail_scale(set.detail);
        // (On the Mac, every pixel the display has: a window's size is in points, half as many.)
        int w = (int)((o.full ? GetRenderWidth() : o.width) * sc), h = (int)((o.full ? GetRenderHeight() : o.height) * sc);
        if (w == rw && h == rh && view.id) {
            if (o.full) view = gfx.full_target(rw, rh, set.detail == 0 ? 4 : 2);
            return;
        }
#if defined(__EMSCRIPTEN__)
        if (view.id) unload_view_target(view);
#else
        if (view.id && !o.full) UnloadRenderTexture(view);
#endif
        rw = w, rh = h;
        // (High multisamples 4x. Below that it's a Mac that's working hard, and at this density
        // 2x is hard to tell from it.)
        view = o.full ? gfx.full_target(rw, rh, set.detail == 0 ? 4 : 2) : load_view_target(rw, rh);
        SetTextureFilter(view.texture, TEXTURE_FILTER_BILINEAR);
    };
    make_view();

    Player player;
    HudState hud;
    bool sprint_on = false, sprint_was = false;
    int count_shown = 4, steps = 0;
    // The endings test. Runs itself in from just before the finish; nothing is saved.
    bool test_end = false;
    int end_r = 0, end_k = 0;
    // Each obstacle as it's passed: not yet (0), being passed (1), done (2); and whether it was clean.
    std::vector<int> ob_state;
    std::vector<int> ob_hits;
    std::vector<char> ob_dirty;
    auto restart = [&]() {
        player.tune = tuning_for(course.difficulty);
        player.reset(course);
        outro.stop();
        ob_state.assign(course.obstacles.size(), 0);
        ob_hits.assign(course.obstacles.size(), 0);
        ob_dirty.assign(course.obstacles.size(), 0);
        float start_at = test_end ? course.length - 6 : o.start;
        if (start_at > 0) {
            int i = std::min((int)course.pts.size() - 3, (int)start_at);
            player.pos = course.pts[i].p;
            player.yaw = course.pts[i].yaw;
            player.hint = i;
            player.near = course.nearest(player.pos, i);
            while (player.next_check < (int)course.checkpoints.size() && course.checkpoints[player.next_check] <= i) player.next_check++;
            for (size_t k = 0; k < course.obstacles.size(); k++)
                if (course.obstacles[k].idx < i + 3) ob_state[k] = 2;  // (behind the start: not counted)
        }
        hud = HudState{};
        hud.trail = course_name;
        hud.par = course_par;
        if (journey) {
            hud.intro_title = TextFormat("%s   trail %d of %d   %s", region(race_r).name, race_k + 1, kLevelsPerRegion, difficulty_name(course.difficulty));
            if (race_k == 0) {
                for (const char* line : region(race_r).news) hud.intro_lines.push_back(std::string("NEW  ") + line);
                if (race_r == 0) hud.intro_lines.push_back("NEW  Sprint: R (uses breath)   Brake: Down");  // (ui_draw names the keys on a Mac)
                hud.hold = !o.autoplay && !test_end;  // as long as it takes to read
            }
        } else {
            hud.intro_title = std::string("Wild trail in ") + region(course_region).name + "   " + difficulty_name(course.difficulty);
        }
        sprint_on = false;
        count_shown = 4;
        if (test_end || (capture && o.screen != "countdown")) hud.phase = Phase::Race, player.speed = player.tune.run_speed;
    };
    restart();

    Screen screen = straight_to_race ? Screen::Race : Screen::Title;
    Screen settings_from = Screen::Title, steering_from = Screen::Title;
    if (o.screen == "pause") screen = Screen::Paused, hud.phase = Phase::Race, player.speed = 12;
    if (o.screen == "settings") screen = Screen::Settings;
    if (o.screen == "journey") screen = Screen::Journey;
    if (o.screen == "wild") screen = Screen::Wild;
    if (kTestTools && o.screen == "endings") screen = Screen::Endings;
    if (kTestTools && o.end_r > 0) {
        test_end = true, end_r = o.end_r - 1, end_k = o.end_k - 1;
        load_level(end_r, end_k);
        restart();
        screen = Screen::Race;
    }
    Menu menu;
    int sel_title = 0, sel_settings = 0, sel_pause = 0, sel_journey = 1 + browse_k, sel_wild = 2, sel_end = 1, sel_steering = 0;

    // The menus are rebuilt every frame from the settings and progress, so they always show
    // current values.
    auto build_menu = [&]() {
        Menu m;
        if (screen == Screen::Title) {
            m.title = "RAPTOR SPRINT";
            m.subtitle = TextFormat("Claws  %d of %d", prog.total_claws(), kRegions * kLevelsPerRegion * 3);
            m.hint = "A  choose      < >  change";
            int r, k;
            prog.next_up(&r, &k);
            m.items = {{JOURNEY, "Journey", "", TextFormat("Next: %s, trail %d", region(r).name, k + 1)},
                       {WILD, "Wild trail", "", "A new trail every run"},
                       {DIFFICULTY, "Difficulty", difficulty_name(set.difficulty),
                        set.difficulty == 2 ? "20% faster, 50% more obstacles" : set.difficulty == 1 ? "10% faster, 20% more obstacles" : ""},
                       {SETTINGS, "Settings", "", ""},
                       {EXIT, "Exit", "", ""}};
#if defined(__EMSCRIPTEN__)
            m.items.pop_back();  // (a web page isn't left this way)
#endif
            if (kTestTools) m.items.insert(m.items.begin() + 2, {ENDINGS, "Test endings", "", "Temporary"});
            m.sel = sel_title;
        } else if (screen == Screen::Endings) {
            static const char* shots[] = {"Chase", "Orbit", "Up the trail", "Crane"};
            m.title = "Test endings";
            m.subtitle = "Nothing here is saved";
            m.hint = "A  run      < >  region      B  back";
            m.items.push_back({E_REGION, "Region", region(end_r).name, ""});
            for (int k = 0; k < kLevelsPerRegion; k++) {
                MenuItem it = {E_LEVEL + k, TextFormat("Trail %d", k + 1), shots[k % 4], ""};
                it.arrows = false;
                m.items.push_back(it);
            }
            m.items.push_back({E_BACK, "Back", "", ""});
            m.sel = sel_end;
        } else if (screen == Screen::Journey) {
            const Region& rg = region(browse_r);
            bool open = prog.region_open(browse_r);
            m.title = "Journey";
            m.subtitle = TextFormat("Region %d of %d      %s claws %d / %d", browse_r + 1, kRegions, difficulty_name(set.difficulty), prog.region_claws(browse_r), kLevelsPerRegion * 3);
            m.hint = "A  run      < >  region      B  back";
            MenuItem reg = {J_REGION, "Region", rg.name, open ? rg.blurb : TextFormat("Finish all of %s to open", region(browse_r - 1).name)};
            reg.locked = !open;
            m.items.push_back(reg);
            for (int k = 0; k < kLevelsPerRegion; k++) {
                bool lv = prog.level_open(browse_r, k);
                float b = prog.best(browse_r, k);
                MenuItem it = {J_LEVEL + k, TextFormat("Trail %d    %.1f km", k + 1, level_length(browse_r, k) / 1000.0f),
                               !lv ? "Locked" : b > 0 ? format_time(b) : "New", ""};
                it.locked = !lv;
                it.arrows = false;
                if (b > 0) it.claws = prog.claws(browse_r, k);
                m.items.push_back(it);
            }
            m.items.push_back({J_BACK, "Back", "", ""});
            m.sel = sel_journey;
        } else if (screen == Screen::Wild) {
            m.title = "Wild trail";
            m.subtitle = "A new trail every run, in any region you have reached";
            m.hint = "< >  change      A  choose      B  back";
            m.items = {{W_REGION, "Region", region(set.wild_region).name, region(set.wild_region).blurb},
                       {W_LENGTH, "Length", length_name(set.length), ""},
                       {W_RUN, "Run", "", ""},
                       {W_BACK, "Back", "", ""}};
            m.sel = sel_wild;
        } else if (screen == Screen::Settings) {
            static const char* bob[] = {"Off", "Reduced", "Full"};
            m.title = "Settings";
            m.subtitle = "Saved as you change them";
            m.hint = "< >  change      B  back";
            m.items = {{FX, "Effects volume", std::to_string(set.fx), ""},
                       {FOREST, "Forest volume", std::to_string(set.forest), ""},
                       {SPRINT, "Sprint (R)", set.sprint_toggle ? "Toggle" : "Hold", ""},
                       {BOB, "Head bob", bob[set.bob], ""},
                       {FPS, "Frame rate counter", set.show_fps ? "On" : "Off", ""},
                       {DETAIL, "Detail", set.detail == 0 ? "High" : set.detail == 1 ? "Medium" : "Low", ""},
                       {BACK, "Back", "", ""}};
            if (o.full) m.items.insert(m.items.end() - 1, {DISPLAY, "Display", set.fullscreen ? "Full screen" : "Window", ""});
#if defined(__EMSCRIPTEN__)
            m.items.insert(m.items.end() - 1, {STEERING, "Steering", "", ""});
#endif
            m.sel = sel_settings;
#if defined(__EMSCRIPTEN__)
        } else if (screen == Screen::Steering) {
            m.title = "Steering";
            m.subtitle = "Pause during a race to change these as you go";
            m.items = {{ST_SPEED, "Turn speed", TextFormat("%d%%", set.steer_speed), ""},
                       {ST_TRAVEL, "Drag for a full turn", TextFormat("%d pt", set.steer_travel), ""},
                       {ST_CURVE, "Soft centre", set.steer_curve ? std::to_string(set.steer_curve) : "Off", ""},
                       {ST_DEAD, "Dead zone", set.steer_dead ? TextFormat("%d%%", set.steer_dead) : "Off", ""},
                       {ST_SMOOTH, "Smoothing", set.steer_smooth ? std::to_string(set.steer_smooth) : "Off", ""},
                       {ST_RESET, "Reset to defaults", "", ""},
                       {ST_BACK, "Back", "", ""}};
            m.sel = sel_steering;
#endif
        } else {
            m.title = "Paused";
            m.subtitle = course_name + "   " + format_time(player.time);
            m.hint = "A  choose      B / Start  resume";
            m.items = {{RESUME, "Resume", "", ""},
                       {RESTART, "Restart", "", ""},
                       {PAUSE_SETTINGS, "Settings", "", ""},
                       {TO_TITLE, "Quit to title", "", ""},
                       {PAUSE_EXIT, "Exit game", "", ""}};
#if defined(__EMSCRIPTEN__)
            m.items.pop_back();
            m.items.insert(m.items.begin() + 1, {STEERING, "Steering", "", ""});
#endif
            m.sel = sel_pause;
        }
        return m;
    };
    auto keep_sel = [&](int s) {
        if (screen == Screen::Title) sel_title = s;
        else if (screen == Screen::Journey) sel_journey = s;
        else if (screen == Screen::Wild) sel_wild = s;
        else if (screen == Screen::Endings) sel_end = s;
        else if (screen == Screen::Settings) sel_settings = s;
        else if (screen == Screen::Steering) sel_steering = s;
        else sel_pause = s;
    };
    // Left and right move a setting along and stop at its ends, and A moves it right. (Wild trails
    // only go to regions already reached; Detail is stored high first, but runs low to high.)
    // The whole screen or a window, on the Mac.
    auto set_display = [&](bool whole) {
        set.fullscreen = whole;
        if (whole != is_full_screen()) toggle_full_screen();
    };
    auto step_value = [&](int id, int dir) {
        auto along = [&](int v, int last) { return std::clamp(v + dir, 0, last); };
        if (id == DIFFICULTY) set.difficulty = prog.difficulty = along(set.difficulty, kDifficulties - 1);
        else if (id == W_REGION) {
            if (prog.region_open(along(set.wild_region, kRegions - 1))) set.wild_region = along(set.wild_region, kRegions - 1);
        }
        else if (id == W_LENGTH) set.length = along(set.length, 2);
        else if (id == FX) set.fx = along(set.fx, 10);
        else if (id == FOREST) set.forest = along(set.forest, 10);
        else if (id == SPRINT) set.sprint_toggle = dir > 0;
        else if (id == BOB) set.bob = along(set.bob, 2);
        else if (id == FPS) set.show_fps = dir > 0;
        else if (id == DETAIL) set.detail = std::clamp(set.detail - dir, 0, 2), make_view();
        else if (id == DISPLAY) set_display(dir < 0);
#if defined(__EMSCRIPTEN__)
        else if (id == ST_SPEED) set.steer_speed = std::clamp(set.steer_speed + dir * 10, 60, 200);
        else if (id == ST_TRAVEL) set.steer_travel = std::clamp(set.steer_travel + dir * 5, 20, 100);
        else if (id == ST_CURVE) set.steer_curve = along(set.steer_curve, 10);
        else if (id == ST_DEAD) set.steer_dead = std::clamp(set.steer_dead + dir * 2, 0, 20);
        else if (id == ST_SMOOTH) set.steer_smooth = along(set.steer_smooth, 10);
#endif
        else return;
#if defined(__EMSCRIPTEN__)
        touch_steering(set.steer_speed, set.steer_travel, set.steer_curve, set.steer_dead, set.steer_smooth);
#endif
        apply_audio();
        set.save(set_path);
        audio.play(Sfx::MenuMove);
    };

    float time = 0;
    int frame = 0;
    bool quit = false;
    Vector3 fog = gfx.fog_col;
    auto grade = [](float c) { return 1.0f - std::exp(-c * 1.25f); };
    Color clear = rgb(grade(fog.x), grade(fog.y), grade(fog.z));
    double perf_t = GetTime();
    int perf_frames = 0;
    double t_logic = 0, t_draw = 0, t_gpu = 0, t_end = 0;  // time spent per part, summed between reports
    int gpu_samples = 0;
    int tri_sum = 0;

    double perf_mark = GetTime();
    if (o.full && !o.sized) set_display(set.fullscreen);
    int settle_frames = -120;  // (the first two seconds don't count: the window is still arriving)
    double settle_from = 0;
#if defined(__EMSCRIPTEN__)
    while (!quit) {  // (the browser's WindowShouldClose sleeps for a whole frame)
        if (web_view_width() != o.width || web_view_height() != o.height) {
            o.width = web_view_width(), o.height = web_view_height();
            SetWindowSize(o.width, o.height);
            o.upright = o.height > o.width;
            ui_design_width = o.upright ? 560.0f : 640.0f;
            top = {0, 0, (float)o.width, (float)o.height};
            make_view();
        }
        TouchMode touch_mode = screen != Screen::Race ? TouchMode::Menu : hud.hold ? TouchMode::Tap : hud.phase == Phase::Finished ? TouchMode::Finish : TouchMode::Race;
        touch_update(touch_mode, web_safe_area(top));
#else
    while (!WindowShouldClose() && !quit) {
#endif
        // First run on this machine: time the title screen at each Detail, from High down, and keep
        // the first that holds 60 a second. It's only a starting point: Settings can change it.
        if (settle_detail && screen == Screen::Title) {
            if (settle_frames == 0) settle_from = GetTime();
            if (++settle_frames == 90) {
                double ms = (GetTime() - settle_from) * 1000.0 / settle_frames;
                if (o.diag) printf("[detail] %d: %.1f ms a frame\n", set.detail, ms);
                if (ms > 18.0 && set.detail < 2) set.detail++, make_view(), settle_frames = -30;  // (60 a second, whatever the screen can do)
                else settle_detail = false, set.save(set_path);
            }
        }
        if (o.full) {
            bool toggle_keys = ((IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) && IsKeyPressed(KEY_F)) || IsKeyPressed(KEY_F11) ||
                               ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_ENTER));
            if (toggle_keys) set_display(!set.fullscreen), set.save(set_path);
            // However the window changed (dragged, zoomed, the system's full screen), follow it.
            static int drawn_w = 0;
            if (GetScreenWidth() != o.width || GetScreenHeight() != o.height || GetRenderWidth() != drawn_w) {
                o.width = GetScreenWidth(), o.height = GetScreenHeight(), drawn_w = GetRenderWidth();
                top = {0, 0, (float)o.width, (float)o.height};
                make_view();
                // (It can also change from outside: the green button, the View menu, or the system
                // itself. The menu follows it, but only Settings and Command-F change what's saved.)
                bool whole = is_full_screen();
                if (!o.sized) set.fullscreen = whole;
                if (whole) HideCursor();  // (nothing here uses the mouse)
                else ShowCursor();
                if (o.diag) printf("[window] %dx%d, drawn at %dx%d, %s\n", o.width, o.height, rw, rh, whole ? "full screen" : "a window");
            }
        }
        float dt = capture ? 1.0f / 60.0f : std::min(GetFrameTime(), 1.0f / 20.0f);
        time += dt;
        int pad = active_pad();
#if !defined(__EMSCRIPTEN__)
        if (pad >= 0 && IsGamepadButtonDown(pad, GAMEPAD_BUTTON_MIDDLE_LEFT) && IsGamepadButtonDown(pad, GAMEPAD_BUTTON_MIDDLE_RIGHT)) break;
#else
        (void)pad;  // (there's no leaving a web page from a controller)
#endif

        // ------------------------------------------------------------ menus
        if (screen != Screen::Race) {
            menu = build_menu();
#if defined(__EMSCRIPTEN__)
            if (touch_row() >= 0 && touch_row() < (int)menu.items.size()) {
                if (touch_row() != menu.sel) audio.play(Sfx::MenuMove);
                menu.sel = touch_row();
                keep_sel(menu.sel);
            }
#endif
            MenuInput mi = capture ? MenuInput::None : read_menu_input(dt);
            int id = menu.current().id;
            int dir = mi == MenuInput::Left ? -1 : mi == MenuInput::Right ? 1 : 0;
            if (mi == MenuInput::Up || mi == MenuInput::Down) {
                if (menu.move(mi == MenuInput::Up ? -1 : 1)) audio.play(Sfx::MenuMove);
                keep_sel(menu.sel);
            } else if (dir != 0) {
                if (screen == Screen::Journey) browse_r = std::clamp(browse_r + dir, 0, kRegions - 1), audio.play(Sfx::MenuMove);
                else if (screen == Screen::Endings) end_r = std::clamp(end_r + dir, 0, kRegions - 1), audio.play(Sfx::MenuMove);
                else step_value(id, dir);
            } else if (mi == MenuInput::Confirm) {
                switch (id) {
                    case JOURNEY:
                        audio.play(Sfx::MenuOk);
                        prog.next_up(&browse_r, &browse_k);
                        sel_journey = 1 + browse_k;
                        screen = Screen::Journey;
                        break;
                    case WILD:
                        audio.play(Sfx::MenuOk);
                        screen = Screen::Wild;
                        break;
                    case ENDINGS:
                        audio.play(Sfx::MenuOk);
                        screen = Screen::Endings;
                        break;
                    case W_RUN:
                        audio.play(Sfx::MenuOk);
                        test_end = false;
                        load_wild();
                        restart();
                        screen = Screen::Race;
                        break;
                    case J_BACK:
                    case W_BACK:
                    case E_BACK:
                        audio.play(Sfx::MenuBack);
                        screen = Screen::Title;
                        break;
                    case SETTINGS:
                    case PAUSE_SETTINGS:
                        audio.play(Sfx::MenuOk);
                        settings_from = screen;
                        screen = Screen::Settings;
                        break;
                    case BACK:
                        audio.play(Sfx::MenuBack);
                        screen = settings_from;
                        break;
#if defined(__EMSCRIPTEN__)
                    case STEERING:
                        audio.play(Sfx::MenuOk);
                        steering_from = screen;
                        sel_steering = 0;
                        screen = Screen::Steering;
                        break;
                    case ST_RESET:
                        audio.play(Sfx::MenuOk);
                        set.reset_steering();
                        touch_steering(set.steer_speed, set.steer_travel, set.steer_curve, set.steer_dead, set.steer_smooth);
                        set.save(set_path);
                        break;
                    case ST_BACK:
                        audio.play(Sfx::MenuBack);
                        screen = steering_from;
                        break;
#endif
                    case RESUME:
                        audio.play(Sfx::MenuOk);
                        screen = Screen::Race;
                        break;
                    case RESTART:
                        audio.play(Sfx::MenuOk);
                        restart();
                        screen = Screen::Race;
                        break;
                    case TO_TITLE:
                        audio.play(Sfx::MenuBack);
                        test_end = false;
                        screen = Screen::Title;
                        break;
                    case EXIT:
                    case PAUSE_EXIT: quit = true; break;
                    default:
                        if (id >= J_LEVEL && id < J_LEVEL + kLevelsPerRegion) {
                            int k = id - J_LEVEL;
                            if (prog.level_open(browse_r, k)) {
                                audio.play(Sfx::MenuOk);
                                test_end = false;
                                load_level(browse_r, k);
                                restart();
                                screen = Screen::Race;
                            } else {
                                audio.play(Sfx::MenuBack);
                            }
                        } else if (id >= E_LEVEL && id < E_LEVEL + kLevelsPerRegion) {
                            audio.play(Sfx::MenuOk);
                            test_end = true;
                            end_k = id - E_LEVEL;
                            load_level(end_r, end_k);
                            restart();
                            screen = Screen::Race;
                        } else if (id == E_REGION) {
                            end_r = std::min(end_r + 1, kRegions - 1);
                            audio.play(Sfx::MenuMove);
                        } else if (id == J_REGION) {
                            browse_r = std::min(browse_r + 1, kRegions - 1);
                            audio.play(Sfx::MenuMove);
                        } else {
                            step_value(id, 1);
                        }
                }
            } else if (mi == MenuInput::Back || mi == MenuInput::Pause) {
                if (screen == Screen::Steering) audio.play(Sfx::MenuBack), screen = steering_from;
                else if (screen == Screen::Settings) audio.play(Sfx::MenuBack), screen = settings_from;
                else if (screen == Screen::Journey || screen == Screen::Wild || screen == Screen::Endings) audio.play(Sfx::MenuBack), screen = Screen::Title;
                else if (screen == Screen::Paused) audio.play(Sfx::MenuOk), screen = Screen::Race;
            }
            if (screen != Screen::Race) menu = build_menu();
            // Browsing regions shows each one's light on the forest behind the menu.
            if (screen == Screen::Journey) environment(browse_r);
            else if (screen == Screen::Wild) environment(set.wild_region);
            else if (screen == Screen::Endings) environment(end_r);
            else if (screen == Screen::Title) environment(course_region);
        }

        // ------------------------------------------------------------ the race
        if (screen == Screen::Race) {
            if (!capture && read_menu_input(dt) == MenuInput::Pause && hud.phase != Phase::Finished) {
                audio.play(Sfx::MenuBack);
                screen = Screen::Paused;
                sel_pause = 0;
            }
        }
        if (screen == Screen::Race) {
            Controls in = o.autoplay || test_end ? autopilot(player, course) : read_controls();
            if (set.sprint_toggle && !o.autoplay) {
                if (in.sprint && !sprint_was) sprint_on = !sprint_on;
                sprint_was = in.sprint;
                if (player.winded) sprint_on = false;
                in.sprint = sprint_on;
            }
            int hits_before = player.hits;
            switch (hud.phase) {
                case Phase::Countdown: {
                    player.step(dt, Controls{}, course, false);
                    if (hud.hold) {
                        hud.held += dt;
                        // (Not straight away: the A that chose the trail mustn't dismiss it.)
                        if (capture || hud.held < 0.3f || read_menu_input(dt) != MenuInput::Confirm) break;
                        audio.play(Sfx::MenuOk);
                        hud.hold = false;
                        hud.intro_lines.clear();
                    }
                    hud.countdown -= dt;
                    int n = (int)std::ceil(hud.countdown);
                    if (n != count_shown && n >= 1 && n <= 3) audio.play(Sfx::Count);
                    count_shown = n;
                    if (hud.countdown <= 0) hud.phase = Phase::Race, hud.go = true, audio.play(Sfx::Go);
                    break;
                }
                case Phase::Race:
                    player.step(dt, in, course, true);
                    // Obstacles passed: clean ones (no stumble, not in the water or mud) are rewarded.
                    for (size_t i = 0; i < course.obstacles.size(); i++) {
                        if (ob_state[i] == 2) continue;
                        const Obstacle& ob = course.obstacles[i];
                        float reach = ob.kind == ObKind::Rock ? ob.r + 0.3f : ob.kind == ObKind::Creek || ob.kind == ObKind::Mud || ob.kind == ObKind::Bramble || ob.kind == ObKind::Sinkhole ? ob.r
                                    : ob.kind == ObKind::Faller || ob.kind == ObKind::Roller ? ob.r + 0.6f : ob.kind == ObKind::Jam ? ob.r + 0.3f
                                    : ob.kind == ObKind::Ribs ? ob.len * 0.5f + 0.3f : 0.6f;
                        float s = player.near.s;
                        if (ob_state[i] == 0 && s > ob.idx - reach - 2.0f) ob_state[i] = 1, ob_hits[i] = player.hits;
                        if (ob_state[i] != 1) continue;
                        if (player.hits > ob_hits[i] || (ob.kind == ObKind::Creek && player.in_water) || (ob.kind == ObKind::Mud && player.in_mud))
                            ob_dirty[i] = 1;
                        if (s > ob.idx + reach + 0.5f) {
                            ob_state[i] = 2;
                            hud.passed++;
                            if (ob_dirty[i]) {
                                hud.streak = 0;
                                continue;
                            }
                            hud.clean++;
                            hud.streak++;
                            hud.best_streak = std::max(hud.best_streak, hud.streak);
                            hud.reward = 1.0f;
                            static const char* words[] = {"CLEARED", "DUCKED", "DODGED", "LEAPT", "CLEAN", "CLOSE CALL",
                                                          "THROUGH", "THREADED", "SLIPPED BY", "GOOD TIMING", "PULLED FREE", "BONE CLEAN"};
                            hud.reward_word = words[(int)ob.kind];
                            static const int notes[] = {0, 2, 4, 7, 9, 12};  // up the scale as the streak grows
                            audio.play(Sfx::Clear, 0.55f, std::pow(2.0f, notes[std::min(hud.streak - 1, 5)] / 12.0f));
                            // Every five clean moves in a row win back some breath.
                            if (hud.streak % 5 == 0) {
                                player.breath = std::min(1.0f, player.breath + 0.2f);
                                hud.boost = 1.0f;
                                audio.play(Sfx::Recover, 0.95f);
                                if (o.diag) printf("[boost] streak %d at %.0f m, frame %d\n", hud.streak, player.near.s, frame);
                            }
                        }
                    }
                    if (player.finished) {
                        hud.phase = Phase::Finished;
                        outro.start(course, player, journey ? race_r : course_region, journey ? race_k : course_seed & 3);
                        if (!test_end && (best.total <= 0 || player.time < best.total)) {
                            hud.new_best = true;
                            best.total = player.time;
                            best.splits = player.splits;
                            best.save(best_path);
                        }
                        hud.claws = claws_for(player.time, course_par);
                        hud.hint = test_end ? "A  next ending      B  list" : "A  run again      B  menu";
                        if (journey && !o.autoplay && !test_end) {
                            bool opened = prog.done(race_r, race_k), first = prog.best(race_r, race_k) <= 0;
                            if (first || player.time < prog.best(race_r, race_k)) prog.best(race_r, race_k) = player.time;
                            prog.claws(race_r, race_k) = std::max(prog.claws(race_r, race_k), hud.claws);
                            prog.save(prog_path);
                            if (!opened) {
                                if (race_k + 1 < kLevelsPerRegion) hud.news = TextFormat("Trail %d is open", race_k + 2);
                                else if (race_r + 1 < kRegions) hud.news = TextFormat("%s is open", region(race_r + 1).name);
                                else hud.news = "The whole journey is done";
                            }
                            bool next = race_k + 1 < kLevelsPerRegion ? prog.level_open(race_r, race_k + 1) : prog.region_open(race_r + 1);
                            if (next) hud.hint = "A  next trail      B  menu";
                        }
                    }
                    break;
                case Phase::Finished: {
                    player.step(dt, Controls{}, course, false);
                    outro.update(dt);
                    MenuInput mi = capture || outro.t < 0.8f ? MenuInput::None : read_menu_input(dt);
                    if (mi == MenuInput::Confirm || (o.autoplay && !capture)) {
                        audio.play(Sfx::MenuOk);
                        if (test_end) {  // every ending in turn
                            end_k = (end_k + 1) % kLevelsPerRegion;
                            if (end_k == 0) end_r = (end_r + 1) % kRegions;
                            load_level(end_r, end_k);
                        } else if (!o.autoplay) {
                            if (!journey) load_wild();  // a wild trail: a new one
                            else if (race_k + 1 < kLevelsPerRegion && prog.level_open(race_r, race_k + 1)) load_level(race_r, race_k + 1);
                            else if (race_k + 1 >= kLevelsPerRegion && prog.region_open(race_r + 1)) load_level(race_r + 1, 0);
                        }
                        restart();
                    } else if (mi == MenuInput::Back || mi == MenuInput::Pause) {
                        audio.play(Sfx::MenuBack);
                        if (journey) browse_r = race_r, sel_journey = 1 + race_k;
                        screen = test_end ? Screen::Endings : journey ? Screen::Journey : Screen::Wild;
                        if (test_end) sel_end = 1 + end_k, test_end = false;
                    }
                    break;
                }
            }
            // Sound for what just happened.
            unsigned ev = player.events;
            float pace = clampf(player.speed / 20.0f, 0, 1);
            if (ev & EV_STEP) {
                steps++;
                Sfx step = player.in_mud ? Sfx::Squelch : player.in_water ? Sfx::Splash : player.rough ? Sfx::StepLeaf : Sfx::StepDirt;
                audio.play(step, (step == Sfx::Splash ? 0.25f : 0.3f) + 0.55f * pace, GetRandomValue(92, 108) / 100.0f);
                bool sprinting = in.sprint && !player.winded && player.speed > player.tune.run_speed + 1;
                if ((player.winded && steps % 2 == 0) || (sprinting && steps % 3 == 0))
                    audio.play(Sfx::Breath, player.winded ? 0.75f : 0.45f, player.winded ? 1.1f : 1.0f);
            }
            if (ev & EV_JUMP) audio.play(Sfx::Jump, 0.8f);
            if (ev & EV_LAND) audio.play(Sfx::Land, 0.5f + 0.5f * player.land);
            if (ev & EV_DUCK) audio.play(Sfx::Duck, 0.6f);
            if (ev & EV_HIT) {
                std::string w = player.last_hit_what;
                audio.play(w == "log" ? Sfx::HitLog : w == "rock" ? Sfx::HitRock : w == "branch" ? Sfx::HitBranch : w == "water" ? Sfx::Splash : w == "mud" ? Sfx::Squelch : Sfx::HitTree);
                audio.play(Sfx::Yelp, 0.8f, GetRandomValue(95, 110) / 100.0f);
            }
            if ((ev & EV_CHECKPOINT) && !(ev & EV_FINISH)) audio.play(Sfx::Checkpoint, 0.8f);
            if (ev & EV_FINISH) audio.play(Sfx::Finish);
            if (ev & EV_CREAK) audio.play(Sfx::Creak, 0.9f);
            if (ev & EV_CRASH) audio.play(Sfx::Crash);
            if (ev & EV_RUMBLE) audio.play(Sfx::Rumble);
            hud.reward = std::max(0.0f, hud.reward - dt / 1.1f);
            if (player.hits > hits_before) {
                hud.flash = 1.0f;
                hud.streak = 0;
                if (o.diag) printf("[hit] %s at %.0f m, side %.1f\n", player.last_hit_what, player.near.s, player.near.side);
            }
            hud.flash = std::max(0.0f, hud.flash - dt * 2.0f);
            hud.boost = std::max(0.0f, hud.boost - dt * 2.0f);
        }
        audio.set_forest(screen == Screen::Race ? player.speed : 0.0f, true);

        // ------------------------------------------------------------ drawing
        double tp_draw = GetTime();
        t_logic += tp_draw - perf_mark;
        Matrix head = MatrixIdentity();
        Screen behind = screen == Screen::Steering ? steering_from : screen;  // (what the menu is over)
        if (behind == Screen::Settings) behind = settings_from;
        bool in_race = behind == Screen::Race || behind == Screen::Paused;
        bool on_title = behind == Screen::Title;
        Stage stage = on_title ? title_stage(course, time, o.dual || o.upright ? 0.0f : (float)o.width / o.height) : Stage{};
        float dark = 0;
        Camera3D cam = in_race ? head_camera(player, time, set.bob * 0.5f, &head) : on_title ? stage.cam : title_camera(course, time, &dark);
        if (!in_race && outro.active) outro.stop();
        bool ending = in_race && hud.phase == Phase::Finished && outro.active;
        if (ending) cam = outro.camera(cam, o.dual || o.upright ? 0.0f : (float)o.width / o.height);
#if defined(__EMSCRIPTEN__)
        if (o.upright) cam.fovy = web_upright_fovy(cam.fovy, (float)o.width / o.height);
#endif
        if (on_title) {
            float k = RaptorShow::kScale;
            showcase.update(dt, Vector3Transform(cam.position, MatrixInvert(compose(MatrixScale(k, k, k), stage.frame))));
        }

        std::vector<float> fall(course.obstacles.size(), 0.0f);
        if (in_race)
            for (size_t i = 0; i < fall.size(); i++) fall[i] = course.obstacles[i].kind == ObKind::Roller ? player.roll(course.obstacles[i], (int)i) : player.fall_angle((int)i);
        if (o.full) gfx.cast_shadows(cam, [&](Vector3 at, float radius, int cascade) {
            world.draw_casters(at, radius, cascade, fall);
            if (ending) outro.draw_casters();
            if (on_title) showcase.draw(stage.frame);
        });
        BeginTextureMode(view);
        ClearBackground(clear);
        BeginMode3D(cam);
        gfx.begin_frame(cam, time, draw_range().far);
        world.draw(cam, draw_range(), fall, region(shown_env < 0 ? 0 : shown_env).biome);
        gfx.draw_sky(cam);
#if defined(__EMSCRIPTEN__)
        // (A tall view sees much more of the head. A little lower, so the trail ahead stays clear.)
        if (o.upright) head = compose(MatrixTranslate(0, -0.1f, 0), head);
#endif
        if (in_race && !ending) world.draw_snout(head);
        if (ending) outro.draw(cam);
        if (on_title) showcase.draw(stage.frame);
        EndMode3D();
#if !defined(__EMSCRIPTEN__)  // (a multisampled target can't be read there)
        if (o.diag && frame % 30 == 0) {  // now and then, wait for the GPU to finish the view, to log how long it really takes
            double t0 = GetTime();
            rlDrawRenderBatchActive();
            std::free(rlReadScreenPixels(1, 1));
            t_gpu += GetTime() - t0, gpu_samples++;
        }
#endif
        EndTextureMode();
#if defined(__EMSCRIPTEN__)
        resolve_view_target(view);
#endif

        if (o.full) {
            gfx.full_post();
        } else if (o.rich) {
            // Where the sun falls on the view, and how much of it is ahead.
            Vector3 look = Vector3Normalize(Vector3Subtract(cam.target, cam.position)), to_sun = Vector3Negate(gfx.sun_dir);
            Vector2 at = GetWorldToScreenEx(Vector3Add(cam.position, Vector3Scale(to_sun, 200)), cam, rw, rh);
            float facing = clampf((Vector3DotProduct(look, to_sun) - 0.3f) / 0.45f, 0, 1);
            gfx.post_prepare(view.texture, {at.x / rw, 1.0f - at.y / rh}, facing);
        }

        // The lower screen: the map while racing, otherwise the menu.
        auto draw_lower_screen = [&](Rectangle r) {
            if (screen == Screen::Race) {
                draw_lower(r, course, player, best, hud);
                if (ending) DrawRectangleRec(r, Color{6, 9, 7, 150}), draw_finish_card(r, player, best, hud, clampf(outro.t * 2.0f, 0, 1), false);
            } else {
                draw_menu_panel(r, menu, time);
            }
        };
        if (panels) {
            BeginTextureMode(lower_rt);
            draw_lower_screen({0, 0, (float)o.width, (float)o.height});
            EndTextureMode();
        }

        // The menus and the race's text stay clear of a phone's notch; the view itself goes edge to edge.
        Rectangle ui = top;
#if defined(__EMSCRIPTEN__)
        ui = web_safe_area(top);
#endif
        BeginDrawing();
        ClearBackground(BLACK);
        if (o.full) gfx.full_draw(top);
        else if (o.rich) gfx.post_draw(view.texture, top);
        else DrawTexturePro(view.texture, {0, 0, (float)rw, -(float)rh}, top, {0, 0}, 0, WHITE);
        if (dark > 0) DrawRectangleRec(top, Fade(BLACK, dark));
        if (ending) outro.draw_dust(cam, top);
        if (screen == Screen::Race) {
            draw_upper_overlay(ui, course, player, best, hud, !o.dual);
            // One screen: the card comes in on the left once the raptor has been found.
            if (ending && !o.dual) draw_finish_card(ui, player, best, hud, clampf((outro.t - 2.2f) * 2.0f, 0, 1), !o.upright);
        } else if (screen == Screen::Journey || screen == Screen::Wild || screen == Screen::Endings) {
            int r = screen == Screen::Journey ? browse_r : screen == Screen::Endings ? end_r : set.wild_region;
            if (o.dual) draw_region_banner(top, region(r), r, screen == Screen::Endings || prog.region_open(r));
            else draw_menu_overlay(ui, menu, time);
        } else if (screen == Screen::Title || (screen == Screen::Settings && settings_from == Screen::Title)) {
            if (o.dual) draw_title(top, time, screen == Screen::Title ? "A  to choose on the lower screen" : "");
            else if (o.upright && screen == Screen::Title) {  // (the name large above the raptor, the menu below it)
                draw_title(ui, time, "");
                Menu below = menu;
                below.title = "";
                draw_menu_overlay(ui, below, time, true);
            }
            else draw_menu_overlay(ui, menu, time, screen == Screen::Title);
        } else {
            if (o.dual) {
                DrawRectangleRec(top, Color{6, 9, 7, 120});
                float k = ui_scale(top);
                const char* p = "PAUSED";
                ui_draw(p, top.width / 2 - ui_measure(p, 48 * k) / 2, top.height * 0.4f, 48 * k, Color{228, 234, 220, 255});
            } else {
                draw_menu_overlay(ui, menu, time);
            }
        }
#if defined(__EMSCRIPTEN__)
        if (screen == Screen::Race) touch_draw(touch_mode, hud.hint, ending ? clampf((outro.t - 2.2f) * 2.0f, 0, 1) : 0);
#endif
        if (o.dual && !panels) draw_lower_screen(lower_rect);
        // The counter follows the setting only; --diag writes to the log, not the screen.
        if (set.show_fps) {
            const char* s = o.diag ? TextFormat("%d fps  %d chunks  %dk tris", GetFPS(), world.drawn, world.triangles / 1000) : TextFormat("%d fps", GetFPS());
#if defined(__EMSCRIPTEN__)
            ui_draw(s, ui.x + ui.width / 2 - ui_measure(s, 18 * ui_scale(ui)) / 2, ui.y + 4, 18 * ui_scale(ui), WHITE);  // (the thumbs are at the bottom)
#else
            ui_draw(s, 10, o.height - 28, 18, WHITE);
#endif
        }

        frame++;
        bool save_seq = !o.seq.empty() && frame % o.every == 0;
        bool save_shot = !o.shot.empty() && frame == o.frames;
        if (save_seq || save_shot) {
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            if (panels) {  // both panels, stacked as on the desktop
                Image low = LoadImageFromTexture(lower_rt.texture);
                ImageFlipVertical(&low);
                Image both = GenImageColor(img.width, img.height * 2, BLACK);
                ImageDraw(&both, img, {0, 0, (float)img.width, (float)img.height}, {0, 0, (float)img.width, (float)img.height}, WHITE);
                ImageDraw(&both, low, {0, 0, (float)low.width, (float)low.height}, {0, (float)img.height, (float)img.width, (float)img.height}, WHITE);
                UnloadImage(low);
                UnloadImage(img);
                img = both;
            }
            if (save_seq) ExportImage(img, TextFormat("%s/f%04d.png", o.seq.c_str(), frame / o.every));
            if (save_shot) ExportImage(img, o.shot.c_str());
            UnloadImage(img);
        }
        // With kTestTools and grab.on next to the game, the endings test records what really reaches the
        // screen: every 2nd frame at half size, to /tmp/rrgrab, up to 280 frames an ending (raw RGB; read
        // at the end of the frame, after everything has been queued, so the GPU works as it normally would).
        static const bool grab_on = kTestTools && FileExists(save_path("grab.on").c_str());
        static int grabbed = 0, grab_for = -1;
        if (grab_for != end_r * 8 + end_k) grab_for = end_r * 8 + end_k, grabbed = 0;
        if (grab_on && test_end && ending && frame % 2 == 0 && grabbed < 280) {
            rlDrawRenderBatchActive();
            int gw = GetRenderWidth(), gh = GetRenderHeight();
            unsigned char* px = rlReadScreenPixels(gw, gh);
            std::vector<unsigned char> half;
            half.reserve((size_t)(gw / 2) * (gh / 2) * 3);
            for (int y = 0; y + 1 < gh; y += 2)
                for (int x = 0; x + 1 < gw; x += 2) {
                    const unsigned char* q = px + ((size_t)y * gw + x) * 4;
                    half.insert(half.end(), {q[0], q[1], q[2]});
                }
            std::free(px);
#if !defined(_WIN32)
            mkdir("/tmp/rrgrab", 0755);
#endif
            if (FILE* f = fopen(TextFormat("/tmp/rrgrab/e%d-%d_%04d_%dx%d.raw", end_r + 1, end_k + 1, grabbed, gw / 2, gh / 2), "wb")) {
                fwrite(half.data(), 1, half.size(), f);
                fclose(f);
            }
            grabbed++;
        }
        double tp_end = GetTime();
        t_draw += tp_end - tp_draw;
        EndDrawing();
        if (panels) lower.present(lower_rt.texture);
#if defined(__EMSCRIPTEN__)
        web_wait_frame();
#endif
        double tp_done = GetTime();
        t_end += tp_done - tp_end;
        perf_mark = tp_done;
        tri_sum += world.triangles;

        perf_frames++;
        if (o.diag && GetTime() - perf_t > 2.0) {
            double n = perf_frames;
            printf("[perf] %.1f ms/frame (logic %.1f, draw %.1f, gpu %.1f, end %.1f), detail %d, view %dx%d, %d chunks, %dk tris, %s, speed %.1f, s %.0f\n",
                   (GetTime() - perf_t) * 1000.0 / n, t_logic * 1000 / n, t_draw * 1000 / n, t_gpu * 1000 / std::max(1, gpu_samples), t_end * 1000 / n, set.detail, rw, rh, world.drawn,
                   (int)(tri_sum / n / 1000), screen == Screen::Race ? "racing" : "menu", player.speed, player.near.s);
            fflush(stdout);
            perf_t = GetTime();
            perf_frames = 0;
            t_logic = t_draw = t_gpu = t_end = 0, gpu_samples = 0;
            tri_sum = 0;
        }
        if (capture && frame >= o.frames) break;
    }

    set.save(set_path);
    UnloadRenderTexture(view);
    if (panels) UnloadRenderTexture(lower_rt), lower.close();
    world.unload();
    showcase.unload();
    outro.stop();
    gfx.unload();
    audio.unload();
    ui_unload_font();
    CloseWindow();
#if defined(PLATFORM_ANDROID)
    exit(0);  // (the system may keep the process for next time, and nothing here expects a second run)
#endif
    return 0;
}
