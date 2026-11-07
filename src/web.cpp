#if defined(__EMSCRIPTEN__)
#include "web.hpp"

#include <emscripten.h>

#include "ui.hpp"

EM_JS(int, web_js_width, (), { return Module.viewW; });
EM_JS(int, web_js_height, (), { return Module.viewH; });
EM_JS(float, web_js_scale, (), { return Module.cssScale; });
EM_JS(float, web_js_inset, (int side), { return Module.safe[side]; });
// (A screen faster than 60 a second shows some frames twice: the game looks the same, and the phone
// stays cooler.)
EM_ASYNC_JS(void, web_js_frame, (), {
    await new Promise(function(done) {
        function tick(now) {
            if (now - (Module.lastFrame || 0) < 14) return requestAnimationFrame(tick);
            Module.lastFrame = now;
            done();
        }
        requestAnimationFrame(tick);
    });
});

namespace rr {

int web_view_width() { return web_js_width(); }
int web_view_height() { return web_js_height(); }
void web_wait_frame() { web_js_frame(); }

Rectangle web_safe_area(Rectangle r) {
    float x0 = std::max(r.x, web_js_inset(3)), y0 = std::max(r.y, web_js_inset(0));
    float x1 = std::min(r.x + r.width, web_js_width() - web_js_inset(1)), y1 = std::min(r.y + r.height, web_js_height() - web_js_inset(2));
    return {x0, y0, x1 - x0, y1 - y0};
}

float web_upright_fovy(float fovy, float aspect) {
    return 2.0f * std::atan(std::tan(fovy * 0.5f * DEG2RAD) * 1.05f / aspect) * RAD2DEG;
}

namespace {

const Color kValue = {228, 234, 220, 255}, kAmber = {236, 186, 92, 255}, kDark = {10, 14, 11, 255};

enum Button { Jump, Duck, Sprint, Pause, Primary, Secondary, kButtons };
enum class Role { None, Stick, Hold, Tap };

struct Finger {
    int id = -1;
    Role role = Role::None;
    int button = -1;
    Vector2 from = {0, 0}, at = {0, 0};
};
struct TouchEvent {
    int id, phase;  // 0 down, 1 moved, 2 up, 3 taken away by the system
    float x, y;
};
struct Row {
    Rectangle r;
    float less, more;
};

Finger fingers[10];
TouchEvent events[128];
int queued = 0;
bool interrupted = false;

TouchMode mode_now = TouchMode::Menu;
float css = 1;  // pixels to a point on the page
Vector2 centre[kButtons];
float radius[kButtons];
Rectangle pill[2];  // the finish card's two buttons
Vector2 stick_rest = {0, 0}, stick_from = {0, 0};
float stick_reach = 50, steer_span = 60;  // the ring as drawn, and the drag from straight ahead to full lock
float steer_gain = 1, steer_travel = 60, steer_power = 1, steer_dead = 0, steer_rate = 0;  // (touch_steering)
bool stick_on = false;
float steer = 0, steer_aim = 0, stick_x = 0;  // what the raptor gets, what the thumb asks for, and the thumb (-1..1)
bool brake = false, pressed[kButtons], held[kButtons];

std::vector<Row> rows;
int row_now = -1;
MenuInput menu_now = MenuInput::None;

void lay_out(Rectangle a) {
    float m = 14 * css, right = a.x + a.width, bottom = a.y + a.height;
    radius[Jump] = 52 * css, radius[Duck] = 44 * css, radius[Sprint] = 38 * css, radius[Pause] = 19 * css;
    centre[Sprint] = {right - m - radius[Jump], bottom - m - radius[Jump]};  // (in the corner, under where the thumb rests)
    centre[Duck] = {centre[Sprint].x - 112 * css, bottom - m - radius[Duck]};
    centre[Jump] = {centre[Sprint].x - 20 * css, centre[Sprint].y - 100 * css};
    // (Beside the map in the top corner.)
    float k = ui_scale(a);
    centre[Pause] = {right - 134 * k - 30 * css, a.y + 12 * css + radius[Pause]};
    stick_reach = 52 * css, steer_span = steer_travel * css;
    stick_rest = {a.x + m + 78 * css, bottom - m - 66 * css};
    float w = 168 * css, h = 54 * css;
    pill[0] = {right - m - w, bottom - m - h, w, h};
    pill[1] = {pill[0].x - m - w * 0.7f, pill[0].y, w * 0.7f, h};
}

// The button under a finger, allowing for thumbs landing a little wide of it.
int button_at(Vector2 p) {
    int best = -1;
    float nearest = 1e9f;
    for (int b : {Jump, Duck, Sprint}) {
        float d = Vector2Distance(p, centre[b]);
        if (d < radius[b] * 1.3f && d / radius[b] < nearest) best = b, nearest = d / radius[b];
    }
    return best;
}

int row_at(Vector2 p) {
    for (int i = 0; i < (int)rows.size(); i++)
        if (CheckCollisionPointRec(p, rows[i].r)) return i;
    return -1;
}

void stick_move(Vector2 p) {
    float dx = p.x - stick_from.x, dy = p.y - stick_from.y, r = stick_reach, span = steer_span;
    // Past the edge the middle comes along, so turning the other way starts at once.
    if (dx > span) stick_from.x += dx - span, dx = span;
    if (dx < -span) stick_from.x += dx + span, dx = -span;
    if (dy < -0.3f * r) stick_from.y += dy + 0.3f * r, dy = -0.3f * r;
    stick_x = dx / span;
    float m = std::max(0.0f, (std::fabs(stick_x) - steer_dead) / (1.0f - steer_dead));
    steer_aim = std::copysign(std::pow(m, steer_power), stick_x);
    brake = dy > 0.8f * r;
}

void stick_off() { stick_on = false, steer = steer_aim = stick_x = 0, brake = false; }

void finger_down(Finger& f, Rectangle area) {
    Vector2 p = f.at;
    if (mode_now == TouchMode::Menu) {
        f.role = Role::Tap;
        row_now = row_at(p);
    } else if (mode_now == TouchMode::Race) {
        if (Vector2Distance(p, centre[Pause]) < radius[Pause] * 1.6f) {
            menu_now = MenuInput::Pause;
            return;
        }
        int b = button_at(p);
        if (b >= 0) {
            f.role = Role::Hold, f.button = b, pressed[b] = true;
        } else if (p.x < area.x + area.width * 0.5f && !stick_on) {
            stick_off(), f.role = Role::Stick, stick_on = true, stick_from = p;
        }
    } else {
        f.role = Role::Tap;
    }
}

void finger_moved(Finger& f) {
    if (f.role == Role::Stick) stick_move(f.at);
    if (f.role == Role::Hold) {  // (a thumb can roll from one button to the next)
        int b = button_at(f.at);
        if (b >= 0 && b != f.button) f.button = b, pressed[b] = true;
    }
}

void finger_up(Finger& f) {
    if (f.role == Role::Stick) stick_off();
    if (f.role != Role::Tap) return;
    if (mode_now == TouchMode::Tap) menu_now = MenuInput::Confirm;
    if (mode_now == TouchMode::Finish) {
        if (CheckCollisionPointRec(f.at, pill[0])) menu_now = MenuInput::Confirm;
        if (CheckCollisionPointRec(f.at, pill[1])) menu_now = MenuInput::Back;
    }
    if (mode_now == TouchMode::Menu) {
        int i = row_at(f.at);
        if (i < 0 || i != row_at(f.from)) return;
        row_now = i;
        const Row& r = rows[i];
        menu_now = MenuInput::Confirm;
        if (r.more > 0) {
            if (f.at.x > (r.less + r.more) / 2) menu_now = MenuInput::Right;
            else if (f.at.x > r.less - 44 * css) menu_now = MenuInput::Left;
        }
    }
}

void word(const char* s, Vector2 c, float size, Color col) {
    ui_draw(s, c.x - ui_measure(s, size) / 2, c.y - size * 0.62f, size, col);
}

void disc(int b, const char* s) {
    bool on = held[b];
    float r = radius[b];
    DrawCircleV(centre[b], r, on ? Color{kAmber.r, kAmber.g, kAmber.b, 170} : Color{kDark.r, kDark.g, kDark.b, 95});
    DrawRing(centre[b], r - 2 * css, r, 0, 360, 48, Color{kValue.r, kValue.g, kValue.b, (unsigned char)(on ? 235 : 150)});
    word(s, centre[b], 15 * css, on ? kDark : Color{kValue.r, kValue.g, kValue.b, 215});
}

}  // namespace

void touch_update(TouchMode mode, Rectangle area) {
    css = web_js_scale();
    lay_out(area);
    row_now = -1, menu_now = MenuInput::None;
    for (bool& p : pressed) p = false;
    if (mode != mode_now) {
        // (A finger still down from the last screen does nothing on this one.)
        mode_now = mode;
        for (Finger& f : fingers) f.role = Role::None;
        stick_off();
    }
    if (interrupted) {
        interrupted = false;
        for (Finger& f : fingers) f = Finger{};
        stick_off();
        if (mode == TouchMode::Race) menu_now = MenuInput::Pause;
    }
    for (int i = 0; i < queued; i++) {
        const TouchEvent& e = events[i];
        Finger* f = nullptr;
        for (Finger& c : fingers)
            if (c.id == e.id) f = &c;
        if (e.phase == 0) {
            if (!f)
                for (Finger& c : fingers)
                    if (c.id < 0 && !f) f = &c;
            if (!f) continue;
            *f = Finger{};
            f->id = e.id, f->from = f->at = {e.x, e.y};
            finger_down(*f, area);
            continue;
        }
        if (!f) continue;
        f->at = {e.x, e.y};
        if (e.phase == 1) finger_moved(*f);
        else {
            if (e.phase == 2) finger_up(*f);
            else if (f->role == Role::Stick) stick_off();
            *f = Finger{};
        }
    }
    queued = 0;
    steer = steer_rate > 0 ? steer + (steer_aim - steer) * std::min(1.0f, GetFrameTime() * steer_rate) : steer_aim;
    for (bool& h : held) h = false;
    for (const Finger& f : fingers)
        if (f.role == Role::Hold) held[f.button] = true;
}

int touch_row() { return row_now; }
MenuInput touch_menu_input() { return menu_now; }

void touch_controls(Controls& in) {
    if (stick_on) in.steer = steer * steer_gain;
    in.brake = in.brake || brake;
    in.jump = in.jump || pressed[Jump];
    // (A tap too quick to last a frame still counts.)
    in.duck = in.duck || held[Duck] || pressed[Duck];
    in.sprint = in.sprint || held[Sprint] || pressed[Sprint];
}

void touch_steering(int speed, int travel, int curve, int dead, int smooth) {
    steer_gain = speed / 100.0f, steer_travel = (float)travel, steer_power = 1.0f + curve * 0.1f, steer_dead = dead / 100.0f;
    steer_rate = smooth > 0 ? 60.0f / smooth : 0;
}

void touch_menu_clear() { rows.clear(); }
void touch_menu_row(Rectangle row, float less, float more) { rows.push_back({row, less, more}); }

void touch_draw(TouchMode mode, const std::string& hint, float alpha) {
    if (mode == TouchMode::Race) {
        disc(Jump, "JUMP");
        disc(Duck, "DUCK");
        disc(Sprint, "SPRINT");
        // Steering: where the thumb came down, or where it's most likely to.
        Vector2 c = stick_on ? stick_from : stick_rest;
        float r = stick_reach;
        DrawCircleV(c, r, Color{kDark.r, kDark.g, kDark.b, (unsigned char)(stick_on ? 110 : 70)});
        DrawRing(c, r - 2 * css, r, 0, 360, 48, Color{kValue.r, kValue.g, kValue.b, (unsigned char)(stick_on ? 170 : 110)});
        Vector2 knob = {c.x + stick_x * r, c.y + (brake ? r * 0.7f : 0)};
        DrawCircleV(knob, r * 0.44f, stick_on ? Color{kAmber.r, kAmber.g, kAmber.b, 190} : Color{kValue.r, kValue.g, kValue.b, 70});
        if (!stick_on) {
            word("<", {c.x - r * 0.74f, c.y}, 22 * css, Color{kValue.r, kValue.g, kValue.b, 170});
            word(">", {c.x + r * 0.74f, c.y}, 22 * css, Color{kValue.r, kValue.g, kValue.b, 170});
        }
        if (brake) word("BRAKE", {c.x, c.y + r + 12 * css}, 15 * css, kAmber);
        // Pause.
        Vector2 p = centre[Pause];
        float rr = radius[Pause], u = rr * 0.2f;
        DrawCircleV(p, rr, Color{kDark.r, kDark.g, kDark.b, 130});
        DrawRectangleRec({p.x - 2.2f * u, p.y - 2.6f * u, 1.5f * u, 5.2f * u}, kValue);
        DrawRectangleRec({p.x + 0.7f * u, p.y - 2.6f * u, 1.5f * u, 5.2f * u}, kValue);
    } else if (mode == TouchMode::Finish && alpha > 0) {
        // The card's "A  next trail      B  menu", as two buttons.
        size_t a = hint.find("A  "), b = hint.find("B  ");
        if (a == std::string::npos || b == std::string::npos) return;
        std::string first = hint.substr(a + 3, b - a - 3), second = hint.substr(b + 3);
        first.erase(first.find_last_not_of(' ') + 1);
        first[0] = (char)std::toupper(first[0]), second[0] = (char)std::toupper(second[0]);
        unsigned char al = (unsigned char)(255 * clampf(alpha, 0, 1));
        DrawRectangleRounded(pill[0], 0.5f, 12, Color{kAmber.r, kAmber.g, kAmber.b, al});
        DrawRectangleRounded(pill[1], 0.5f, 12, Color{44, 56, 46, (unsigned char)(al * 0.92f)});
        word(first.c_str(), {pill[0].x + pill[0].width / 2, pill[0].y + pill[0].height / 2}, 20 * css, Color{kDark.r, kDark.g, kDark.b, al});
        word(second.c_str(), {pill[1].x + pill[1].width / 2, pill[1].y + pill[1].height / 2}, 20 * css, Color{kValue.r, kValue.g, kValue.b, al});
    }
}

}  // namespace rr

// Called by the page.
extern "C" {
EMSCRIPTEN_KEEPALIVE void touch_event(int id, int phase, float x, float y) {
    using namespace rr;
    if (phase == 1 && queued > 0 && events[queued - 1].id == id && events[queued - 1].phase == 1) {
        events[queued - 1].x = x, events[queued - 1].y = y;  // (only the latest of a run of moves matters)
        return;
    }
    if (queued < 128) events[queued++] = {id, phase, x, y};
}
// The page was left, or the phone turned: fingers are gone, and a race pauses.
EMSCRIPTEN_KEEPALIVE void web_interrupted() { rr::interrupted = true; }
}
#endif
