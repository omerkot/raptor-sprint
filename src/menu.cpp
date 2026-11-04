#include "menu.hpp"

#include <cstdio>

#include "hud.hpp"
#include "ui.hpp"

namespace rr {

namespace {
const Color kBg = {14, 19, 16, 255}, kPanel = {22, 29, 24, 255}, kSel = {34, 44, 36, 255};
const Color kLabel = {128, 150, 124, 255}, kValue = {228, 234, 220, 255}, kAmber = {236, 186, 92, 255};

void text(const std::string& s, float x, float y, float size, Color c, int align = 0) {
    float w = ui_measure(s.c_str(), size);
    ui_draw(s.c_str(), align == 0 ? x : align == 1 ? x - w : x - w / 2, y, size, c);
}

// Held directions repeat after a pause, like a keyboard.
struct Repeat {
    float t = 0;
    bool fire(bool down, float dt) {
        if (!down) return t = 0, false;
        bool first = t == 0;
        t += dt;
        if (first) return true;
        if (t > 0.38f) {
            t -= 0.11f;
            return true;
        }
        return false;
    }
};
Repeat r_up, r_down, r_left, r_right;
}  // namespace

// The pad in use: the first one there. (It isn't always in slot 0: the system numbers its devices
// as it finds them, and on the RG DS the pad is a virtual one that can land in any slot.)
int active_pad() {
    static int shown = -2;
    int pad = -1;
    for (int i = 0; i < 4 && pad < 0; i++)
        if (IsGamepadAvailable(i)) pad = i;
    if (pad != shown) {
        shown = pad;
        printf("[pad] slot %d: %s\n", pad, pad >= 0 ? GetGamepadName(pad) : "none");
    }
    return pad;
}

MenuInput read_menu_input(float dt) {
    int pad = active_pad();
    bool gp = pad >= 0;
    float sx = gp ? GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_X) : 0, sy = gp ? GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_Y) : 0;
    auto btn = [&](GamepadButton b) { return gp && IsGamepadButtonDown(pad, b); };
    auto hit = [&](GamepadButton b) { return gp && IsGamepadButtonPressed(pad, b); };
    bool up = IsKeyDown(KEY_UP) || btn(GAMEPAD_BUTTON_LEFT_FACE_UP) || sy < -0.6f;
    bool down = IsKeyDown(KEY_DOWN) || btn(GAMEPAD_BUTTON_LEFT_FACE_DOWN) || sy > 0.6f;
    bool left = IsKeyDown(KEY_LEFT) || btn(GAMEPAD_BUTTON_LEFT_FACE_LEFT) || sx < -0.6f;
    bool right = IsKeyDown(KEY_RIGHT) || btn(GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || sx > 0.6f;
    MenuInput in = MenuInput::None;
    if (r_up.fire(up, dt)) in = MenuInput::Up;
    if (r_down.fire(down, dt)) in = MenuInput::Down;
    if (r_left.fire(left, dt)) in = MenuInput::Left;
    if (r_right.fire(right, dt)) in = MenuInput::Right;
    // A (east) chooses, B (south) goes back, Start pauses.
    if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_X) || hit(GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) in = MenuInput::Confirm;
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressed(KEY_Z) || hit(GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) in = MenuInput::Back;
    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P) || hit(GAMEPAD_BUTTON_MIDDLE_RIGHT)) in = MenuInput::Pause;
    return in;
}

bool Menu::move(int d) {
    if (items.empty()) return false;
    int n = (int)items.size(), s = (sel + d + n) % n;
    bool moved = s != sel;
    sel = s;
    return moved;
}

static void draw_items(Rectangle r, const Menu& m, float k, float time) {
    // Rows are 52 tall, a little less when a long list must fit.
    int notes = 0;
    for (const MenuItem& it : m.items) notes += !it.note.empty();
    float row = std::min(52 * k, (r.height - notes * 22 * k) / std::max<size_t>(1, m.items.size())), y = r.y;
    for (int i = 0; i < (int)m.items.size(); i++) {
        const MenuItem& it = m.items[i];
        bool on = i == m.sel;
        float h = it.note.empty() ? row : row + 22 * k;
        if (on) {
            DrawRectangleRec({r.x, y, r.width, h - 6 * k}, kSel);
            float pulse = 0.75f + 0.25f * std::sin(time * 5.0f);
            DrawRectangleRec({r.x, y, 4 * k, h - 6 * k}, Color{kAmber.r, kAmber.g, kAmber.b, (unsigned char)(255 * pulse)});
        }
        float ty = y + 10 * k;
        Color dim = {80, 92, 78, 255};
        text(it.label, r.x + 18 * k, ty, 23 * k, it.locked ? dim : on ? kValue : kLabel);
        if (!it.note.empty()) text(it.note, r.x + 18 * k, ty + 28 * k, 18 * k, kLabel);
        if (!it.value.empty()) {
            float vx = r.x + r.width - 16 * k;
            bool arrows = on && it.arrows && !it.locked;
            if (arrows) {
                text(">", vx, ty, 23 * k, kAmber, 1);
                vx -= 24 * k;
            }
            text(it.value, vx, ty + 1 * k, 21 * k, it.locked ? dim : on ? kAmber : kValue, 1);
            float vw = ui_measure(it.value.c_str(), 21 * k);
            if (arrows) text("<", vx - vw - 10 * k, ty, 23 * k, kAmber, 1);
            if (it.claws >= 0) draw_claws(vx - vw - 64 * k, ty + 2 * k, 20 * k, it.claws);
        }
        y += h;
    }
}

