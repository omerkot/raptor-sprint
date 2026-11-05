#include "outro.hpp"

#include <algorithm>
#include <cstring>

#include "audio.hpp"
#include "gfx.hpp"
#include "regions.hpp"
#include "world.hpp"

namespace rr {

namespace {

float ease(float x) {
    x = clampf(x, 0, 1);
    return x * x * (3 - 2 * x);
}
float frand(float a, float b) { return a + (b - a) * GetRandomValue(0, 10000) / 10000.0f; }
Vector3 add(Vector3 a, Vector3 b) { return Vector3Add(a, b); }
Vector3 mul(Vector3 a, float s) { return Vector3Scale(a, s); }
const Vector3 kUp = {0, 1, 0};

}  // namespace

Vector3 Outro::trail(float d, float side) const {
    const auto& pts = course_->pts;
    int i = clampf((float)fin_ + d, 0, (float)pts.size() - 1);
    return add(pts[i].p, mul(heading_right(pts[i].yaw), side));
}

Vector2 Outro::spot(float d, float side) const {
    Vector3 s = to_stage(trail(d, side));
    return {s.x, s.z};
}

void Outro::start(const Course& c, const Player& p, int region, int variant) {
    stop();
    course_ = &c;
    region_ = ((region % kRegionsMax) + kRegionsMax) % kRegionsMax;
    variant_ = variant & 3;
    active = true;
    t = 0, shake_ = 0, spray_acc_ = 0;
    creaked_ = crashed_ = false;
    bits_.clear();

    // The stage: a stretch of the trail beyond the finish (it runs on 60 m), x right, z back.
    int n = (int)c.pts.size();
    fin_ = std::min(n - 1, (int)c.length);
    const PathPt& o = c.pts[std::min(n - 1, fin_ + 18)];
    origin_ = o.p;
    frame_ = basis(heading_right(o.yaw), kUp, Vector3Negate(heading_dir(o.yaw)), origin_);
    float k = RaptorShow::kScale;
    to_world_ = compose(MatrixScale(k, k, k), frame_);
    to_stage_ = MatrixInvert(to_world_);

    raptor_ = RaptorShow();
    raptor_.ground = [this](float x, float z) {
        Vector3 w = to_world({x, 0, z});
        return (course_->ground(w.x, w.z, fin_ + 15) - origin_.y) / RaptorShow::kScale;
    };
    // It starts where the view was: its eyes where ours were, running at our pace.
    Vector3 dir = heading_dir(p.yaw);
    Vector3 at = to_stage(Vector3Subtract(p.pos, mul(dir, 0.95f))), here = to_stage(p.pos), ahead = to_stage(add(p.pos, dir));
    raptor_.place({at.x, at.z}, std::atan2(ahead.x - here.x, ahead.z - here.z));
    float fast = Vector2Distance({at.x, at.z}, spot(13)) * k / clampf(p.speed, 8, 16);

    std::vector<Cue> cues;
    int loop = 0;
    auto run_in = [&](float stop_at) {
        cues.push_back({RAct::Run, spot(13), fast});
        cues.push_back({RAct::Run, spot(stop_at)});
    };
    switch (region_) {
        case 0:  // Fern Hollow: it pulls up, turns back to the trail it ran, and calls.
            run_in(17);
            cues.insert(cues.end(), {{RAct::Walk, spot(19)}, {RAct::Turn, spot(5)}, {RAct::Call}});
            loop = (int)cues.size();
            cues.insert(cues.end(), {{RAct::Look}, {RAct::Idle}, {RAct::Sniff}});
            break;
        case 1: {  // Riverlands: a drink at a pool beside the trail, then it shakes itself dry.
            // A pond beside the trail: an uneven shore, and a muddy bank sloping down into the
            // ground all round (so the ground meets it in a clean line rather than lying level with it).
            pool_at_ = trail(21, 2.1f);
            float top = -1e9f, low = 1e9f;
            for (int i = 0; i < 13; i++) {
                float a = kTau * i / 12, rr_ = i ? pool_r_ + 0.6f : 0;
                float g = c.ground(pool_at_.x + std::cos(a) * rr_, pool_at_.z + std::sin(a) * rr_, fin_ + 21);
                top = std::max(top, g), low = std::min(low, g);
            }
            pool_at_.y = top + 0.04f;
            auto shore = [&](float a) { return pool_r_ * (1.0f + 0.12f * std::sin(3 * a + 0.7f) + 0.07f * std::sin(5 * a + 2.1f)); };
            MeshBuilder wb, bb;
            const int SEG = 40;
            Vector3 up = kUp;
            Color water = rgb(0.30f, 0.37f, 0.36f), wet = rgb(0.55f, 0.47f, 0.38f), dry = rgb(0.85f, 0.78f, 0.66f);
            float sink = pool_at_.y - low + 0.3f;
            for (int i = 0; i < SEG; i++) {
                float a0 = kTau * i / SEG, a1 = kTau * (i + 1) / SEG;
                Vector3 d0 = {std::cos(a0), 0, std::sin(a0)}, d1 = {std::cos(a1), 0, std::sin(a1)};
                Vector3 w0 = mul(d0, shore(a0)), w1 = mul(d1, shore(a1));
                wb.vtri({0, 0, 0}, w1, w0, up, up, up, water, water, water, {0, 0}, {0, 0}, {0, 0});
                // The bank in three rings: under the water's edge, a low lip, then down into the ground.
                Vector3 r0[4] = {add(mul(d0, shore(a0) - 0.15f), {0, -0.05f, 0}), add(mul(d0, shore(a0) + 0.2f), {0, -0.03f, 0}),
                                 add(mul(d0, shore(a0) + 0.55f), {0, -0.12f, 0}), add(mul(d0, shore(a0) + 1.1f), {0, -sink, 0})};
                Vector3 r1[4] = {add(mul(d1, shore(a1) - 0.15f), {0, -0.05f, 0}), add(mul(d1, shore(a1) + 0.2f), {0, -0.03f, 0}),
                                 add(mul(d1, shore(a1) + 0.55f), {0, -0.12f, 0}), add(mul(d1, shore(a1) + 1.1f), {0, -sink, 0})};
                Color k[4] = {wet, wet, Color{(unsigned char)((wet.r + dry.r) / 2), (unsigned char)((wet.g + dry.g) / 2), (unsigned char)((wet.b + dry.b) / 2), 255}, dry};
                auto uv = [](Vector3 p) { return Vector2{p.x * 0.5f, p.z * 0.5f}; };
                for (int j = 0; j < 3; j++) {
                    bb.vtri(r0[j], r1[j], r0[j + 1], up, up, up, k[j], k[j], k[j + 1], uv(r0[j]), uv(r1[j]), uv(r0[j + 1]));
                    bb.vtri(r1[j], r1[j + 1], r0[j + 1], up, up, up, k[j], k[j + 1], k[j + 1], uv(r1[j]), uv(r1[j + 1]), uv(r0[j + 1]));
                }
            }
            pool_ = wb.build(), bank_ = bb.build();
            Vector3 from = trail(19, 0), d = Vector3Normalize(Vector3Subtract({pool_at_.x, from.y, pool_at_.z}, from));
            float toward = std::atan2(-d.z, -d.x);  // (the shore's angle on the side facing the trail)
            Vector3 stand = to_stage(Vector3Subtract(pool_at_, mul(d, shore(toward) + 0.55f))), pool = to_stage(pool_at_);
            run_in(16);
            cues.insert(cues.end(), {{RAct::Walk, {stand.x, stand.z}}, {RAct::Turn, {pool.x, pool.z}}, {RAct::Drink}, {RAct::Look}, {RAct::Shake}});
            loop = (int)cues.size();
            cues.insert(cues.end(), {{RAct::Idle}, {RAct::Look}});
            break;
        }
        case 2:  // The Thicket: it stops to shake the leaves off, sniffs, and calls back into it.
            run_in(17);
            cues.insert(cues.end(), {{RAct::Walk, spot(19)}, {RAct::Shake}, {RAct::Sniff}, {RAct::Turn, spot(5)}, {RAct::Call}});
            loop = (int)cues.size();
            cues.insert(cues.end(), {{RAct::Look}, {RAct::Idle}});
            break;
        case 3: {  // Granite Rise: up onto a boulder, to call from the top.
            float r = 0.95f;
            boulder_at_ = trail(21, 0);
            boulder_ = make_boulder(r);
            run_in(17.5f);
            cues.insert(cues.end(), {{RAct::Leap, spot(21), 0, r * 1.22f / k}, {RAct::Turn, spot(5)}, {RAct::Call}});
            loop = (int)cues.size();
            cues.insert(cues.end(), {{RAct::Look}, {RAct::Idle}, {RAct::Call}});
            break;
        }
        case 4:  // The Mire: it shakes the mud off, then picks its way on.
            run_in(17);
            cues.insert(cues.end(), {{RAct::Walk, spot(19)}, {RAct::Shake}, {RAct::Sniff}, {RAct::Walk, spot(21.5f, -0.5f)}});
            loop = (int)cues.size();
            cues.insert(cues.end(), {{RAct::Look}, {RAct::Idle}});
            break;
        default: {  // Deadwood: a dead tree comes down across the trail just behind it.
            tree_base_ = trail(10.5f, 3.4f);
            tree_axis_ = Vector3Negate(heading_right(c.pts[std::min(n - 1, fin_ + 10)].yaw));
            tree_ = make_dead_tree(0.32f, tree_len_, false);
            tree_lift_ = 0.1f + 0.32f * 0.85f;
            tree_t0_ = std::max(0.3f, fast * 0.82f - 0.25f);
            run_in(17);
            cues.insert(cues.end(), {{RAct::Walk, spot(19)}, {RAct::Turn, spot(10.5f)}, {RAct::Call}, {RAct::Sniff}});
            loop = (int)cues.size();
            cues.insert(cues.end(), {{RAct::Look}, {RAct::Idle}});
            break;
        }
    }
    raptor_.script(cues, loop);
    focus_ = add(p.pos, {0, 0.8f, 0});
    aim(0, true);
}

void Outro::stop() {
    active = false;
    for (int i = 0; i < kRing; i++)
        for (Mesh* m : {&blobs_[i], &leaves_[i]})
            if (m->vboId) UnloadMesh(*m), *m = Mesh{};
    if (puff_.id) UnloadTexture(puff_), puff_ = Texture2D{};
    raptor_.unload();
    for (Mesh* m : {&boulder_, &tree_, &pool_, &bank_})
        if (m->vboId) UnloadMesh(*m), *m = Mesh{};
    bits_.clear();
}

// Where the camera wants to be for this trail's shot.
void Outro::aim(float dt, bool snap) {
    // (Between the hips and the head, so its face stays in the shot, above all at a long reach to drink.)
    float lean = raptor_.act() == RAct::Drink ? 0.85f : 0.3f;
    Vector3 hip = t > 0.05f ? to_world(Vector3Lerp(raptor_.hip(), raptor_.head(), lean)) : focus_;
    if (snap) focus_ = hip;
    else focus_ = Vector3Lerp(focus_, hip, 1.0f - std::exp(-6.0f * dt));
    Nearest nr = course_->nearest(focus_, fin_ + 12);
    float yaw = course_->pts[nr.i].yaw;
    Vector3 f = heading_dir(yaw), r = heading_right(yaw), want, look;
    switch (variant_) {
        case 0:  // following, then holding back as it stops
            want = add(add(add(focus_, mul(f, -4.6f)), mul(r, 2.0f)), {0, 0.9f, 0});
            look = add(add(focus_, mul(f, 0.8f)), {0, 0.1f, 0});
            break;
        case 1: {  // circling round it
            float a = kPi - 0.3f * t;
            want = add(add(focus_, mul(add(mul(f, std::cos(a)), mul(r, std::sin(a))), 4.4f)), {0, 1.2f + 0.3f * std::sin(t * 0.4f), 0});
            look = add(focus_, {0, 0.1f, 0});
            break;
        }
        case 2:  // from up the trail, as it comes
            want = add(trail(24.5f, -1.2f), {0, 0.75f, 0});
            look = add(focus_, {0, 0.15f, 0});
            break;
        default: {  // rising up and away
            float e = ease(t / 7.0f);
            want = add(add(add(focus_, mul(f, -2.0f - 1.2f * e)), mul(r, 2.8f - 0.6f * e)), {0, 0.9f + 2.6f * e, 0});
            look = add(focus_, mul(f, 0.5f * (1 - e)));
            break;
        }
    }
    if (snap) {
        cam_pos_ = want, cam_look_ = look;
    } else {
        cam_pos_ = Vector3Lerp(cam_pos_, want, 1.0f - std::exp(-(t < 2.0f ? 5.0f : 3.0f) * dt));
        cam_look_ = Vector3Lerp(cam_look_, look, 1.0f - std::exp(-5.0f * dt));
    }
}

void Outro::spray(int kind, Vector3 from, float spread, int count) {
    for (int i = 0; i < count && bits_.size() < 600; i++) {
        float a = frand(0, kTau);
        Vector3 out = {std::cos(a), 0, std::sin(a)};
        Bit b{};
        b.kind = kind;
        b.spin = frand(0, kTau);
        // Drops, mud and leaves leave from the surface (`spread` out), the rest from anywhere inside it.
        bool surface = kind == Drop || kind == Mud || kind == Leaf;
        b.p = add(from, mul(out, surface ? spread : frand(0, spread)));
        b.p.y += frand(-spread, spread) * (surface ? 0.8f : 0.5f);
        switch (kind) {
            case Drop:
                b.v = add(mul(out, frand(1.5f, 3.5f)), {0, frand(1.0f, 2.5f), 0});
                b.max = frand(0.6f, 1.0f), b.size = frand(0.012f, 0.022f), b.c = {190, 205, 210, 255};
                break;
            case Mud:
                b.v = add(mul(out, frand(1.5f, 3.8f)), {0, frand(1.0f, 2.5f), 0});
                b.max = frand(0.7f, 1.1f), b.size = frand(0.015f, 0.03f), b.c = {92, 70, 48, 255};
                break;
            case Leaf:
                b.v = add(mul(out, frand(0.5f, 1.5f)), {0, frand(1.0f, 2.0f), 0});
                b.max = frand(1.8f, 2.6f), b.size = frand(0.09f, 0.14f);
                b.c = {(unsigned char)GetRandomValue(150, 205), (unsigned char)GetRandomValue(170, 220), (unsigned char)GetRandomValue(90, 130), 255};
                break;
            case Dust:
                b.v = add(mul(out, frand(0.2f, 0.9f)), {0, frand(0.3f, 1.0f), 0});
                b.max = frand(1.0f, 1.7f), b.size = frand(0.2f, 0.36f), b.c = {150, 140, 122, 46};
                break;
        }
        b.life = b.max;
        bits_.push_back(b);
    }
}

void Outro::update(float dt) {
    if (!active) return;
    t += dt;
    aim(dt, false);
    raptor_.update(dt, to_stage(cam_pos_));

    // Sounds, quieter the further it is from the camera.
    unsigned ev = raptor_.events;
    Vector3 body = to_world(raptor_.hip());
    float near = clampf(5.0f / std::max(0.5f, Vector3Distance(body, cam_pos_)), 0.25f, 1.0f);
    if (ev & RV_STEP) audio.play(Sfx::StepDirt, 0.4f * near, GetRandomValue(92, 108) / 100.0f);
    if (ev & RV_TAKEOFF) audio.play(Sfx::Jump, 0.6f * near);
    if (ev & RV_LAND) {
        audio.play(Sfx::Land, 0.8f * near);
        if (region_ == 3) audio.play(Sfx::HitRock, 0.35f * near, 1.2f), spray(Dust, add(boulder_at_, {0, 1.3f, 0}), 0.5f, 8);
    }
    if (ev & RV_CALL) audio.play(Sfx::Call, 0.95f * std::max(near, 0.6f));
    if (ev & RV_SHAKE) {
        audio.play(Sfx::Shake, 0.8f * near);
        if (region_ == 1) audio.play(Sfx::Splash, 0.35f * near, 1.4f);
        if (region_ == 4) audio.play(Sfx::Squelch, 0.6f * near);
    }
    if (ev & RV_LAP) audio.play(Sfx::Splash, 0.12f * near, 1.9f);
    // Shaking itself off: water, leaves or mud fly.
    if (raptor_.act() == RAct::Shake && (region_ == 1 || region_ == 2 || region_ == 4)) {
        float env = std::sin(kPi * clampf(raptor_.act_time() / 1.3f, 0, 1));
        spray_acc_ += dt * (region_ == 2 ? 45.0f : 110.0f) * env;
        Vector3 hip = to_world(raptor_.hip()), chest = to_world(raptor_.chest());
        while (spray_acc_ >= 1) {
            spray_acc_ -= 1;
            spray(region_ == 1 ? Drop : region_ == 2 ? Leaf : Mud, Vector3Lerp(hip, chest, frand(-0.3f, 1.0f)), 0.2f, 1);
        }
    }
    // The dead tree: a creak, the fall, the crash and its dust.
    if (tree_.vboId) {
        if (!creaked_ && t > tree_t0_ - 0.3f) creaked_ = true, audio.play(Sfx::Creak, 0.9f);
        if (!crashed_ && t > tree_t0_ + 0.95f) {
            crashed_ = true, shake_ = 1.0f;
            audio.play(Sfx::Crash);
            for (float d = 0.5f; d < tree_len_; d += 0.7f) spray(Dust, add(tree_base_, mul(tree_axis_, d)), 0.4f, 2);
        }
    }
    shake_ = std::max(0.0f, shake_ - dt * 1.8f);

    for (Bit& b : bits_) {
        b.life -= dt;
        switch (b.kind) {
            case Drop:
            case Mud:
                b.v.y -= 9.8f * dt;
                break;
            case Leaf:
                b.v.y -= 2.5f * dt;
                b.v = mul(b.v, 1.0f - 1.5f * dt);
                b.p.x += std::sin(b.life * 7.0f + b.size * 90.0f) * 0.4f * dt;
                b.p.z += std::cos(b.life * 5.0f + b.size * 70.0f) * 0.4f * dt;
                break;
            case Dust:
                b.v = mul(b.v, 1.0f - 1.2f * dt);
                b.size += 0.25f * dt;
                break;
        }
        b.p = add(b.p, mul(b.v, dt));
        if (b.kind == Drop || b.kind == Mud || b.kind == Leaf) {
            float g = course_->ground(b.p.x, b.p.z, fin_ + 15) + 0.05f;
            if (b.p.y < g) {
                b.p.y = g, b.v = {0, 0, 0};
                if (b.kind != Leaf) b.life = std::min(b.life, 0.15f);
            }
        }
    }
    bits_.erase(std::remove_if(bits_.begin(), bits_.end(), [](const Bit& b) { return b.life <= 0; }), bits_.end());
}

Camera3D Outro::camera(const Camera3D& eyes, float beside_card) const {
    float w = ease(t / 1.1f);
    Vector3 pos = Vector3Lerp(eyes.position, cam_pos_, w), look = Vector3Lerp(eyes.target, cam_look_, w);
    if (shake_ > 0) {
        float s = shake_ * shake_ * 0.12f;
        pos = add(pos, {s * std::sin(t * 53.0f), s * std::sin(t * 41.0f + 1.0f), s * std::sin(t * 59.0f + 2.0f)});
    }
    if (beside_card > 0) {
        // Turn just far enough, on this shape of screen, to put the raptor in the middle of the space
        // right of the card: halfway from the middle of the screen to its right edge.
        Vector3 d = Vector3Subtract(focus_, pos);
        Vector3 side = Vector3Normalize(Vector3CrossProduct(d, kUp));
        float off = 0.5f * beside_card * std::tan(26.0f * DEG2RAD);  // (half of the camera's 52 degrees)
        look = Vector3Lerp(look, add(Vector3Subtract(focus_, mul(side, Vector3Length(d) * off)), {0, 0.1f, 0}), w);
    }
    Camera3D c = {};
    c.position = pos;
    c.target = look;
    c.up = kUp;
    c.fovy = 52.0f;
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}

void Outro::build_bits() {
    turn_ = (turn_ + 1) % kRing;
    if (!blobs_[turn_].vboId) {
        auto make = [](Mesh& m, int verts) {
            m.vertexCount = verts, m.triangleCount = verts / 3;
            m.vertices = (float*)MemAlloc(sizeof(float) * 3 * verts);
            m.normals = (float*)MemAlloc(sizeof(float) * 3 * verts);
            m.texcoords = (float*)MemAlloc(sizeof(float) * 2 * verts);
            m.colors = (unsigned char*)MemAlloc(4 * verts);
            UploadMesh(&m, true);
        };
        make(blobs_[turn_], kMaxBlobs * 24);
        make(leaves_[turn_], kMaxLeaves * 6);
    }
    bp_.assign(kMaxBlobs * 24 * 3, 0.0f), bn_.assign(bp_.size(), 0.0f), bc_.assign(kMaxBlobs * 24 * 4, 255);
    lp_.assign(kMaxLeaves * 6 * 3, 0.0f), ln_.assign(lp_.size(), 0.0f), lc_.assign(kMaxLeaves * 6 * 4, 255);
    int nb = 0, nl = 0;
    auto put = [](std::vector<float>& P, std::vector<float>& N, std::vector<unsigned char>& C, int at, Vector3 p, Vector3 n, Color c) {
        P[at * 3] = p.x, P[at * 3 + 1] = p.y, P[at * 3 + 2] = p.z;
        N[at * 3] = n.x, N[at * 3 + 1] = n.y, N[at * 3 + 2] = n.z;
        C[at * 4] = c.r, C[at * 4 + 1] = c.g, C[at * 4 + 2] = c.b, C[at * 4 + 3] = 255;
    };
    for (const Bit& b : bits_) {
        float fade = clampf(b.life / 0.25f, 0, 1);  // (shrinks away at the end)
        if (b.kind == Drop || b.kind == Mud) {
            if (nb >= kMaxBlobs) continue;
            // An eight-sided blob, drawn out along its flight.
            float speed = Vector3Length(b.v), r = b.size * fade;
            Vector3 u = speed > 0.2f ? Vector3Scale(b.v, 1.0f / speed) : kUp;
            Vector3 e1 = Vector3Normalize(Vector3CrossProduct(u, std::fabs(u.y) < 0.9f ? kUp : Vector3{1, 0, 0})), e2 = Vector3CrossProduct(u, e1);
            float len = r * (1.0f + std::min(speed * 0.35f, 2.0f));
            Vector3 tip = add(b.p, mul(u, len)), tail = add(b.p, mul(u, -r)), ring[4] = {add(b.p, mul(e1, r)), add(b.p, mul(e2, r)), add(b.p, mul(e1, -r)), add(b.p, mul(e2, -r))};
            int v = nb * 24;
            for (int k = 0; k < 4; k++) {
                Vector3 a = ring[k], c = ring[(k + 1) % 4];
                for (Vector3 end : {tip, tail}) {
                    Vector3 p1 = a, p2 = c;  // (wound to face outwards: the backs are culled)
                    Vector3 n = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p1, end), Vector3Subtract(p2, end)));
                    if (Vector3DotProduct(n, Vector3Subtract(end, b.p)) + Vector3DotProduct(n, Vector3Subtract(a, b.p)) < 0) std::swap(p1, p2), n = Vector3Negate(n);
                    put(bp_, bn_, bc_, v++, end, n, b.c), put(bp_, bn_, bc_, v++, p1, n, b.c), put(bp_, bn_, bc_, v++, p2, n, b.c);
                }
            }
            nb++;
        } else if (b.kind == Leaf) {
            if (nl >= kMaxLeaves) continue;
            // A leaf: a small diamond tumbling as it falls.
            float a = b.spin + b.life * 5.0f, s = b.size * fade;
            Vector3 e1 = Vector3Normalize({std::cos(a), 0.4f * std::sin(a * 1.3f), std::sin(a)});
            Vector3 e2 = Vector3Normalize(Vector3CrossProduct(e1, Vector3Normalize({std::sin(a * 0.7f), 1.0f, std::cos(a * 0.9f)})));
            Vector3 n = Vector3CrossProduct(e1, e2);
            Vector3 p0 = add(b.p, mul(e1, s * 0.6f)), p1 = add(b.p, mul(e2, s * 0.25f)), p2 = add(b.p, mul(e1, -s * 0.4f)), p3 = add(b.p, mul(e2, -s * 0.25f));
            int v = nl * 6;
            put(lp_, ln_, lc_, v++, p0, n, b.c), put(lp_, ln_, lc_, v++, p1, n, b.c), put(lp_, ln_, lc_, v++, p2, n, b.c);
            put(lp_, ln_, lc_, v++, p0, n, b.c), put(lp_, ln_, lc_, v++, p2, n, b.c), put(lp_, ln_, lc_, v++, p3, n, b.c);
            nl++;
        }
    }
    UpdateMeshBuffer(blobs_[turn_], 0, bp_.data(), (int)(bp_.size() * sizeof(float)), 0);
    UpdateMeshBuffer(blobs_[turn_], 2, bn_.data(), (int)(bn_.size() * sizeof(float)), 0);
    UpdateMeshBuffer(blobs_[turn_], 3, bc_.data(), (int)bc_.size(), 0);
    UpdateMeshBuffer(leaves_[turn_], 0, lp_.data(), (int)(lp_.size() * sizeof(float)), 0);
    UpdateMeshBuffer(leaves_[turn_], 2, ln_.data(), (int)(ln_.size() * sizeof(float)), 0);
    UpdateMeshBuffer(leaves_[turn_], 3, lc_.data(), (int)lc_.size(), 0);
}

