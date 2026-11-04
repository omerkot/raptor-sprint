#include "hud.hpp"

#include <fstream>

#include "progress.hpp"
#include "ui.hpp"

namespace rr {

namespace {

const Color kBg = {14, 19, 16, 255}, kPanel = {22, 29, 24, 255}, kLine = {44, 56, 46, 255};
const Color kLabel = {128, 150, 124, 255}, kValue = {228, 234, 220, 255}, kTrail = {150, 140, 112, 255};
const Color kAmber = {236, 186, 92, 255}, kGood = {140, 206, 110, 255}, kBad = {236, 112, 92, 255};

void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    // raylib wants counter-clockwise order on screen.
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0) std::swap(b, c);
    DrawTriangle(a, b, c, col);
}

// A convex shape, cut to fit a box. (By hand: raylib's scissor sends what's been drawn so far to the
// GPU mid-frame, and on the XU Mini M's driver the next batch then waits for the whole 3D view to finish.)
void fill_clipped(Rectangle box, const Vector2* shape, int n, Color col) {
    Vector2 a[12], b[12];
    int count = n;
    std::copy(shape, shape + n, a);
    for (int edge = 0; edge < 4 && count > 0; edge++) {
        // How far inside this edge of the box a point is.
        auto in = [&](Vector2 p) {
            return edge == 0 ? p.x - box.x : edge == 1 ? box.x + box.width - p.x : edge == 2 ? p.y - box.y : box.y + box.height - p.y;
        };
        int out = 0;
        for (int i = 0; i < count; i++) {
            Vector2 p = a[i], q = a[(i + 1) % count];
            float dp = in(p), dq = in(q);
            if (dp >= 0) b[out++] = p;
            if ((dp >= 0) != (dq >= 0)) b[out++] = Vector2Lerp(p, q, dp / (dp - dq));
        }
        count = out;
        std::copy(b, b + count, a);
    }
    for (int i = 1; i + 1 < count; i++) tri(a[0], a[i], a[i + 1], col);
}

void arrow(Vector2 at, float yaw_screen, float size, Color col) {
    Vector2 f = {std::sin(yaw_screen), -std::cos(yaw_screen)}, r = {-f.y, f.x};
    Vector2 tip = {at.x + f.x * size, at.y + f.y * size};
    Vector2 l = {at.x - f.x * size * 0.7f - r.x * size * 0.65f, at.y - f.y * size * 0.7f - r.y * size * 0.65f};
    Vector2 rr_ = {at.x - f.x * size * 0.7f + r.x * size * 0.65f, at.y - f.y * size * 0.7f + r.y * size * 0.65f};
    Vector2 back = {at.x - f.x * size * 0.35f, at.y - f.y * size * 0.35f};
    tri(tip, l, back, col);
    tri(tip, back, rr_, col);
}

void label(const char* s, float x, float y, float size, Color c, int align = 0) {
    float w = ui_measure(s, size);
    ui_draw(s, align == 0 ? x : align == 1 ? x - w : x - w / 2, y, size, c);
}

// The whole course as a line, north (the start's heading) up, fitted into `box`.
void course_map(Rectangle box, const Course& c, const Player& p, float k) {
    float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
    for (const PathPt& q : c.pts) minx = std::min(minx, q.p.x), maxx = std::max(maxx, q.p.x), minz = std::min(minz, q.p.z), maxz = std::max(maxz, q.p.z);
    float pad = 22 * k;
    float sc = std::min((box.width - pad * 2) / std::max(1.0f, maxx - minx), (box.height - pad * 2) / std::max(1.0f, maxz - minz));
    float ox = box.x + box.width / 2 - (minx + maxx) / 2 * sc, oy = box.y + box.height / 2 - (minz + maxz) / 2 * sc;
    auto P = [&](Vector3 v) { return Vector2{ox + v.x * sc, oy + v.z * sc}; };
    int n = (int)c.pts.size();
    for (int i = 0; i + 4 < n; i += 4) DrawLineEx(P(c.pts[i].p), P(c.pts[i + 4].p), 5 * k, kLine);
    for (int i = 0; i + 4 < n; i += 4) DrawLineEx(P(c.pts[i].p), P(c.pts[i + 4].p), 2.2f * k, i < p.near.i ? kAmber : kTrail);
    for (size_t j = 0; j < c.checkpoints.size(); j++) {
        Vector2 q = P(c.pts[c.checkpoints[j]].p);
        bool last = j + 1 == c.checkpoints.size();
        bool passed = (int)j < p.next_check;
        if (last) {
            float s = 5 * k;
            for (int a = 0; a < 2; a++)
                for (int b = 0; b < 2; b++) DrawRectangleV({q.x - s + a * s, q.y - s + b * s}, {s, s}, (a + b) % 2 ? kValue : BLACK);
        } else {
            DrawCircleV(q, 5 * k, passed ? kAmber : kPanel);
            DrawCircleLinesV(q, 5 * k, passed ? kAmber : kTrail);
        }
    }
    DrawCircleV(P(c.pts[0].p), 3.5f * k, kTrail);
    arrow(P(p.pos), p.yaw, 9 * k, kValue);
}