void draw_menu_panel(Rectangle r, const Menu& m, float time) {
    float k = r.height / 480.0f;
    DrawRectangleRec(r, kBg);
    text(m.title, r.x + 28 * k, r.y + 16 * k, 28 * k, kValue);
    if (!m.subtitle.empty()) text(m.subtitle, r.x + 28 * k, r.y + 52 * k, 20 * k, kLabel);
    draw_items({r.x + 14 * k, r.y + 86 * k, r.width - 28 * k, r.height - 130 * k}, m, k, time);
    if (!m.hint.empty()) text(m.hint, r.x + r.width / 2, r.y + r.height - 34 * k, 20 * k, kLabel, 2);
}

void draw_menu_overlay(Rectangle r, const Menu& m, float time, bool left) {
    float k = r.height / 480.0f;
    if (!left) DrawRectangleRec(r, Color{6, 9, 7, 140});
    float h = 120 * k;
    for (const MenuItem& it : m.items) h += it.note.empty() ? 52 * k : 74 * k;
    h = std::min(h, r.height - 8 * k);
    Rectangle box = left ? Rectangle{r.x + r.width * 0.04f, r.y + (r.height - h) / 2, r.width * 0.5f, h}
                         : Rectangle{r.x + r.width * 0.12f, r.y + (r.height - h) / 2, r.width * 0.76f, h};
    DrawRectangleRec(box, Color{kBg.r, kBg.g, kBg.b, 235});
    text(m.title, box.x + 20 * k, box.y + 14 * k, 26 * k, kValue);
    if (!m.subtitle.empty()) text(m.subtitle, box.x + 20 * k, box.y + 46 * k, 18 * k, kLabel);
    draw_items({box.x + 8 * k, box.y + 76 * k, box.width - 16 * k, h - 110 * k}, m, k, time);
    if (!m.hint.empty()) text(m.hint, box.x + box.width / 2, box.y + h - 30 * k, 18 * k, kLabel, 2);
    (void)kPanel;
}

void draw_title(Rectangle r, float time, const char* prompt) {
    float k = r.height / 480.0f;
    // A soft dark band behind the lettering, fading out at the edges.
    float cy = r.y + r.height * 0.17f;  // (high, clear of the raptor)
    // (A smooth curve, in strips of whole pixels: two straight fades meeting in the middle make a
    // crease there, which reads as a dark line across the screen.)
    int top = (int)cy - (int)(90 * k), tall = 2 * (int)(90 * k);
    auto band = [&](int i) {
        float s = std::sin(kPi * i / 16.0f);
        return Color{6, 9, 7, (unsigned char)(150 * s * s)};
    };
    for (int i = 0; i < 16; i++) {
        int y0 = top + tall * i / 16, y1 = top + tall * (i + 1) / 16;
        DrawRectangleGradientV((int)r.x, y0, (int)r.width, y1 - y0, band(i), band(i + 1));
    }
    text("RAPTOR SPRINT", r.x + r.width / 2 + 2 * k, cy - 40 * k + 2 * k, 64 * k, Color{0, 0, 0, 120}, 2);
    text("RAPTOR SPRINT", r.x + r.width / 2, cy - 40 * k, 64 * k, kValue, 2);
    text("a race through the deep forest", r.x + r.width / 2, cy + 34 * k, 22 * k, kLabel, 2);
    if (prompt && prompt[0]) {
        unsigned char a = (unsigned char)(160 + 95 * (0.5f + 0.5f * std::sin(time * 3.0f)));
        text(prompt, r.x + r.width / 2, r.y + r.height * 0.80f, 22 * k, Color{kAmber.r, kAmber.g, kAmber.b, a}, 2);
    }
}

void draw_region_banner(Rectangle r, const Region& rg, int index, bool open) {
    float k = r.height / 480.0f;
    float h = 226 * k, y = r.y + r.height - h;
    DrawRectangleGradientV((int)r.x, (int)(y - 60 * k), (int)r.width, (int)(60 * k), BLANK, Color{6, 9, 7, 200});
    DrawRectangleRec({r.x, y, r.width, h}, Color{6, 9, 7, 200});
    text(TextFormat("REGION %d", index + 1), r.x + 28 * k, y - 4 * k, 20 * k, kAmber);
    text(rg.name, r.x + 28 * k, y + 20 * k, 40 * k, kValue);
    text(rg.blurb, r.x + 28 * k, y + 70 * k, 20 * k, kLabel);
    if (open) {
        text(std::string("NEW  ") + rg.news[0], r.x + 28 * k, y + 106 * k, 20 * k, kValue);
        text(std::string("NEW  ") + rg.news[1], r.x + 28 * k, y + 136 * k, 20 * k, kValue);
        text(std::string("NEW  ") + rg.news[2], r.x + 28 * k, y + 166 * k, 20 * k, kValue);
    } else {
        text("Locked", r.x + 28 * k, y + 116 * k, 24 * k, Color{236, 112, 92, 255});
    }
}

}  // namespace rr