void Outro::draw(const Camera3D& cam) {
    if (!active) return;
    const Biome& b = region(region_).biome;
    if (boulder_.vboId) gfx.draw(boulder_, MatrixTranslate(boulder_at_.x, boulder_at_.y, boulder_at_.z), gfx.tex.rock, b.bark);
    if (pool_.vboId) {
        gfx.draw(bank_, MatrixTranslate(pool_at_.x, pool_at_.y, pool_at_.z), gfx.tex.floor, b.ground);
        gfx.draw(pool_, MatrixTranslate(pool_at_.x, pool_at_.y, pool_at_.z), gfx.tex.white, WHITE, {0.02f, 0.03f, 0.035f});
    }
    if (tree_.vboId) {
        float th = kPi * 0.5f * clampf((t - tree_t0_) / 0.95f, 0, 1);
        th *= clampf((t - tree_t0_) / 0.95f, 0, 1);  // (slow to start, faster as it goes)
        Vector3 k = Vector3Normalize(Vector3CrossProduct(kUp, tree_axis_));
        Matrix m = compose(MatrixRotate(k, th), MatrixTranslate(tree_base_.x, tree_base_.y + tree_lift_ * std::sin(th), tree_base_.z));
        gfx.draw(tree_, m, gfx.tex.bark, b.bark);
    }
    raptor_.draw(frame_);
    build_bits();
    gfx.draw(blobs_[turn_], MatrixIdentity(), gfx.tex.white, WHITE, region_ == 1 ? Vector3{0.03f, 0.035f, 0.04f} : Vector3{0, 0, 0});
    gfx.draw_cut(leaves_[turn_], MatrixIdentity(), gfx.tex.white, b.leaves);
}