// The next stretch of trail as a ribbon, turned so the raptor faces up. No obstacles: the raptor
// knows the way, not what's lying across it.
void ahead_map(Rectangle box, const Course& c, const Player& p, float k, float metres) {
    DrawRectangleRec(box, kPanel);
    float sc = box.height * 0.82f / metres;
    Vector2 base = {box.x + box.width / 2, box.y + box.height * 0.88f};
    Vector3 f = heading_dir(p.yaw), r = heading_right(p.yaw);
    auto L = [&](Vector3 w) {
        Vector3 d = Vector3Subtract(w, p.pos);
        return Vector2{base.x + (d.x * r.x + d.z * r.z) * sc, base.y - (d.x * f.x + d.z * f.z) * sc};
    };
    int n = (int)c.pts.size();
    int i0 = std::max(0, p.near.i - 12), i1 = std::min(n - 1, p.near.i + (int)metres + 30);
    for (int i = i0; i + 2 <= i1; i += 2) {
        const PathPt &a = c.pts[i], &b = c.pts[i + 2];
        Vector3 ra = heading_right(a.yaw), rb = heading_right(b.yaw);
        Vector2 al = L(Vector3Subtract(a.p, Vector3Scale(ra, a.half))), ar = L(Vector3Add(a.p, Vector3Scale(ra, a.half)));
        Vector2 bl = L(Vector3Subtract(b.p, Vector3Scale(rb, b.half))), br = L(Vector3Add(b.p, Vector3Scale(rb, b.half)));
        float fade = 1.0f - clampf((float)(i - p.near.i) / (metres + 20), 0, 1) * 0.75f;
        Color col = {(unsigned char)(kTrail.r * fade + kPanel.r * (1 - fade)), (unsigned char)(kTrail.g * fade + kPanel.g * (1 - fade)),
                     (unsigned char)(kTrail.b * fade + kPanel.b * (1 - fade)), 255};
        Vector2 quad[4] = {al, ar, br, bl};
        fill_clipped(box, quad, 4, col);
    }
    // Checkpoint and finish lines across the ribbon.
    for (int ci : c.checkpoints) {
        if (ci < p.near.i || ci > i1) continue;
        const PathPt& q = c.pts[ci];
        Vector3 rq = heading_right(q.yaw);
        Vector2 a = L(Vector3Subtract(q.p, Vector3Scale(rq, q.half + 1))), b = L(Vector3Add(q.p, Vector3Scale(rq, q.half + 1)));
        Vector2 d = Vector2Subtract(b, a), w = Vector2Scale(Vector2Normalize({-d.y, d.x}), 1.25f * k);
        Vector2 quad[4] = {Vector2Add(a, w), Vector2Add(b, w), Vector2Subtract(b, w), Vector2Subtract(a, w)};
        fill_clipped(box, quad, 4, kAmber);
    }
    for (int m = 25; m <= (int)metres; m += 25) {  // distance ticks
        float y = base.y - m * sc;
        DrawLineEx({box.x + 6 * k, y}, {box.x + 14 * k, y}, 1.5f * k, kLine);
    }
    arrow(base, 0, 8 * k, kValue);
}

