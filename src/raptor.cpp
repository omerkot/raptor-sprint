#include "raptor.hpp"

#include <cstring>

#include "gfx.hpp"

namespace rr {

namespace {

// The title screen's routine: spots on its patch of trail, and what it does there.
std::vector<Cue> title_routine() {
    const Vector2 a = {-0.2f, 0.0f}, b = {0.3f, 0.9f}, c = {-0.6f, -0.8f}, d = {0.4f, -0.4f};
    return {{RAct::Idle}, {RAct::Look},    {RAct::Turn, b}, {RAct::Walk, b}, {RAct::Sniff}, {RAct::Turn, c},
            {RAct::Leap, c}, {RAct::Call}, {RAct::Turn, d}, {RAct::Walk, d}, {RAct::Shake}, {RAct::Look},
            {RAct::Idle}, {RAct::Turn, a}, {RAct::Leap, a}, {RAct::Sniff}};
}

// The leap: crouch and landing (seconds), and how high it goes.
constexpr float kCrouch = 0.28f, kLand = 0.42f, kLeapHeight = 0.55f;
// Running: speed (stage units a second) and strides a second.
constexpr float kRunSpeed = 7.0f, kCadence = 2.6f;
const float kTailLen[5] = {0.38f, 0.38f, 0.38f, 0.38f, 0.36f};
const float kTailRise[5] = {-0.3f, -0.12f, 0.0f, 0.05f, 0.06f};  // each joint's bend upwards at rest: it slopes down, nearly straight

float ease(float x) {
    x = clampf(x, 0, 1);
    return x * x * (3 - 2 * x);
}
void towards(float& v, float target, float rate, float dt) { v += (target - v) * (1.0f - std::exp(-rate * dt)); }
Vector3 rot_y(Vector3 v, float a) { return {v.x * std::cos(a) + v.z * std::sin(a), v.y, -v.x * std::sin(a) + v.z * std::cos(a)}; }
// A direction pitched `p` up from straight ahead (+z), or from straight back.
Vector3 ahead(float p) { return {0, std::sin(p), std::cos(p)}; }
Vector3 behind(float p) { return {0, std::sin(p), -std::cos(p)}; }
Vector3 spline_point(Vector3 a, Vector3 b, Vector3 c, Vector3 d, float t) {
    float t2 = t * t, t3 = t2 * t;
    auto f = [&](float pa, float pb, float pc, float pd) {
        return 0.5f * (2 * pb + (pc - pa) * t + (2 * pa - 5 * pb + 4 * pc - pd) * t2 + (3 * pb - pa - 3 * pc + pd) * t3);
    };
    return {f(a.x, b.x, c.x, d.x), f(a.y, b.y, c.y, d.y), f(a.z, b.z, c.z, d.z)};
}

// One point on a limb or the spine: where, how wide (w, sideways) and deep (h), how far the
// cross-section hangs below the line (a fraction of h: the belly), how far along its strip of the
// skin map it is (v), how square its section is (box), how much narrower on top (pear), and
// which way is sideways, if not the tube's own.
struct Sample {
    Vector3 p;
    float w, h, drop = 0, v = 0, box = 0, pear = 0;
    Vector3 side = {0, 0, 0};
};

// A part's strip of the skin map (textures.cpp).
struct Strip {
    float x0, x1, y0, y1;
};
constexpr Strip kBody = {0.002f, 0.496f, 0, 1}, kHead = {0.504f, 0.998f, 0.002f, 0.398f}, kJaw = {0.504f, 0.998f, 0.402f, 0.518f},
                kLeg = {0.504f, 0.998f, 0.522f, 0.798f}, kArm = {0.504f, 0.998f, 0.802f, 0.918f}, kToe = {0.504f, 0.93f, 0.922f, 0.998f},
                kPlain = {0.985f, 0.985f, 0.99f, 0.99f};

// Skin: tubes through the samples (on a smooth curve between them), `sub` rings per gap, `around`
// points per ring, starting at the belly. The positions and normals change every frame; the
// rest of the mesh only needs writing once (`full`).
struct Skin {
    std::vector<float>& pos;
    std::vector<float>& nrm;
    std::vector<float>& uv;
    std::vector<unsigned char>& col;
    std::vector<unsigned short>& idx;
    bool full;
    std::vector<Sample> d;
    std::vector<Vector3> P;
    std::vector<Vector2> ring;
    void tube(const std::vector<Sample>& s, int sub, int around, const Strip& st, Vector3 side, Color top = WHITE, Color belly = WHITE) {
        int n = (int)s.size();
        d.clear();
        for (int i = 0; i + 1 < n; i++)
            for (int k = 0; k < sub; k++) {
                float f = (float)k / sub;
                const Vector3& p0 = s[std::max(0, i - 1)].p;
                const Vector3& p3 = s[std::min(n - 1, i + 2)].p;
                const Sample &a = s[i], &b = s[i + 1];
                Sample m;
                m.p = spline_point(p0, a.p, b.p, p3, f);
                float e = lerpf(f, ease(f), 0.5f);
                m.w = lerpf(a.w, b.w, e), m.h = lerpf(a.h, b.h, e), m.drop = lerpf(a.drop, b.drop, e);
                m.v = lerpf(a.v, b.v, f), m.box = lerpf(a.box, b.box, e), m.pear = lerpf(a.pear, b.pear, e);
                m.side = Vector3Lerp(a.side, b.side, f);
                d.push_back(m);
            }
        d.push_back(s[n - 1]);
        int rings = (int)d.size(), row = around + 1;
        P.resize((size_t)rings * row);
        ring.resize(around);
        for (int k = 0; k < around; k++) {
            float th = kTau * k / around - kPi * 0.5f;
            ring[k] = {std::cos(th), std::sin(th)};
        }
        Vector3 right = side;
        for (int i = 0; i < rings; i++) {
            Vector3 T = Vector3Normalize(Vector3Subtract(d[std::min(rings - 1, i + 1)].p, d[std::max(0, i - 1)].p));
            Vector3 want = Vector3LengthSqr(d[i].side) > 0.01f ? d[i].side : side;
            Vector3 r = Vector3Subtract(want, Vector3Scale(T, Vector3DotProduct(want, T)));
            if (Vector3LengthSqr(r) > 1e-4f) right = Vector3Normalize(r);
            Vector3 up = Vector3CrossProduct(T, right);
            Vector3 c = Vector3Subtract(d[i].p, Vector3Scale(up, d[i].drop * d[i].h));
            float e = 1.0f - 0.55f * d[i].box;
            for (int k = 0; k < around; k++) {
                float cs = ring[k].x, sn = ring[k].y;
                if (d[i].box > 0.01f) {
                    cs = std::copysign(std::pow(std::fabs(cs), e), cs);
                    sn = std::copysign(std::pow(std::fabs(sn), e), sn);
                }
                float shut = i == 0 || i == rings - 1 ? 0.0f : 1.0f;  // (the ends close to a point)
                float x = cs * d[i].w * (1.0f - d[i].pear * sn) * shut;
                P[(size_t)i * row + k] = Vector3Add(c, Vector3Add(Vector3Scale(right, x), Vector3Scale(up, sn * d[i].h * shut)));
            }
            P[(size_t)i * row + around] = P[(size_t)i * row];
        }
        int base = (int)pos.size() / 3;
        for (int i = 0; i < rings; i++)
            for (int k = 0; k <= around; k++) {
                const Vector3& p = P[(size_t)i * row + k];
                int ka = (k + around - 1) % around, kb = (k + 1) % around, i0 = std::max(0, i - 1), i1 = std::min(rings - 1, i + 1);
                Vector3 du = Vector3Subtract(P[(size_t)i * row + kb], P[(size_t)i * row + ka]);
                Vector3 dv = Vector3Subtract(P[(size_t)i1 * row + k], P[(size_t)i0 * row + k]);
                Vector3 N = Vector3CrossProduct(du, dv);
                float l = Vector3Length(N);
                N = l > 1e-9f ? Vector3Scale(N, 1.0f / l) : Vector3Normalize(dv);
                pos.insert(pos.end(), {p.x, p.y, p.z});
                nrm.insert(nrm.end(), {N.x, N.y, N.z});
                if (!full) continue;
                float back = std::fabs(1.0f - 2.0f * k / around);  // 0 along the top, 1 along the bottom
                uv.insert(uv.end(), {lerpf(st.x0, st.x1, back), lerpf(st.y0, st.y1, d[i].v)});
                float under = ease((back - 0.55f) / 0.35f);
                col.insert(col.end(), {(unsigned char)lerpf(top.r, belly.r, under), (unsigned char)lerpf(top.g, belly.g, under),
                                       (unsigned char)lerpf(top.b, belly.b, under), 255});
            }
        if (!full) return;
        for (int i = 0; i + 1 < rings; i++)
            for (int k = 0; k < around; k++) {
                unsigned short a = (unsigned short)(base + i * row + k), b = (unsigned short)(a + 1), c = (unsigned short)(a + row), e = (unsigned short)(c + 1);
                idx.insert(idx.end(), {a, b, c, b, e, c});
            }
    }
    // A blade with a broad base, showing from either side.
    void blade(Vector3 a, Vector3 b, Vector3 tip, Vector3 n, Color c) {
        int base = (int)pos.size() / 3;
        for (const Vector3& p : {a, b, tip}) {
            pos.insert(pos.end(), {p.x, p.y, p.z});
            nrm.insert(nrm.end(), {n.x, n.y, n.z});
            if (!full) continue;
            uv.insert(uv.end(), {kPlain.x0, kPlain.y0});
            col.insert(col.end(), {c.r, c.g, c.b, 255});
        }
        if (!full) return;
        unsigned short i = (unsigned short)base;
        idx.insert(idx.end(), {i, (unsigned short)(i + 1), (unsigned short)(i + 2), i, (unsigned short)(i + 2), (unsigned short)(i + 1)});
    }
};

}  // namespace

void RaptorShow::script(std::vector<Cue> cues, int loop_from) {
    cues_ = std::move(cues);
    loop_from_ = loop_from;
    if (started_) begin(0);
}

void RaptorShow::place(Vector2 at, float yaw) {
    pos_ = at, yaw_ = yaw;
    started_ = false;
}

void RaptorShow::begin(int step) {
    if (step >= (int)cues_.size()) step = std::min(loop_from_, (int)cues_.size() - 1);
    step_ = step;
    const Cue& c = cues_[step_];
    act_ = c.act;
    act_t_ = 0;
    called_ = false;
    from_ = to_ = pos_;
    yaw_from_ = yaw_to_ = yaw_;
    perch_from_ = perch_;
    rise_ = c.rise;
    float dist = Vector2Distance(pos_, c.spot);
    switch (act_) {
        case RAct::Idle: act_len_ = 2.4f; break;
        case RAct::Look: act_len_ = 2.2f; break;
        case RAct::Sniff: act_len_ = 2.3f; break;
        case RAct::Call: act_len_ = 2.0f; break;
        case RAct::Shake:
            act_len_ = 1.3f;
            events |= RV_SHAKE;
            break;
        case RAct::Drink:
            act_len_ = 3.2f;
            lap_next_ = 0.7f;
            break;
        case RAct::Turn: {
            float face = dist > 0.05f ? std::atan2(c.spot.x - pos_.x, c.spot.y - pos_.y) : yaw_ + 1.6f;
            yaw_to_ = yaw_ + angle_diff(yaw_, face);
            act_len_ = 0.45f + std::fabs(yaw_to_ - yaw_) / 1.7f;
            break;
        }
        case RAct::Walk:
            to_ = c.spot, perch_ = 0;
            act_len_ = 0.3f + dist / 1.1f;
            break;
        case RAct::Run:
            to_ = c.spot, perch_ = 0;
            act_len_ = c.len > 0 ? c.len : std::max(0.2f, dist / kRunSpeed);
            run_speed_ = dist / act_len_;
            break;
        case RAct::Leap:
            to_ = c.spot;
            leap_air_ = clampf(0.45f + dist * 0.09f, 0.55f, 0.95f);
            act_len_ = kCrouch + leap_air_ + kLand;
            break;
    }
    bool stays = act_ != RAct::Turn && act_ != RAct::Walk && act_ != RAct::Run && act_ != RAct::Leap;
    if (c.len > 0 && stays) act_len_ = c.len;
}

void RaptorShow::update(float dt, Vector3 cam) {
    dt = std::max(1e-4f, std::min(dt, 0.05f));
    events = 0;
    cam_ = cam;
    if (!started_) {
        started_ = true;
        if (cues_.empty()) cues_ = title_routine();
        base_ = ground_at(pos_.x, pos_.y) + perch_;
        for (int i = 0; i < 2; i++) {
            feet_[i].at = Vector3Add({pos_.x, 0, pos_.y}, rot_y({i == 0 ? 0.19f : -0.19f, 0, i == 0 ? 0.22f : -0.1f}, yaw_));
            feet_[i].at.y = ground_at(feet_[i].at.x, feet_[i].at.z) + perch_;
            feet_[i].swing = -1;
        }
        last_y_ = base_ + 1.06f;
        begin(0);
    }
    t_ += dt;
    act_t_ += dt;
    if (act_t_ >= act_len_) begin(step_ + 1);
    float u = clampf(act_t_ / act_len_, 0, 1);
    Vector2 old_pos = pos_;
    float old_yaw = yaw_;

    // Glances: a new place to look every so often, snapped to quickly.
    if (t_ >= look_next_) {
        look_yaw_ = GetRandomValue(-70, 70) / 100.0f;
        look_pitch_ = GetRandomValue(-15, 15) / 100.0f;
        look_next_ = t_ + GetRandomValue(50, 140) / 100.0f;
    }
    // The camera from where it stands.
    float cam_yaw = clampf(angle_diff(yaw_, std::atan2(cam_.x - pos_.x, cam_.z - pos_.y)), -1.3f, 1.3f);
    float cam_pitch = clampf(std::atan2(cam_.y - base_ - 1.4f, std::hypot(cam_.x - pos_.x, cam_.z - pos_.y)), -0.3f, 0.4f);

    float crouch = 0, pitch = 0.09f, neck_low = 0, jaw = 0, tail_lift = 0, hy = look_yaw_, hp = look_pitch_, crouch_rate = 6;
    float bend = 0, hip_x = 0, head_rate = 7, wave_amp = 0.05f, wave_freq = 1.4f;
    airborne_ = 0;
    lift_ = 0;
    float stand = -1e9f;  // where its feet are, if not simply on the ground where it is
    switch (act_) {
        case RAct::Idle: head_rate = 12; break;
        case RAct::Look: hy = cam_yaw, hp = cam_pitch; break;
        case RAct::Turn:
            yaw_ = lerpf(yaw_from_, yaw_to_, ease(u));
            hy = clampf(yaw_to_ - yaw_, -1.0f, 1.0f) * 0.8f, hp = -0.05f;
            break;
        case RAct::Walk:
            pos_ = Vector2Lerp(from_, to_, ease(u * 1.15f));
            hy = 0.15f * look_yaw_, hp = -0.08f, wave_amp = 0.08f, wave_freq = 5.0f;
            break;
        case RAct::Run:
            pos_ = Vector2Lerp(from_, to_, u);
            hy = 0, hp = 0.05f, pitch = -0.06f, neck_low = 0.25f, jaw = 0.12f, tail_lift = 0.03f;
            wave_amp = 0.04f, wave_freq = kTau * kCadence * 1.2f;
            break;
        case RAct::Sniff:
            neck_low = 1, hy = 0.35f * look_yaw_, hp = -0.25f + 0.06f * std::sin(t_ * 14.0f) * (u < 0.85f);
            tail_lift = 0.06f;
            break;
        case RAct::Drink:
            crouch = 0.8f, pitch = -0.16f, neck_low = 1.06f, hy = 0, hp = -0.3f, tail_lift = 0.1f;
            jaw = (u > 0.12f && u < 0.88f) ? 0.12f * (0.5f + 0.5f * std::sin(t_ * 10.0f)) : 0.0f;
            if (act_t_ >= lap_next_ && u < 0.88f) events |= RV_LAP, lap_next_ += 0.55f;
            break;
        case RAct::Shake: {
            float env = std::sin(kPi * u), w = t_ * 26.0f;
            crouch = 0.25f, hp = 0.1f, head_rate = 30;
            hy = 0.55f * std::sin(w) * env, bend = 0.22f * std::sin(w + 1.0f) * env, hip_x = 0.05f * std::sin(w) * env;
            wave_amp = 0.35f * env, wave_freq = 26.0f;
            break;
        }
        case RAct::Leap: {
            float tt = act_t_, y0 = ground_at(from_.x, from_.y) + perch_from_, y1 = ground_at(to_.x, to_.y) + rise_;
            hy = 0, hp = -0.1f;
            if (tt < kCrouch) {
                crouch = 1, pitch = -0.08f, crouch_rate = 16, tail_lift = 0.1f;
                stand = y0;
            } else if (tt < kCrouch + leap_air_) {
                if (tt - dt < kCrouch) events |= RV_TAKEOFF;
                float a = (tt - kCrouch) / leap_air_;
                lift_ = 4.0f * (kLeapHeight + std::max(0.0f, y1 - y0) * 0.6f) * a * (1 - a);
                stand = lerpf(y0, y1, a);
                pos_ = Vector2Lerp(from_, to_, a);
                pitch = 0.2f * (1 - 2 * a), crouch_rate = 20, tail_lift = 0.12f - 0.2f * a;
                airborne_ = ease(a / 0.2f) * (1 - ease((a - 0.72f) / 0.28f));
            } else {
                if (tt - dt < kCrouch + leap_air_) events |= RV_LAND;
                pos_ = to_, perch_ = rise_;
                bool impact = tt < kCrouch + leap_air_ + 0.16f;
                crouch = impact ? 1 : 0, crouch_rate = impact ? 24 : 5, pitch = impact ? -0.1f : 0.09f;
            }
            break;
        }
        case RAct::Call:
            hy = cam_yaw * 0.6f;
            if (u < 0.22f) {
                hp = 0.15f, crouch = 0.4f;
            } else {
                if (!called_) events |= RV_CALL, called_ = true;
                hp = 0.55f + 0.04f * std::sin(t_ * 31.0f) * (u < 0.8f), neck_low = -0.7f, jaw = u < 0.8f ? 0.75f : 0.0f;
                crouch = 0.15f, pitch = 0.12f;
            }
            break;
    }
    base_ = stand > -1e8f ? stand : ground_at(pos_.x, pos_.y) + perch_;
    Vector2 vel = Vector2Scale(Vector2Subtract(pos_, old_pos), 1.0f / dt);
    // Moving, it faces where it's going.
    bool moving = (act_ == RAct::Walk || act_ == RAct::Run || (act_ == RAct::Leap && airborne_ > 0)) && Vector2Length(vel) > 0.3f;
    if (moving) yaw_ += angle_diff(yaw_, std::atan2(vel.x, vel.y)) * (1.0f - std::exp(-(act_ == RAct::Walk ? 5.0f : 8.0f) * dt));
    yaw_rate_ = angle_diff(old_yaw, yaw_) / dt;
    towards(turn_rate_, clampf(yaw_rate_, -4, 4), 6, dt);

    towards(crouch_, crouch, crouch_rate, dt);
    towards(pitch_, pitch, 7, dt);
    towards(neck_low_, neck_low, act_ == RAct::Drink ? 3.0f : 4.0f, dt);
    towards(jaw_, jaw, 14, dt);
    towards(tail_lift_, tail_lift, 5, dt);
    towards(head_yaw_, hy, head_rate, dt);
    towards(head_pitch_, hp, 9, dt);
    towards(puff_, act_ == RAct::Run ? 1.0f : 0.0f, act_ == RAct::Run ? 2.0f : 0.25f, dt);

    // Feet. Running: a stride cycle with both feet off the ground between steps. Otherwise
    // planted on the stage, stepping one at a time when the body has moved away from them.
    bob_ = 0;
    float stance_side = 0;
    if (act_ == RAct::Run) {
        float prev = run_phase_;
        float cadence = kCadence * std::sqrt(std::max(0.3f, run_speed_ / kRunSpeed));  // (faster: longer, quicker strides)
        run_phase_ += dt * cadence;
        float L = run_speed_ * 0.45f / cadence;
        for (int i = 0; i < 2; i++) {
            float ph = run_phase_ + 0.5f * i, p0 = prev + 0.5f * i;
            if (std::floor(ph) != std::floor(p0)) events |= RV_STEP;
            float f = ph - std::floor(ph);
            Vector3 l = {i == 0 ? 0.17f : -0.17f, 0, 0};
            if (f < 0.45f) {
                l.z = 0.1f + L * 0.5f - L * f / 0.45f;
            } else {
                float s = (f - 0.45f) / 0.55f;
                l.z = 0.1f - L * 0.5f + L * ease(s), l.y = std::sin(kPi * s) * 0.3f;
            }
            Vector3 at = Vector3Add({pos_.x, 0, pos_.y}, rot_y(l, yaw_));
            at.y = ground_at(at.x, at.z) + l.y;
            feet_[i].at = at, feet_[i].swing = -1;
        }
        bob_ = 0.035f * std::cos(2 * kTau * run_phase_) - 0.03f;
        hip_x = 0.03f * std::sin(kTau * run_phase_);
    } else {
        bool leaping = act_ == RAct::Leap && act_t_ > kCrouch;
        auto home_of = [&](int i) {
            Vector2 at = leaping ? to_ : pos_;
            float yaw = leaping ? yaw_ : yaw_ + yaw_rate_ * 0.15f;
            Vector3 h = Vector3Add({at.x, 0, at.y}, rot_y({i == 0 ? 0.19f : -0.19f, 0, i == 0 ? 0.22f : -0.1f}, yaw));  // (one foot ahead of the other)
            if (!leaping) h = Vector3Add(h, {vel.x * 0.22f, 0, vel.y * 0.22f});
            h.y = ground_at(h.x, h.z) + (leaping ? rise_ : perch_);
            return h;
        };
        for (int i = 0; i < 2; i++) {
            Foot& f = feet_[i];
            if (leaping) {
                if (act_t_ > kCrouch + leap_air_ * 0.5f) f.at = home_of(i), f.swing = -1;  // (lands where it's going)
                continue;
            }
            if (f.swing >= 0) {
                Vector3 home = home_of(i);
                f.swing += dt / 0.26f;
                float e = ease(f.swing), s = clampf(f.swing, 0, 1);
                f.at = Vector3Lerp(f.from, home, e);
                f.at.y += std::sin(kPi * s) * 0.13f;
                bob_ -= 0.03f * std::sin(kPi * s);
                stance_side = i == 0 ? -1.0f : 1.0f;  // (the other foot carries it)
                if (f.swing >= 1) f.at = home, f.swing = -1, events |= RV_STEP;
            }
        }
        if (!leaping && feet_[0].swing < 0 && feet_[1].swing < 0) {
            float far[2];
            for (int i = 0; i < 2; i++) {
                Vector3 h = home_of(i);
                far[i] = Vector2Distance({feet_[i].at.x, feet_[i].at.z}, {h.x, h.z}) + std::fabs(feet_[i].at.y - h.y);
            }
            bool busy = Vector2Length(vel) > 0.1f || std::fabs(yaw_rate_) > 0.3f;
            int i = far[0] > far[1] ? 0 : 1;
            if (far[i] > (busy ? 0.16f : 0.07f)) feet_[i].swing = 0, feet_[i].from = feet_[i].at;
        }
        if (act_ == RAct::Walk || stance_side != 0) hip_x += stance_side * 0.035f;
    }

    // The body follows through: the spine bends into turns, the hips ride over the standing
    // foot, and the tail lags behind, a little more at each joint.
    bend += clampf(turn_rate_ * 0.12f, -0.3f, 0.3f);
    towards(bend_, bend, 8, dt);
    towards(hip_x_, hip_x, 10, dt);
    float y_now = base_ + lift_ + bob_ - 0.24f * crouch_;
    towards(vy_, clampf((y_now - last_y_) / dt, -3, 3), 12, dt);
    last_y_ = y_now;
    for (int i = 0; i < 5; i++) {
        float wave = wave_amp * std::sin(t_ * wave_freq - i * 0.7f) * (0.5f + 0.2f * i) + 0.02f * std::sin(t_ * 0.7f + i);
        towards(tail_yaw_[i], wave - turn_rate_ * 0.06f, 12.0f - 2.0f * i, dt);
        float rise = kTailRise[i] + (i == 0 ? tail_lift_ * 0.5f : tail_lift_ * 0.25f) - vy_ * 0.03f * (i + 1) / 3.0f;
        towards(tail_pitch_[i], rise, 10.0f - 1.5f * i, dt);
    }
}

void RaptorShow::build_mesh() {
    pos_buf_.clear(), nrm_buf_.clear();
    bool full = !mesh_.vboId;
    Skin sk{pos_buf_, nrm_buf_, uv_buf_, col_buf_, idx_buf_, full, {}, {}, {}};
    const Color horn = {74, 44, 34, 255}, horn_tip = {120, 84, 62, 255}, tooth = {236, 226, 196, 255};
    const Vector3 X = {1, 0, 0}, Y = {0, 1, 0}, Z = {0, 0, 1};

    // The skeleton, in its own frame (x right, y up, z forward, feet on y = 0).
    float breath = std::sin(t_ * (2.2f + 2.5f * puff_)) * (1.0f + 2.0f * puff_);
    float H = 1.06f - 0.24f * crouch_ + bob_ + lift_ + 0.006f * breath;
    float pb = pitch_;
    auto body = [&](Vector3 v) {  // an offset in the pitched body
        return Vector3{v.x, v.y * std::cos(pb) + v.z * std::sin(pb), -v.y * std::sin(pb) + v.z * std::cos(pb)};
    };
    Vector3 hip = {hip_x_, H, 0};
    Vector3 s1 = Vector3Add(hip, Vector3Scale(rot_y(ahead(pb + 0.05f), bend_ * 0.4f), 0.3f));
    Vector3 s2 = Vector3Add(s1, Vector3Scale(rot_y(ahead(pb + 0.1f), bend_ * 0.9f), 0.28f));
    // The neck: an S rising from the shoulders, swinging down and forward as `neck_low_` grows.
    float nl = neck_low_, ny = head_yaw_ * 0.45f + bend_ * 1.1f;
    Vector3 nside = rot_y(X, ny);
    float a1 = std::min(1.3f, 1.08f - 1.35f * nl), a2 = std::min(1.5f, 1.42f - 1.68f * nl), a3 = std::min(1.5f, 1.2f - 1.48f * nl);
    Vector3 n0 = Vector3Add(s2, Vector3Scale(rot_y(ahead(a1 * 0.4f), ny * 0.5f), 0.17f));
    Vector3 n1 = Vector3Add(n0, Vector3Scale(rot_y(ahead(a1), ny), 0.19f));
    Vector3 n2 = Vector3Add(n1, Vector3Scale(rot_y(ahead(a2), ny), 0.23f));
    Vector3 n3 = Vector3Add(n2, Vector3Scale(rot_y(ahead(a3), ny), 0.17f));
    // The head: `at(a, b, c)` is a metres forward from the back of the skull, b up, c right.
    float hp = head_pitch_ - 0.17f - 0.3f * std::max(0.0f, std::min(nl, 1.0f)), hyaw = head_yaw_ + bend_;
    Vector3 hd = {std::sin(hyaw) * std::cos(hp), std::sin(hp), std::cos(hyaw) * std::cos(hp)};
    Vector3 hr = Vector3Normalize(Vector3CrossProduct(Y, hd)), hu = Vector3CrossProduct(hd, hr);
    Vector3 k0 = Vector3Add(n3, Vector3Scale(hu, -0.02f));
    const float S = 1.24f;  // (the head's numbers were measured smaller)
    auto at = [&](float a, float b, float c = 0) {
        return Vector3Add(k0, Vector3Add(Vector3Scale(hd, a * S), Vector3Add(Vector3Scale(hu, b * S), Vector3Scale(hr, c * S))));
    };
    // The tail: each joint turns and lifts from the one before.
    Vector3 tail[7], tside[7];
    tail[0] = hip, tside[0] = X;
    float ty = -bend_ * 0.5f, tp = pb * 0.5f;
    for (int i = 0; i < 5; i++) {
        ty += tail_yaw_[i], tp += tail_pitch_[i];
        tail[i + 1] = Vector3Add(tail[i], Vector3Scale(rot_y(behind(tp), ty), kTailLen[i]));
        tside[i + 1] = rot_y(X, ty);
    }
    tail[6] = Vector3Add(tail[5], Vector3Scale(rot_y(behind(tp), ty), 0.08f));
    tside[6] = tside[5];
    float chest = 1.0f + 0.025f * breath;
    // Tail tip to the top of the neck, which ends inside the skull.
    sk.tube({{tail[6], 0.003f, 0.004f, 0, 1.0f, 0, 0, tside[6]},
             {tail[5], 0.012f, 0.016f, 0, 0.95f, 0, 0, tside[5]},
             {tail[4], 0.029f, 0.038f, 0, 0.86f, 0, 0, tside[4]},
             {tail[3], 0.05f, 0.067f, 0, 0.76f, 0, 0, tside[3]},
             {tail[2], 0.075f, 0.1f, 0, 0.65f, 0, 0.1f, tside[2]},
             {tail[1], 0.115f, 0.15f, 0.05f, 0.54f, 0, 0.15f, tside[1]},
             {hip, 0.185f, 0.235f, 0.12f, 0.43f, 0, 0.18f},
             {s1, 0.205f * chest, 0.27f * chest, 0.24f, 0.33f, 0, 0.2f},
             {s2, 0.18f * chest, 0.275f * chest, 0.42f, 0.24f, 0, 0.15f},
             {n0, 0.145f, 0.2f, 0.22f, 0.16f, 0, 0, nside},
             {n1, 0.122f, 0.158f, 0.03f, 0.1f, 0, 0, nside},
             {n2, 0.102f, 0.125f, 0, 0.05f, 0, 0, nside},
             {n3, 0.09f, 0.108f, 0, 0.015f, 0, 0, hr},
             {at(0.05f, 0.03f), 0.06f, 0.07f, 0, 0, 0, 0, hr}},
            4, 18, kBody, X);
    // The upper head, back to nose: deep and square, with a flat-sided snout. (a, centre, w, h)
    struct Sec {
        float a, b, w, h, v, box;
    };
    static const Sec kSkull[] = {{-0.065f, 0.045f, 0.01f, 0.01f, 1.0f, 0},   {-0.04f, 0.048f, 0.07f, 0.07f, 0.97f, 0.1f}, {0.03f, 0.056f, 0.108f, 0.104f, 0.9f, 0.25f},
                                 {0.14f, 0.07f, 0.128f, 0.108f, 0.74f, 0.42f}, {0.24f, 0.058f, 0.114f, 0.102f, 0.58f, 0.52f},
                                 {0.34f, 0.042f, 0.096f, 0.094f, 0.42f, 0.58f}, {0.46f, 0.026f, 0.083f, 0.09f, 0.24f, 0.6f},
                                 {0.56f, 0.002f, 0.071f, 0.086f, 0.1f, 0.55f},  {0.615f, -0.022f, 0.052f, 0.064f, 0.03f, 0.3f},
                                 {0.64f, -0.034f, 0.008f, 0.008f, 0, 0}};
    std::vector<Sample> skull;
    for (const Sec& c : kSkull) skull.push_back({at(c.a, c.b), c.w * S, c.h * S, 0, c.v, c.box});
    sk.tube(skull, 3, 16, kHead, hr);
    // The lower jaw, hinged under the back of the skull.
    Vector3 hinge = at(0.06f, -0.045f);
    Vector3 jd = Vector3Subtract(Vector3Scale(hd, std::cos(jaw_)), Vector3Scale(hu, std::sin(jaw_)));
    Vector3 ju = Vector3Add(Vector3Scale(hu, std::cos(jaw_)), Vector3Scale(hd, std::sin(jaw_)));
    auto jat = [&](float a, float b, float c = 0) {
        return Vector3Add(hinge, Vector3Add(Vector3Scale(jd, a * S), Vector3Add(Vector3Scale(ju, b * S), Vector3Scale(hr, c * S))));
    };
    sk.tube({{jat(-0.09f, 0.0f), 0.01f, 0.01f, 0, 1.0f},
             {jat(-0.05f, -0.01f), 0.075f, 0.05f, 0, 0.96f, 0.2f},
             {jat(0.02f, -0.04f), 0.108f, 0.075f, 0, 0.84f, 0.35f},
             {jat(0.1f, -0.057f), 0.122f, 0.09f, 0, 0.7f, 0.45f},
             {jat(0.26f, -0.05f), 0.102f, 0.07f, 0, 0.44f, 0.5f},
             {jat(0.41f, -0.053f), 0.081f, 0.049f, 0, 0.2f, 0.5f},
             {jat(0.52f, -0.05f), 0.059f, 0.036f, 0, 0.06f, 0.4f},
             {jat(0.56f, -0.05f), 0.007f, 0.007f, 0, 0}},
            3, 12, kJaw, hr);

    for (int side = 0; side < 2; side++) {
        float sg = side == 0 ? 1.0f : -1.0f;
        // Brow: a heavy ridge over the eye.
        sk.tube({{at(0.09f, 0.135f, sg * 0.094f), 0.006f, 0.006f, 0, 0.78f},
                 {at(0.19f, 0.14f, sg * 0.09f), 0.02f, 0.017f, 0, 0.63f},
                 {at(0.29f, 0.122f, sg * 0.083f), 0.017f, 0.015f, 0, 0.47f},
                 {at(0.41f, 0.078f, sg * 0.068f), 0.005f, 0.005f, 0, 0.3f}},
                2, 6, {kHead.x0 + 0.05f, kHead.x0 + 0.05f, kHead.y0, kHead.y1}, hr);
        // Teeth along both jaws, longest in the middle, just inside the lips.
        for (int i = 0; i < 11; i++) {
            float a = 0.2f + 0.032f * i, f = (float)i / 10, len = 0.014f + 0.014f * std::sin(kPi * f) + 0.006f * ((i * 7) % 3);
            int j = 2;
            while (kSkull[j + 1].a < a) j++;
            const Sec &p = kSkull[j], &q = kSkull[j + 1];
            float e = (a - p.a) / (q.a - p.a), w = lerpf(p.w, q.w, e), h = lerpf(p.h, q.h, e), b = lerpf(p.b, q.b, e);
            Vector3 c = at(a, b - h * 0.8f, sg * w * 0.8f);
            sk.blade(Vector3Add(c, Vector3Scale(hd, 0.013f)), Vector3Subtract(c, Vector3Scale(hd, 0.013f)),
                     Vector3Add(Vector3Subtract(c, Vector3Scale(hu, len)), Vector3Scale(hd, -0.006f)), Vector3Scale(hr, sg), tooth);
        }
        for (int i = 0; i < 9; i++) {
            float a = 0.156f + 0.032f * i, f = (float)i / 8, len = 0.008f + 0.007f * std::sin(kPi * f);
            float w = lerpf(0.104f, 0.062f, f), b = lerpf(0.004f, -0.026f, f);
            Vector3 c = jat(a, b, sg * w * 0.8f);
            sk.blade(Vector3Add(c, Vector3Scale(jd, 0.012f)), Vector3Subtract(c, Vector3Scale(jd, 0.012f)),
                     Vector3Add(c, Vector3Scale(ju, len)), Vector3Scale(hr, sg), tooth);
        }
        // Leg: two-bone reach from the hip to the ankle, the knee forward (a bird's leg).
        const Vector3 shank = {0, 0.35f, -0.1f};
        Vector3 foot = rot_y(Vector3Subtract(feet_[side].at, {pos_.x, 0, pos_.y}), -yaw_);
        foot.y = feet_[side].at.y - base_;
        Vector3 tuck = {sg * 0.18f, H - 0.6f, 0.18f};
        foot = Vector3Lerp(foot, tuck, airborne_);
        foot.y += 0.035f;
        Vector3 socket = Vector3Add(hip, body({sg * 0.145f, -0.1f, 0.03f}));
        Vector3 ankle = Vector3Add(foot, shank);
        const float l1 = 0.5f, l2 = 0.54f;
        Vector3 v = Vector3Subtract(ankle, socket);
        float D = clampf(Vector3Length(v), 0.15f, l1 + l2 - 0.005f);
        Vector3 vn = Vector3Normalize(v);
        ankle = Vector3Add(socket, Vector3Scale(vn, D));
        foot = Vector3Subtract(ankle, shank);
        Vector3 perp = Vector3Normalize(Vector3Subtract(Z, Vector3Scale(vn, Vector3DotProduct(Z, vn))));
        float a = std::acos(clampf((l1 * l1 + D * D - l2 * l2) / (2 * l1 * D), -1, 1));
        Vector3 knee = Vector3Add(socket, Vector3Scale(Vector3Add(Vector3Scale(vn, std::cos(a)), Vector3Scale(perp, std::sin(a))), l1));
        knee.x += sg * 0.03f;
        // The thigh is a great slab of muscle, hanging back of the bone towards the tail.
        sk.tube({{Vector3Add(socket, body({-sg * 0.03f, 0.2f, -0.03f})), 0.03f, 0.05f, 0, 0.0f},
                 {Vector3Add(socket, body({0, 0.11f, -0.02f})), 0.105f, 0.235f, 0.1f, 0.1f},
                 {socket, 0.125f, 0.27f, 0.22f, 0.2f},
                 {Vector3Lerp(socket, knee, 0.45f), 0.112f, 0.2f, 0.32f, 0.3f},
                 {Vector3Lerp(socket, knee, 0.8f), 0.08f, 0.115f, 0.35f, 0.37f},
                 {knee, 0.06f, 0.08f, 0.1f, 0.43f},
                 {Vector3Lerp(knee, ankle, 0.3f), 0.07f, 0.105f, -0.1f, 0.5f},
                 {Vector3Lerp(knee, ankle, 0.65f), 0.056f, 0.08f, -0.1f, 0.58f},
                 {ankle, 0.056f, 0.074f, 0, 0.66f},
                 {Vector3Lerp(ankle, foot, 0.5f), 0.05f, 0.064f, 0, 0.8f},
                 {foot, 0.07f, 0.062f, 0, 0.94f},
                 {Vector3Add(foot, {0, -0.02f, 0.04f}), 0.012f, 0.01f, 0, 1.0f}},
                4, 12, kLeg, X);
        // Two toes forward...
        for (int t = 0; t < 2; t++) {
            float off = t == 0 ? -0.03f : 0.035f, len = t == 0 ? 1.0f : 0.84f;
            Vector3 dir = Vector3Normalize({sg * 0.1f + off * 3.0f * sg, 0, 1});
            Vector3 b = Vector3Add(foot, {sg * off, -0.005f, 0.02f});
            auto along = [&](float f, float y) { return Vector3Add(b, Vector3Add(Vector3Scale(dir, f * len), {0, y, 0})); };
            sk.tube({{along(-0.03f, 0), 0.006f, 0.006f, 0, 0.0f}, {along(0.01f, 0.006f), 0.043f, 0.04f, 0, 0.1f},
                     {along(0.09f, 0.016f), 0.04f, 0.04f, 0, 0.42f}, {along(0.17f, 0.004f), 0.034f, 0.032f, 0, 0.74f},
                     {along(0.225f, -0.002f), 0.024f, 0.024f, 0, 1.0f}},
                    2, 7, kToe, X);
            sk.tube({{along(0.21f, 0.0f), 0.021f, 0.022f}, {along(0.26f, 0.004f), 0.016f, 0.019f}, {along(0.31f, -0.012f), 0.008f, 0.011f},
                     {along(0.335f, -0.034f), 0.001f, 0.001f}},
                    2, 6, kPlain, X, horn, horn);
        }
        // ...and the second, carried clear of the ground, with the great sickle claw.
        Vector3 c0 = Vector3Add(foot, {-sg * 0.045f, 0.015f, 0.0f});
        auto rel = [&](float y, float z) { return Vector3Add(c0, {-sg * 0.012f, y, z}); };
        sk.tube({{Vector3Add(c0, {sg * 0.03f, -0.01f, -0.04f}), 0.006f, 0.006f, 0, 0.0f}, {c0, 0.042f, 0.044f, 0, 0.2f},
                 {rel(0.05f, 0.03f), 0.038f, 0.042f, 0, 0.55f}, {rel(0.082f, 0.075f), 0.032f, 0.038f, 0, 1.0f}},
                2, 7, kToe, X);
        sk.tube({{rel(0.08f, 0.07f), 0.028f, 0.037f}, {rel(0.13f, 0.13f), 0.021f, 0.03f}, {rel(0.126f, 0.2f), 0.015f, 0.021f},
                 {rel(0.074f, 0.26f), 0.008f, 0.011f}, {rel(0.02f, 0.285f), 0.001f, 0.001f}},
                3, 6, kPlain, X, horn, horn_tip);
        // Arm: down from the shoulder, the forearm reaching forward, the long hand hanging with
        // its claws. (Folded up when the chest is low; raised when it rears; swinging at a run.)
        float fold = clampf(crouch_ * 0.7f + std::max(0.0f, nl) * 0.45f + puff_ * 0.8f, 0, 1), raise = clampf(-nl, 0, 1) * 0.08f;
        float swing = 0.012f * std::sin(t_ * 1.7f + side) + puff_ * 0.05f * std::sin(kTau * run_phase_ + side * kPi);
        Vector3 sh = Vector3Add(s2, body({sg * 0.145f, -0.1f, 0.02f}));
        Vector3 el = Vector3Add(sh, body({sg * 0.04f, -0.2f + 0.08f * fold, -0.05f + swing}));
        Vector3 wr = Vector3Add(el, body({-sg * 0.015f, -0.05f + raise + 0.12f * fold, 0.36f - 0.09f * fold + swing}));
        Vector3 hdir = Vector3Normalize(body({0, -1.0f, -0.06f + 0.25f * fold + swing * 3}));
        Vector3 pa = Vector3Add(wr, Vector3Scale(hdir, 0.15f));
        sk.tube({{Vector3Add(sh, body({-sg * 0.06f, 0.12f, 0.0f})), 0.01f, 0.02f, 0, 0.0f},
                 {Vector3Add(sh, body({-sg * 0.02f, 0.05f, 0.0f})), 0.05f, 0.09f, 0, 0.08f},
                 {sh, 0.066f, 0.105f, 0, 0.16f},
                 {Vector3Lerp(sh, el, 0.6f), 0.06f, 0.088f, 0, 0.32f},
                 {el, 0.056f, 0.074f, 0, 0.46f},
                 {Vector3Lerp(el, wr, 0.45f), 0.054f, 0.07f, 0, 0.66f},
                 {wr, 0.042f, 0.046f, 0, 0.86f},
                 {pa, 0.062f, 0.036f, 0, 1.0f}},
                3, 10, kArm, X);
        // Three long fingers, each with a hooked claw.
        for (int f = 0; f < 3; f++) {
            float o = (float)(f - 1), len = f == 1 ? 0.2f : 0.16f;
            Vector3 b = Vector3Add(pa, body({o * 0.04f, 0, o * 0.012f}));
            Vector3 fd = Vector3Normalize(Vector3Add(hdir, body({o * 0.1f, 0, 0.1f})));
            Vector3 hook = body({0, 0, -1});
            auto along = [&](float s, float c) { return Vector3Add(b, Vector3Add(Vector3Scale(fd, s), Vector3Scale(hook, c))); };
            sk.tube({{along(-0.03f, 0), 0.026f, 0.026f, 0, 0.0f}, {along(len * 0.5f, 0.008f), 0.024f, 0.025f, 0, 0.5f}, {along(len, 0.0f), 0.021f, 0.022f, 0, 1.0f}},
                    2, 6, kToe, X);
            sk.tube({{along(len - 0.01f, 0.0f), 0.015f, 0.017f}, {along(len + 0.05f, -0.012f), 0.011f, 0.013f}, {along(len + 0.11f, 0.008f), 0.005f, 0.007f},
                     {along(len + 0.14f, 0.04f), 0.001f, 0.001f}},
                    2, 5, kPlain, X, horn, horn);
        }
    }
    // A few points on the stage, for splashes and the like.
    auto stage = [&](Vector3 l) { return Vector3Add(rot_y(l, yaw_), {pos_.x, base_, pos_.y}); };
    head_ = stage(at(0.6f, -0.05f)), hip_ = stage(hip), chest_ = stage(s1);
}

void RaptorShow::draw(const Matrix& stage) {
    build_mesh();
    int n = (int)pos_buf_.size() / 3;
    if (!mesh_.vboId) {
        mesh_.vertexCount = n;
        mesh_.triangleCount = (int)idx_buf_.size() / 3;
        mesh_.vertices = (float*)MemAlloc(sizeof(float) * 3 * n);
        mesh_.normals = (float*)MemAlloc(sizeof(float) * 3 * n);
        mesh_.texcoords = (float*)MemAlloc(sizeof(float) * 2 * n);
        mesh_.colors = (unsigned char*)MemAlloc(4 * n);
        mesh_.indices = (unsigned short*)MemAlloc(sizeof(unsigned short) * idx_buf_.size());
        std::memcpy(mesh_.vertices, pos_buf_.data(), sizeof(float) * 3 * n);
        std::memcpy(mesh_.normals, nrm_buf_.data(), sizeof(float) * 3 * n);
        std::memcpy(mesh_.texcoords, uv_buf_.data(), sizeof(float) * 2 * n);
        std::memcpy(mesh_.colors, col_buf_.data(), 4 * n);
        std::memcpy(mesh_.indices, idx_buf_.data(), sizeof(unsigned short) * idx_buf_.size());
        UploadMesh(&mesh_, true);

        // A soft round shadow under it.
        MeshBuilder sb;
        const int seg = 20;
        for (int i = 0; i < seg; i++) {
            float a0 = kTau * i / seg, a1 = kTau * (i + 1) / seg;
            Color in = {0, 0, 0, 150}, out = {0, 0, 0, 0};
            Vector3 c = {0, 0, 0}, p0 = {std::cos(a0), 0, std::sin(a0)}, p1 = {std::cos(a1), 0, std::sin(a1)};
            Vector3 up = {0, 1, 0};
            sb.vtri(c, p1, p0, up, up, up, in, out, out, {0, 0}, {0, 0}, {0, 0});
        }
        shadow_ = sb.build();
        shadow_mat_ = LoadMaterialDefault();
    } else if (n == mesh_.vertexCount) {
        UpdateMeshBuffer(mesh_, 0, pos_buf_.data(), sizeof(float) * 3 * n, 0);
        UpdateMeshBuffer(mesh_, 2, nrm_buf_.data(), sizeof(float) * 3 * n, 0);
    }
    Matrix root = xform({pos_.x, base_, pos_.y}, {0, yaw_, 0});
    Matrix to_world = compose(MatrixScale(kScale, kScale, kScale), stage);
    // The shadow first. The ground under it is never quite flat, and a disc laid on it would flicker
    // in and out, so it's drawn nearer the camera than it really is: enough to clear the ground's
    // bumps, not enough to show through a rock or a tree. The raptor then covers it where it should.
    float s = 1.0f - 0.35f * clampf(lift_ / kLeapHeight, 0, 1);
    Vector3 near = Vector3Scale(Vector3Normalize(Vector3Subtract(cam_, {pos_.x, base_, pos_.y})), 0.3f);
    Matrix sm = compose(compose(compose(xform({0, 0.03f, -0.15f}, {0, 0, 0}, {0.55f * s, 1, 1.35f * s}), root), MatrixTranslate(near.x, near.y, near.z)), to_world);
    shadow_mat_.maps[MATERIAL_MAP_DIFFUSE].color = Color{255, 255, 255, (unsigned char)(255 * s)};
    if (!gfx.full) {  // (there the sun casts a real one)
        rlDisableDepthMask();
        rlColorMask(true, true, true, false);  // (its alpha mustn't reach the view's, or the shadow turns see-through)
        DrawMesh(shadow_, shadow_mat_, sm);
        rlColorMask(true, true, true, true);
        rlEnableDepthMask();
    }
    gfx.draw(mesh_, compose(root, to_world), gfx.tex.raptor, WHITE, {0.02f, 0.016f, 0.01f});
}

void RaptorShow::unload() {
    if (mesh_.vboId) UnloadMesh(mesh_);
    if (shadow_.vboId) UnloadMesh(shadow_);
    mesh_ = shadow_ = Mesh{};
}

}  // namespace rr