// Dust: soft puffs the colour of the haze, so they melt into it, laid over the finished view with the
// rest of the 2D. In the 3D pass the handhelds' GPUs drop them for several frames at a time (it
// doesn't show on the desktop), and a soft puff has no use for the depth buffer anyway.
void Outro::draw_dust(const Camera3D& cam, Rectangle view) {
    if (!active) return;
    if (!puff_.id) {
        // (White fading to clear white: fading to clear black would darken the puffs' edges.)
        Image im = GenImageGradientRadial(32, 32, 0.0f, WHITE, Color{255, 255, 255, 0});
        puff_ = LoadTextureFromImage(im);
        SetTextureFilter(puff_, TEXTURE_FILTER_BILINEAR);  // (raylib's default is hard pixels, which shimmer as a puff moves)
        UnloadImage(im);
    }
    Color haze = {(unsigned char)clampf(gfx.fog_col.x * 230 + 25, 0, 255), (unsigned char)clampf(gfx.fog_col.y * 222 + 22, 0, 255),
                  (unsigned char)clampf(gfx.fog_col.z * 210 + 18, 0, 255), 255};
    Vector3 ahead = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(ahead, cam.up)), up = Vector3CrossProduct(right, ahead);
    float focal = view.height * 0.5f / std::tan(cam.fovy * DEG2RAD * 0.5f);
    for (const Bit& p : bits_) {
        if (p.kind != Dust) continue;
        Vector3 d = Vector3Subtract(p.p, cam.position);
        float depth = Vector3DotProduct(d, ahead);
        if (depth < 0.3f) continue;
        float k = focal / depth, r = p.size * k;
        Vector2 at = {view.x + view.width * 0.5f + Vector3DotProduct(d, right) * k, view.y + view.height * 0.5f - Vector3DotProduct(d, up) * k};
        Color c = haze;
        c.a = (unsigned char)(p.c.a * clampf(p.life / p.max * 1.5f, 0, 1) * clampf((p.max - p.life) * 4.0f, 0, 1));
        DrawTexturePro(puff_, {0, 0, 32, 32}, {at.x, at.y, r * 2, r * 2}, {r, r}, (p.spin + p.life * 0.3f) * RAD2DEG, c);
    }
}

}  // namespace rr
