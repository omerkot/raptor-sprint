#include "player.hpp"

namespace rr {

// A falling tree lands just after the raptor would get there at the speed it was going, so that
// it's still coming down at about body height then: too low to run under, too high to leap.
constexpr float kFallLate = 0.15f;

Tuning tuning_for(int difficulty) {
    Tuning t;
    float f = difficulty_pace(difficulty);
    t.pace = f;
    t.run_speed *= f, t.sprint_speed *= f, t.rough_speed *= f, t.jump_speed *= f, t.turn_rate *= f;
    t.accel *= f * f, t.brake *= f * f, t.gravity *= f * f;
    return t;
}

void Player::reset(const Course& c) {
    Tuning keep = tune;
    *this = Player{};
    tune = keep;
    pos = c.pts[2].p;
    yaw = c.pts[2].yaw;
    hint = 2;
    near = c.nearest(pos, hint);
    fall_start.assign(c.obstacles.size(), -1.0f);
    roll_speed.assign(c.obstacles.size(), 0.0f);
    fall_time.assign(c.obstacles.size(), 1.0f);
}

float Player::fall_angle(int k) const {
    if (k < 0 || k >= (int)fall_start.size() || fall_start[k] < 0) return 0;
    float t = (clock - fall_start[k]) / fall_time[k];
    return kPi * 0.5f * std::min(1.0f, t * t);  // accelerating, like a real fall
}

float Player::roll(const Obstacle& o, int k) const {
    if (k < 0 || k >= (int)fall_start.size() || fall_start[k] < 0) return 0;
    constexpr float settle = 3.0f;  // slowing to rest over the last few metres
    float d = roll_speed[k] * (clock - fall_start[k]), rest = o.len - settle;
    if (d > rest) d = o.len - settle * std::exp(-(d - rest) / settle);
    return d / o.len;
}

// Where an obstacle band (creek, mud) is: inside it if within r along the trail and len/2 across.
static bool in_band(const Obstacle& o, Vector3 p) {
    Vector3 d = Vector3Subtract(p, o.c);
    float across = d.x * o.axis.x + d.z * o.axis.z;
    float along = d.x * -o.axis.z + d.z * o.axis.x;
    return std::fabs(along) < o.r && std::fabs(across) < o.len * 0.5f;
}

void Player::step(float dt, const Controls& in, const Course& c, bool racing) {
    const Tuning& T = tune;
    events = 0;
    clock += dt;
    if (fall_start.size() != c.obstacles.size()) fall_start.assign(c.obstacles.size(), -1.0f), roll_speed.assign(c.obstacles.size(), 0.0f), fall_time.assign(c.obstacles.size(), 1.0f);
    float stride_before = stride;
    bool duck_before = duck > 0.5f;
    shake = std::max(0.0f, shake - dt * 2.5f);
    land = std::max(0.0f, land - dt * 4.0f);
    steer_vis += (in.steer - steer_vis) * std::min(1.0f, dt * 6.0f);

    // Breath: sprinting spends it; running gets it back. Empty means no sprint until 30 %.
    bool sprinting = racing && in.sprint && !winded && stumble <= 0 && speed > 4;
    breath = clampf(breath + (sprinting ? -T.breath_use : T.breath_regain) * dt, 0, 1);
    if (breath <= 0) winded = true;
    if (winded && breath > 0.3f) winded = false;

    // Ducking works in the air too: jump, hold B, and land low under a branch.
    duck += ((in.duck ? 1.0f : 0.0f) - duck) * std::min(1.0f, dt * 12.0f * T.pace);

    // Underfoot: undergrowth, mud and water all slow the raptor down.
    in_mud = in_water = false;
    for (const Obstacle& o : c.obstacles) {
        if ((o.kind != ObKind::Mud && o.kind != ObKind::Creek) || std::abs(o.idx - near.i) > 12) continue;
        if (grounded && in_band(o, pos)) (o.kind == ObKind::Mud ? in_mud : in_water) = true;
    }
    float off = near.dist - near.half;  // > 0 in the undergrowth
    rough = off > 0.3f;
    float target = sprinting ? T.sprint_speed : T.run_speed;
    if (off > 0.3f) target = std::min(target, lerpf(T.run_speed, T.rough_speed, smooth(0.3f, 2.0f, off)));
    target *= 1.0f - 0.18f * duck;
    if (in_mud) target = std::min(target, 4.5f * T.pace);
    if (in_water) target = std::min(target, 3.0f * T.pace);
    if (!racing) target = 0;
    if (stumble > 0) {
        stumble -= dt;
        target = std::min(target, 3.0f);
    }
    if (in.brake) target = 0;
    float rate = speed < target ? T.accel : (in.brake ? T.brake : T.accel * 1.4f);
    if (in_mud || in_water) rate = std::max(rate, 30.0f);  // the mud grabs at once
    if (grounded) speed = approach(speed, target, rate * dt);

    float turn = T.turn_rate * lerpf(1.0f, 0.65f, clampf(speed / T.sprint_speed, 0, 1));
    if (stumble > 0) turn *= 0.4f;
    if (!grounded) turn *= 0.5f;
    if (racing) yaw += in.steer * turn * dt;

    Vector3 prev = pos;
    Vector3 fwd = heading_dir(yaw);
    pos = Vector3Add(pos, Vector3Scale(fwd, speed * dt));

    near = c.nearest(pos, hint);
    hint = near.i;
    // Far into the forest: an invisible fence keeps the raptor near the course.
    if (near.dist > 30.0f) {
        pos = prev;
        speed *= 0.5f;
        near = c.nearest(pos, hint);
    }
    float g = c.ground(pos.x, pos.z, near);

    if (grounded && in.jump && racing && stumble <= 0) {
        vy = T.jump_speed * (in_mud || in_water ? 0.7f : 1.0f);
        grounded = false;
        events |= EV_JUMP;
    }
    if (!grounded) {
        vy -= T.gravity * dt;
        pos.y += vy * dt;
        if (pos.y <= g) {
            pos.y = g;
            grounded = true;
            land = std::min(1.0f, -vy / 8.0f);
            events |= EV_LAND;
            vy = 0;
        }
    } else {
        pos.y = g;
    }
    if (grounded) stride += speed * dt * 0.75f;
    // A foot comes down twice per stride cycle.
    if (grounded && speed > 0.5f && std::floor(stride / kPi) != std::floor(stride_before / kPi)) events |= EV_STEP;
    if (!duck_before && duck > 0.5f) events |= EV_DUCK;

    // Dead trees start to fall as the raptor comes near, and boulders to roll.
    for (int k = 0; k < (int)c.obstacles.size(); k++) {
        const Obstacle& o = c.obstacles[k];
        if (o.kind == ObKind::Roller && fall_start[k] < 0 && racing && o.idx - near.s < o.trigger && o.idx > near.s) {
            // Sent off to reach the raptor's line just as the raptor does, at the speed it's going.
            float meet = (pos.x - o.c.x) * o.axis.x + (pos.z - o.c.z) * o.axis.z;
            roll_speed[k] = clampf(meet, 6.0f, o.len) * std::max(speed, T.run_speed) / (o.idx - near.s);
            fall_start[k] = clock;
            events |= EV_RUMBLE;
        }
        if (o.kind != ObKind::Faller) continue;
        float before = fall_angle(k);
        if (fall_start[k] < 0 && racing && o.idx - near.s < o.trigger && o.idx > near.s) {
            fall_time[k] = (o.idx - near.s) / std::max(speed, T.run_speed) + kFallLate / T.pace;
            fall_start[k] = clock;
            events |= EV_CREAK;
        }
        if (before < kPi * 0.5f && fall_angle(k) >= kPi * 0.5f) {
            events |= EV_CRASH;
            float d = Vector3Distance(pos, o.c);
            if (d < 25.0f) shake = std::max(shake, 0.5f * (1.0f - d / 25.0f));
        }
    }

    // Collisions. A stumble costs most of the raptor's speed and a moment of control.
    auto hit = [&](int which) {
        if (which == last_hit) return;
        last_hit = which;
        events |= EV_HIT;
        ObKind kind = c.obstacles[which].kind;
        last_hit_what = kind == ObKind::Log || kind == ObKind::Faller || kind == ObKind::Jam || kind == ObKind::Gate || kind == ObKind::Ribs ? "log"
                      : kind == ObKind::Rock || kind == ObKind::Roller ? "rock" : kind == ObKind::Creek ? "water" : kind == ObKind::Sinkhole ? "mud" : "branch";
        if (kind == ObKind::Creek) events |= EV_SPLASH;
        hits++;
        stumble = 0.75f;
        shake = 1.0f;
        speed *= 0.2f;
        if (kind != ObKind::Creek && kind != ObKind::Sinkhole) pos = Vector3Subtract(prev, Vector3Scale(fwd, 0.25f));  // (in a creek: wade on through)
        pos.y = std::max(pos.y, c.ground(pos.x, pos.z, near));
    };
    float head = pos.y + eye_height() + 0.12f;
    for (int k = 0; k < (int)c.obstacles.size(); k++) {
        const Obstacle& o = c.obstacles[k];
        if (std::abs(o.idx - near.i) > 25) continue;
        Vector3 d = Vector3Subtract(pos, o.c);
        switch (o.kind) {
            case ObKind::Rock: {
                float r = o.r * 0.95f + T.body_r;
                if (d.x * d.x + d.z * d.z < r * r && pos.y < o.c.y + o.r * 1.4f) hit(k);
                break;
            }
            case ObKind::Creek:
                if (grounded && in_band(o, pos)) hit(k);
                break;
            case ObKind::Mud: break;
            case ObKind::Bramble:
                if (pos.y < near.y + kBrambleTop && in_band(o, pos)) hit(k);
                break;
            case ObKind::Jam: {
                float across = d.x * o.axis.x + d.z * o.axis.z, along = d.x * -o.axis.z + d.z * o.axis.x;
                if (std::fabs(along) > o.r + T.body_r || std::fabs(across) > o.len * 0.5f) break;
                if (std::fabs(across - o.gap) + T.body_r > o.gap_w && pos.y < near.y + kJamTop) hit(k);
                break;
            }
            case ObKind::Gate: {
                float along = d.x * o.axis.x + d.z * o.axis.z, across = d.x * -o.axis.z + d.z * o.axis.x;
                if (std::fabs(along) > o.len * 0.5f || std::fabs(across) > o.r + T.body_r) break;
                if (pos.y < o.c.y + o.r - 0.08f || head > near.y + o.gap) hit(k);
                break;
            }
            case ObKind::Roller: {
                Vector3 b = c.roller_at(o, roll(o, k));
                float dx = pos.x - b.x, dz = pos.z - b.z, r = o.r * 0.92f + T.body_r;
                if (dx * dx + dz * dz < r * r && pos.y < b.y + o.r) hit(k);
                break;
            }
            case ObKind::Sinkhole: {
                if (!grounded || k == last_hit) break;
                float dist = std::sqrt(d.x * d.x + d.z * d.z);
                if (dist > o.r || dist < 1e-3f) break;
                float pull = 5.5f * T.pace * (1.0f - dist / o.r) * dt / dist;
                pos.x -= d.x * pull, pos.z -= d.z * pull;
                if (dist < o.len + 0.15f) hit(k);
                break;
            }
            case ObKind::Ribs: {
                float along = d.x * o.axis.x + d.z * o.axis.z, across = d.x * -o.axis.z + d.z * o.axis.x;
                if (std::fabs(along) > o.len * 0.5f) break;
                float out = std::fabs(across);
                if (out < o.r + 1.0f && (out + T.body_r > o.r || head > near.y + kRibsHeadroom)) hit(k);
                break;
            }
            case ObKind::Faller: {
                float th = fall_angle(k);
                if (th <= 0.02f) break;
                float along = d.x * o.axis.x + d.z * o.axis.z;           // out from the base, the way it falls
                float across = d.x * -o.axis.z + d.z * o.axis.x;
                if (std::fabs(across) > o.r + T.body_r || along < 0 || along > o.len * std::sin(th)) break;
                // The trunk's height over this spot; once down it lies half sunk, like a log.
                float h = o.c.y + 0.1f + o.r * 0.85f + along / std::tan(th);
                if (pos.y < h + o.r - 0.08f && head > h - o.r) hit(k);
                break;
            }
            case ObKind::Log:
            case ObKind::Branch: {
                float along = d.x * o.axis.x + d.z * o.axis.z;
                float across = d.x * -o.axis.z + d.z * o.axis.x;
                if (std::fabs(along) > o.len * 0.5f || std::fabs(across) > o.r + T.body_r) break;
                if (o.kind == ObKind::Log) {
                    if (pos.y < o.c.y + o.r - 0.08f) hit(k);
                } else {
                    float bottom = o.c.y - o.r;
                    if (head > bottom && pos.y < o.c.y + o.r) hit(k);
                }
                break;
            }
        }
    }
    for (const Tree& t : c.trees) {
        if (std::abs(t.idx - near.i) > 20) continue;
        float dx = pos.x - t.p.x, dz = pos.z - t.p.z, r = t.r + T.body_r;
        float d2 = dx * dx + dz * dz;
        if (d2 < r * r && d2 > 1e-6f) {
            float d = std::sqrt(d2);
            // Glancing blows slide off; running straight into a trunk is a stumble.
            float head_on = -(dx * fwd.x + dz * fwd.z) / d;
            pos.x = t.p.x + dx / d * r;
            pos.z = t.p.z + dz / d * r;
            if (head_on > 0.6f && speed > 5.0f) {
                last_hit_what = "tree";
                events |= EV_HIT;
                hits++;
                stumble = 0.75f;
                shake = 1.0f;
                speed *= 0.2f;
            } else {
                speed *= 1.0f - 0.6f * dt * 10.0f * std::max(0.0f, head_on);
            }
        }
    }

    if (racing && !finished) {
        time += dt;
        if (next_check < (int)c.checkpoints.size() && near.i >= c.checkpoints[next_check]) {
            splits.push_back(time);
            next_check++;
            events |= EV_CHECKPOINT;
            if (next_check == (int)c.checkpoints.size()) finished = true, events |= EV_FINISH;
        }
    }
}

Controls autopilot(const Player& p, const Course& c) {
    Controls in;
    const Nearest& n = p.near;
    float lateral = 0;  // where across the trail to aim
    float look = 9.0f + p.speed / p.tune.pace * 0.4f;
    bool swerving = false, mud_ahead = false;
    float nearest_rock = 1e9f;
    // Jump so the peak of the leap comes over the obstacle (about a third of a second ahead).
    // (All of this as if on easy: a harder race covers the same ground, just sooner.)
    float v = p.speed / p.tune.pace;
    float reach = v * 0.30f + 0.3f;
    float mud_edge = 1e9f;
    const Obstacle* lane = nullptr;  // a ribcage: run down its middle
    float lane_ahead = 0;
    float weave_ahead = 1e9f;  // the next thing that needs steering round (not to be sprinted at)
    int last_log = -100;
    auto keep_to = [&](float ahead, float offset) {  // steer for `offset` across the trail, as for the nearest rock
        if (ahead > -1.0f && ahead < 30 && ahead < nearest_rock) nearest_rock = ahead, lateral = offset, swerving = true;
        if (ahead > -1.0f) weave_ahead = std::min(weave_ahead, ahead);
    };
    for (int k = 0; k < (int)c.obstacles.size(); k++) {
        const Obstacle& o = c.obstacles[k];
        float ahead = o.idx - n.s;
        if (ahead < -12 || ahead > 120) continue;
        switch (o.kind) {
            case ObKind::Rock:
                // Swerve for the nearest rock still ahead.
                if (ahead > -1.0f) weave_ahead = std::min(weave_ahead, ahead);
                if (ahead > -1.0f && ahead < 30 && ahead < nearest_rock) {
                    nearest_rock = ahead;
                    const PathPt& q = c.pts[o.idx];
                    Vector3 right = heading_right(q.yaw);
                    float side = (o.c.x - q.p.x) * right.x + (o.c.z - q.p.z) * right.z;
                    lateral = side > 0 ? -q.half * 0.65f : q.half * 0.65f;
                    swerving = true;
                }
                break;
            case ObKind::Log:
                if (ahead > 0 && ahead < reach) in.jump = true;
                if (ahead > 0 && o.idx - last_log < 17) weave_ahead = std::min(weave_ahead, ahead - 13);  // (no room to land at a sprint)
                last_log = o.idx;
                break;
            case ObKind::Bramble: {
                const PathPt& q = c.pts[o.idx];
                Vector3 right = heading_right(q.yaw);
                float side = (o.c.x - q.p.x) * right.x + (o.c.z - q.p.z) * right.z;
                keep_to(ahead, side > 0 ? -q.half * 0.6f : q.half * 0.6f);
                break;
            }
            case ObKind::Jam:
                keep_to(ahead, o.gap);
                break;
            case ObKind::Gate:
                if (ahead > 0 && ahead < reach) in.jump = true;
                if (ahead < 6 && ahead > -1.5f) in.duck = true;
                break;
            case ObKind::Roller: {
                if (ahead > 0 && ahead < 9.0f + v * v / 36.0f && p.roll(o, k) * o.len < o.len - 1.2f) in.brake = true;  // let it cross
                if (ahead > 0 && ahead < 50) weave_ahead = std::min(weave_ahead, ahead);
                break;
            }
            case ObKind::Sinkhole: {
                const PathPt& q = c.pts[o.idx];
                Vector3 right = heading_right(q.yaw);
                float side = (o.c.x - q.p.x) * right.x + (o.c.z - q.p.z) * right.z;
                keep_to(ahead, side > 0 ? -q.half * 0.75f : q.half * 0.75f);
                break;
            }
            case ObKind::Ribs:
                if (ahead < 24 + o.len * 0.5f && ahead > -o.len * 0.5f - 0.5f) lane = &o, lane_ahead = ahead - o.len * 0.5f;
                if (ahead < o.len * 0.5f + 7 && ahead > -o.len * 0.5f - 1.5f) in.duck = true;
                break;
            case ObKind::Faller: {
                float th = p.fall_angle(k);
                if (th < kPi * 0.5f && ahead > 0 && ahead < 9.0f + v * v / 36.0f) in.brake = true;  // let it come down first
                if (ahead > 0 && ahead < 50) weave_ahead = std::min(weave_ahead, ahead);
                if (th >= kPi * 0.5f && ahead > 0 && ahead < reach) in.jump = true;
                if (ahead > 0 && o.idx - last_log < 17) weave_ahead = std::min(weave_ahead, ahead - 13);
                last_log = o.idx;
                break;
            }
            case ObKind::Branch:
                if (ahead < 7 && ahead > -1.5f) in.duck = true;
                break;
            case ObKind::Creek: {
                float edge = ahead - o.r;  // to the near bank
                if (edge > -0.5f && edge < 1.2f + v * 0.04f) in.jump = true;
                break;
            }
            case ObKind::Mud: {
                float edge = ahead - o.r;
                if (edge > 0 && edge < 32) mud_ahead = true, mud_edge = std::min(mud_edge, edge);
                if (edge > -0.5f && edge < 0.9f) in.jump = true;
                break;
            }
        }
    }
    if (swerving) look = 6.0f;
    int ti = std::min((int)c.pts.size() - 1, n.i + (int)look);
    const PathPt& q = c.pts[ti];
    Vector3 aim = Vector3Add(q.p, Vector3Scale(heading_right(q.yaw), lateral));
    if (lane && lane_ahead < nearest_rock) {
        Vector3 d = Vector3Subtract(p.pos, lane->c);
        float along = d.x * lane->axis.x + d.z * lane->axis.z;
        aim = Vector3Add(lane->c, Vector3Scale(lane->axis, along + 7.0f));
        swerving = true;
    }
    float want = std::atan2(aim.x - p.pos.x, -(aim.z - p.pos.z));
    in.steer = clampf(angle_diff(p.yaw, want) * 3.0f, -1, 1);
    // Sprint when there's breath to spare, keeping some back for mud; never through a slalom
    // (too fast to weave) unless the mud comes first.
    in.sprint = mud_ahead ? !p.winded : (p.breath > 0.6f || (p.breath > 0.35f && !p.winded && v > 16));
    if (swerving && nearest_rock < mud_edge) in.sprint = false;
    if (weave_ahead < 45) in.sprint = false;
    // Nor into a bend: too fast to hold the trail, above all in the air.
    int bend = std::min((int)c.pts.size() - 1, n.i + 30);
    if (std::fabs(angle_diff(c.pts[n.i].yaw, c.pts[bend].yaw)) > 0.55f) in.sprint = false;
    return in;
}

}  // namespace rr