float claw_time_ui(float par, int n) { return claw_time(par, n); }

}  // namespace

std::string format_time(float t) {
    int m = (int)(t / 60);
    float s = t - m * 60;
    char buf[32];
    snprintf(buf, sizeof buf, "%d:%04.1f", m, s);
    return buf;
}

void Best::load(const std::string& path) {
    std::ifstream in(path);
    total = 0;
    splits.clear();
    float v;
    if (in >> v) total = v;
    while (in >> v) splits.push_back(v);
}

void Best::save(const std::string& path) const {
    std::ofstream out(path);
    out << total;
    for (float s : splits) out << ' ' << s;
    out << '\n';
}

void draw_claws(float x, float y, float size, int got) {
    for (int i = 0; i < 3; i++) {
        float x0 = x + i * size * 0.55f;
        Color c = i < got ? kAmber : kLine;
        DrawLineEx({x0 + size * 0.32f, y}, {x0, y + size}, size * 0.16f, c);
    }
}

void draw_lower(Rectangle r, const Course& c, const Player& p, const Best& best, const HudState& h) {
    float k = r.height / 480.0f;
    const float LBL = 20 * k;  // the smallest text anywhere
    DrawRectangleRec(r, kBg);
    Rectangle panel = {r.x + 14 * k, r.y + 14 * k, 300 * k, r.height - 28 * k};
    DrawRectangleRec(panel, kPanel);
    label(h.trail.empty() ? "COURSE" : h.trail.c_str(), panel.x + 12 * k, panel.y + 10 * k, LBL, kLabel);
    course_map({panel.x, panel.y + 34 * k, panel.width, panel.height - 34 * k}, c, p, k);

    float x = r.x + 330 * k, w = r.width - 344 * k;
    label("TIME", x, r.y + 12 * k, LBL, kLabel);
    label(format_time(p.time).c_str(), x, r.y + 34 * k, 42 * k, kValue);
    // Against the best time, at the last checkpoint passed.
    int j = (int)p.splits.size() - 1;
    if (j >= 0 && j < (int)best.splits.size()) {
        float d = p.splits[j] - best.splits[j];
        char buf[32];
        snprintf(buf, sizeof buf, "%+.1f", d);
        label(buf, x + w, r.y + 44 * k, 26 * k, d <= 0 ? kGood : kBad, 1);
    } else if (best.total > 0) {
        label(("BEST " + format_time(best.total)).c_str(), x + w, r.y + 12 * k, LBL, kLabel, 1);
    }

    Rectangle ahead = {x, r.y + 88 * k, w, 176 * k};
    ahead_map(ahead, c, p, k, 100);
    label("AHEAD", ahead.x + 10 * k, ahead.y + 8 * k, LBL, kLabel);

    float y = r.y + 274 * k;
    char buf[64];
    label("SPEED", x, y, LBL, kLabel);
    snprintf(buf, sizeof buf, "%d km/h", (int)std::round(p.speed * 3.6f));
    label(buf, x, y + 24 * k, 28 * k, kValue);
    label("TO GO", x + w, y, LBL, kLabel, 1);
    snprintf(buf, sizeof buf, "%d m", (int)std::max(0.0f, c.length - p.near.s));
    label(buf, x + w, y + 24 * k, 28 * k, kValue, 1);

    y += 66 * k;
    label(p.winded ? "BREATH  WINDED" : "BREATH", x, y, LBL, p.winded ? kBad : kLabel);
    Rectangle bar = {x, y + 26 * k, w, 14 * k};
    DrawRectangleRounded(bar, 0.5f, 6, kLine);
    DrawRectangleRounded({bar.x, bar.y, bar.width * p.breath, bar.height}, 0.5f, 6, p.winded ? kBad : kGood);

    y += 54 * k;
    label("STUMBLES", x, y, LBL, kLabel);
    snprintf(buf, sizeof buf, "%d", p.hits);
    label(buf, x + w, y, LBL, p.hits ? kBad : kValue, 1);
    label("CHECKPOINT", x, y + 28 * k, LBL, kLabel);
    snprintf(buf, sizeof buf, "%d / %d", p.next_check, (int)c.checkpoints.size());
    label(buf, x + w, y + 28 * k, LBL, kValue, 1);
}

void draw_upper_overlay(Rectangle r, const Course& c, const Player& p, const Best& best, const HudState& h, bool compact) {
    float k = r.height / 480.0f;
    const float LBL = 20 * k;
    char buf[48];
    auto edge_glow = [&](Color edge) {
        float t = 46 * k;
        DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)t, edge, BLANK);
        DrawRectangleGradientV((int)r.x, (int)(r.y + r.height - t), (int)r.width, (int)t, BLANK, edge);
        DrawRectangleGradientH((int)r.x, (int)r.y, (int)t, (int)r.height, edge, BLANK);
        DrawRectangleGradientH((int)(r.x + r.width - t), (int)r.y, (int)t, (int)r.height, BLANK, edge);
    };
    if (h.flash > 0) edge_glow(Color{150, 20, 10, (unsigned char)(h.flash * 150)});
    if (h.boost > 0) edge_glow(Color{30, 190, 70, (unsigned char)(h.boost * 170)});
    if (h.reward > 0 && h.phase == Phase::Race) {
        // A clean clear: a warm glow at the edges and the word, popping up and drifting away.
        float a = h.reward, age = 1.0f - a;
        Color glow = {236, 196, 96, (unsigned char)(70 * a)};
        float t = 40 * k;
        if (h.boost <= 0) {  // (the green says it all)
            DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)t, glow, BLANK);
            DrawRectangleGradientV((int)r.x, (int)(r.y + r.height - t), (int)r.width, (int)t, BLANK, glow);
        }
        float pop = 1.0f + 0.4f * std::exp(-age * 14.0f), y = r.y + r.height * (compact ? 0.27f : 0.22f) - age * 24 * k;
        unsigned char al = (unsigned char)(255 * clampf(a * 2.5f, 0, 1));
        label(h.reward_word.c_str(), r.x + r.width / 2 + 2 * k, y + 2 * k, 40 * k * pop, Color{0, 0, 0, (unsigned char)(al / 2)}, 2);
        label(h.reward_word.c_str(), r.x + r.width / 2, y, 40 * k * pop, Color{kAmber.r, kAmber.g, kAmber.b, al}, 2);
        if (h.streak >= 2) {
            snprintf(buf, sizeof buf, "STREAK  %d", h.streak);
            label(buf, r.x + r.width / 2, y + 48 * k, 22 * k, Color{kGood.r, kGood.g, kGood.b, al}, 2);
        }
    }
    if (compact && h.phase != Phase::Finished) {
        // One screen: the essentials across the top, the stretch ahead in the corner.
        DrawRectangleGradientV((int)r.x, (int)r.y, (int)r.width, (int)(96 * k), Color{6, 9, 7, 150}, BLANK);
        label(format_time(p.time).c_str(), r.x + 14 * k, r.y + 8 * k, 32 * k, kValue);
        Rectangle bar = {r.x + 16 * k, r.y + 50 * k, 170 * k, 12 * k};
        DrawRectangleRounded(bar, 0.5f, 6, kLine);
        DrawRectangleRounded({bar.x, bar.y, bar.width * p.breath, bar.height}, 0.5f, 6, p.winded ? kBad : kGood);
        snprintf(buf, sizeof buf, "CP %d/%d   %d m", p.next_check, (int)c.checkpoints.size(), (int)std::max(0.0f, c.length - p.near.s));
        label(buf, r.x + 16 * k, r.y + 68 * k, LBL, kLabel);
        if (p.hits) {
            snprintf(buf, sizeof buf, "STUMBLES %d", p.hits);
            label(buf, r.x + 200 * k, r.y + 44 * k, LBL, kBad);
        }
        ahead_map({r.x + r.width - 134 * k, r.y + 12 * k, 120 * k, 120 * k}, c, p, k * 0.7f, 80);
    }
    if (h.phase == Phase::Countdown) {
        int n = (int)std::ceil(h.countdown);
        const char* s = n >= 3 ? "3" : n == 2 ? "2" : n == 1 ? "1" : "RUN";
        bool card = !h.intro_title.empty();
        if (!h.hold) label(s, r.x + r.width / 2, r.y + r.height * (card ? 0.12f : 0.32f), 72 * k, kValue, 2);
        if (card) {
            float hh = (56 + 34 * h.intro_lines.size() + (h.hold ? 40 : 0)) * k;
            Rectangle box = {r.x + r.width * 0.06f, r.y + r.height * 0.42f, r.width * 0.88f, hh};
            DrawRectangleRec(box, Color{10, 14, 11, 215});
            DrawRectangleRec({box.x, box.y, 5 * k, box.height}, kAmber);
            label(h.intro_title.c_str(), box.x + 20 * k, box.y + 12 * k, 26 * k, kAmber);
            for (size_t i = 0; i < h.intro_lines.size(); i++)
                label(h.intro_lines[i].c_str(), box.x + 20 * k, box.y + (50 + 34 * i) * k, 21 * k, kValue);
            if (h.hold) label("A  continue", box.x + box.width - 20 * k, box.y + hh - 38 * k, 21 * k, kAmber, 1);
        }
    } else if (h.phase == Phase::Race && h.go && p.time < 1.0f) {
        label("RUN", r.x + r.width / 2, r.y + r.height * 0.32f, 72 * k, Color{228, 234, 220, (unsigned char)(255 * (1 - p.time))}, 2);
    }
}

void draw_finish_card(Rectangle r, const Player& p, const Best& best, const HudState& h, float alpha, bool left) {
    float k = r.height / 480.0f;
    const float LBL = 20 * k;
    auto fade = [&](Color c) { return Color{c.r, c.g, c.b, (unsigned char)(c.a * clampf(alpha, 0, 1))}; };
    float hh = (h.par > 0 ? (left ? 332 : 304) : 236) * k;
    Rectangle card = left ? Rectangle{r.x + r.width * 0.03f, r.y + (r.height - hh) * 0.5f, r.width * 0.47f, hh}
                          : Rectangle{r.x + r.width * 0.1f, r.y + (r.height - hh) * 0.45f, r.width * 0.8f, hh};
    DrawRectangleRec(card, fade(Color{10, 14, 11, 222}));
    float cx = card.x + card.width / 2, y = card.y + 14 * k;
    label("FINISH", cx, y, 26 * k, fade(kAmber), 2);
    label(format_time(p.time).c_str(), cx, y + 32 * k, 50 * k, fade(kValue), 2);
    y += 96 * k;
    if (h.par > 0) {
        float cw = 3 * 26 * k * 0.55f + 26 * k * 0.32f;
        if (alpha > 0.5f) draw_claws(cx - cw / 2, y, 26 * k, h.claws);
        y += 38 * k;
        std::string t3 = "3 claws  " + format_time(claw_time_ui(h.par, 3)), t2 = "2 claws  " + format_time(claw_time_ui(h.par, 2));
        if (left) {
            label(t3.c_str(), cx, y, LBL, fade(kLabel), 2);
            label(t2.c_str(), cx, y + 26 * k, LBL, fade(kLabel), 2);
            y += 56 * k;
        } else {
            label((t3 + "      " + t2).c_str(), cx, y, LBL, fade(kLabel), 2);
            y += 30 * k;
        }
    }
    // How cleanly it went.
    char buf[64];
    bool flawless = h.passed > 0 && h.clean == h.passed;
    if (flawless) snprintf(buf, sizeof buf, "Flawless  %d of %d clean", h.clean, h.passed);
    else if (h.best_streak >= 2) snprintf(buf, sizeof buf, "%d of %d clean   streak %d", h.clean, h.passed, h.best_streak);
    else snprintf(buf, sizeof buf, "%d of %d clean", h.clean, h.passed);
    if (h.passed > 0) label(buf, cx, y, LBL, fade(flawless ? kAmber : kValue), 2);
    y += 30 * k;
    std::string sub = !h.news.empty() ? h.news : h.new_best ? "New best time" : (best.total > 0 ? "Best " + format_time(best.total) : "");
    label(sub.c_str(), cx, y, 22 * k, fade((!h.news.empty() || h.new_best) ? kGood : kLabel), 2);
    label(h.hint.c_str(), cx, card.y + card.height - 34 * k, LBL, fade(kLabel), 2);
}

}  // namespace rr
