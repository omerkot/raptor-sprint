#include "audio.hpp"

#include <atomic>

namespace rr {

Audio audio;

namespace {

constexpr int SR = 44100;
constexpr int VARIANTS = 4;  // footsteps and breaths come in a few takes, so repeats don't drone

struct Noise {
    uint32_t s;
    float operator()() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (s & 0xffffff) / 8388607.5f - 1.0f;
    }
};

struct OnePole {  // low-pass; high-pass is the input minus this
    float a = 0, y = 0;
    void set(float fc) { a = 1.0f - std::exp(-kTau * fc / SR); }
    float lp(float x) { return y += a * (x - y); }
    float hp(float x) { return x - lp(x); }
};

struct Band {  // a band-pass filter (constant peak gain)
    float b0 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void set(float fc, float q) {
        float w = kTau * clampf(fc, 20, SR * 0.45f) / SR, al = std::sin(w) / (2 * q), a0 = 1 + al;
        b0 = al / a0, b2 = -al / a0, a1 = -2 * std::cos(w) / a0, a2 = (1 - al) / a0;
    }
    float run(float x) {
        float y = b0 * x + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1, x1 = x, y2 = y1, y1 = y;
        return y;
    }
};

using Buf = std::vector<float>;
Buf buf(float seconds) { return Buf((size_t)(seconds * SR), 0.0f); }
float tsec(size_t i) { return (float)i / SR; }

void normalize(Buf& b, float peak) {
    float m = 1e-6f;
    for (float v : b) m = std::max(m, std::fabs(v));
    for (float& v : b) v *= peak / m;
    // Short fades at both ends: no clicks.
    size_t f = std::min(b.size() / 4, (size_t)(SR * 0.003f));
    for (size_t i = 0; i < f; i++) b[i] *= (float)i / f, b[b.size() - 1 - i] *= (float)i / f;
}

// A dropping sine: the body of a footfall or an impact.
void thump(Buf& b, float f0, float f1, float decay, float amp, float at = 0) {
    float ph = 0;
    for (size_t i = (size_t)(at * SR); i < b.size(); i++) {
        float t = tsec(i) - at;
        float f = f1 + (f0 - f1) * std::exp(-t * 30.0f);
        ph += kTau * f / SR;
        b[i] += std::sin(ph) * amp * std::exp(-t * decay);
    }
}

// Filtered noise with an exponential decay: crunch, crack, rustle.
void burst(Buf& b, Noise& n, float fc, float q, float decay, float amp, float at = 0, float attack = 0.001f) {
    Band bp;
    bp.set(fc, q);
    for (size_t i = (size_t)(at * SR); i < b.size(); i++) {
        float t = tsec(i) - at;
        float env = std::min(1.0f, t / attack) * std::exp(-t * decay);
        b[i] += bp.run(n()) * amp * env;
    }
}

// Ringing modes of a struck object (wood, stone).
void modes(Buf& b, std::initializer_list<float> f, std::initializer_list<float> d, std::initializer_list<float> a, float at = 0) {
    auto fi = f.begin();
    auto di = d.begin();
    auto ai = a.begin();
    for (; fi != f.end(); ++fi, ++di, ++ai)
        for (size_t i = (size_t)(at * SR); i < b.size(); i++) {
            float t = tsec(i) - at;
            b[i] += std::sin(kTau * *fi * t) * *ai * std::exp(-t * *di);
        }
}

Buf step_dirt(int v) {
    Noise n{(uint32_t)(101 + v * 7919)};
    Buf b = buf(0.17f);
    thump(b, 105 + v * 6, 52 + v * 3, 26, 0.9f);
    burst(b, n, 1700 + v * 250, 0.8f, 55, 1.6f);
    for (int k = 0; k < 4; k++) burst(b, n, 3200, 2.0f, 180, 0.9f, 0.004f + 0.009f * k + 0.003f * v);  // grit
    normalize(b, 0.9f);
    return b;
}

Buf step_leaf(int v) {
    Noise n{(uint32_t)(303 + v * 104729)};
    Buf b = buf(0.28f);
    OnePole hp;
    hp.set(1300);
    float hold = 0;
    for (size_t i = 0; i < b.size(); i++) {
        float t = tsec(i);
        if (i % (SR / 90) == 0) hold = 0.35f + 0.65f * std::fabs(n());  // leaves flutter
        float env = std::min(1.0f, t / 0.012f) * std::exp(-t * 11.0f);
        b[i] += hp.hp(n()) * env * hold;
    }
    thump(b, 90, 50, 30, 0.35f);
    normalize(b, 0.8f);
    return b;
}

Buf whoosh(float dur, float f0, float f1, float amp, uint32_t seed) {
    Noise n{seed};
    Buf b = buf(dur);
    Band bp;
    for (size_t i = 0; i < b.size(); i++) {
        float t = tsec(i) / dur;
        if (i % 64 == 0) bp.set(f0 + (f1 - f0) * std::sin(t * kPi), 1.4f);
        b[i] += bp.run(n()) * amp * std::pow(std::sin(t * kPi), 1.5f);
    }
    return b;
}

// The raptor's voice: a buzzy source through two formants, gliding down.
void voice(Buf& b, Noise& n, float f0, float f1, float dur, float amp, float rasp, float at = 0) {
    Band fa, fb;
    fa.set(900, 3.0f);
    fb.set(2300, 4.0f);
    float ph = 0;
    for (size_t i = (size_t)(at * SR); i < b.size() && tsec(i) - at < dur; i++) {
        float t = tsec(i) - at, k = t / dur;
        float f = f0 * std::pow(f1 / f0, k) * (1.0f + 0.04f * std::sin(kTau * 24.0f * t));
        ph += f / SR;
        float saw = 2.0f * (ph - std::floor(ph)) - 1.0f;
        float src = saw * (1.0f - rasp) + n() * rasp * (0.5f + 0.5f * saw);
        float env = std::min(1.0f, t / 0.015f) * (k < 0.7f ? 1.0f : (1.0f - k) / 0.3f);
        b[i] += (fa.run(src) * 1.0f + fb.run(src) * 0.6f) * amp * env;
    }
}

Buf yelp() {
    Noise n{777};
    Buf b = buf(0.42f);
    voice(b, n, 1150, 520, 0.38f, 1.0f, 0.35f);
    normalize(b, 0.85f);
    return b;
}

Buf breath(int v) {
    Noise n{(uint32_t)(909 + v * 31)};
    float dur = 0.26f + 0.03f * v;
    Buf b = buf(dur);
    Band bp;
    for (size_t i = 0; i < b.size(); i++) {
        float t = tsec(i), k = t / dur;
        if (i % 64 == 0) bp.set(1350 - 500 * k + 60 * v, 1.3f);
        float env = std::min(1.0f, t / 0.035f) * (1.0f - k) * (1.0f - k);
        b[i] += bp.run(n()) * env;
    }
    normalize(b, 0.6f);
    return b;
}

// A soft wooden mallet note (checkpoints, countdown, menus).
void mallet(Buf& b, float f, float at, float amp, float decay) {
    modes(b, {f, f * 3.93f, f * 9.1f}, {decay, decay * 3.0f, decay * 6.0f}, {amp, amp * 0.25f, amp * 0.07f}, at);
}

Buf make(Sfx s, int v) {
    Noise n{(uint32_t)(5000 + (int)s * 97 + v)};
    Buf b;
    switch (s) {
        case Sfx::StepDirt: return step_dirt(v);
        case Sfx::StepLeaf: return step_leaf(v);
        case Sfx::Breath: return breath(v);
        case Sfx::Jump: {
            b = whoosh(0.42f, 380, 1500, 0.9f, 4242);
            thump(b, 110, 60, 35, 0.5f);
            Buf g = buf(0.42f);
            voice(g, n, 260, 190, 0.14f, 0.35f, 0.6f);
            for (size_t i = 0; i < b.size(); i++) b[i] += g[i];
            normalize(b, 0.8f);
            return b;
        }
        case Sfx::Land:
            b = buf(0.3f);
            thump(b, 95, 42, 16, 1.0f);
            burst(b, n, 1500, 0.7f, 30, 1.8f);
            burst(b, n, 3000, 1.5f, 60, 1.0f, 0.01f);
            normalize(b, 0.95f);
            return b;
        case Sfx::Duck:
            b = whoosh(0.22f, 900, 450, 1.0f, 5151);
            normalize(b, 0.5f);
            return b;
        case Sfx::HitLog:  // hollow wood
            b = buf(0.6f);
            thump(b, 90, 55, 14, 1.0f);
            modes(b, {196, 422, 711, 1030}, {14, 20, 28, 40}, {0.6f, 0.4f, 0.25f, 0.15f});
            burst(b, n, 1200, 0.8f, 40, 1.2f);
            normalize(b, 0.95f);
            return b;
        case Sfx::HitRock:  // a hard clack
            b = buf(0.4f);
            thump(b, 80, 45, 22, 0.8f);
            burst(b, n, 3500, 1.0f, 70, 2.0f);
            modes(b, {1240, 1890, 2710}, {35, 45, 60}, {0.4f, 0.3f, 0.2f});
            normalize(b, 0.95f);
            return b;
        case Sfx::HitBranch:  // a crack, then leaves
            b = buf(0.55f);
            for (int k = 0; k < 3; k++) burst(b, n, 2600, 1.5f, 120, 2.5f, 0.012f * k);
            burst(b, n, 4500, 0.7f, 7, 0.35f, 0.03f, 0.03f);
            thump(b, 140, 80, 30, 0.4f);
            normalize(b, 0.9f);
            return b;
        case Sfx::HitTree:
            b = buf(0.5f);
            thump(b, 75, 40, 12, 1.0f);
            modes(b, {150, 331}, {16, 24}, {0.5f, 0.3f});
            burst(b, n, 900, 0.7f, 25, 1.0f);
            normalize(b, 0.95f);
            return b;
        case Sfx::Yelp: return yelp();
        case Sfx::Checkpoint:
            b = buf(0.9f);
            mallet(b, 784, 0, 0.8f, 7);
            mallet(b, 1046.5f, 0.09f, 0.8f, 6);
            normalize(b, 0.7f);
            return b;
        case Sfx::Finish:
            b = buf(2.0f);
            mallet(b, 523.3f, 0.0f, 0.7f, 5);
            mallet(b, 659.3f, 0.11f, 0.7f, 5);
            mallet(b, 784.0f, 0.22f, 0.7f, 4);
            mallet(b, 1046.5f, 0.36f, 0.9f, 2.5f);
            mallet(b, 523.3f, 0.36f, 0.5f, 2.5f);
            normalize(b, 0.8f);
            return b;
        case Sfx::Count:
            b = buf(0.25f);
            modes(b, {1150, 2450}, {38, 60}, {0.8f, 0.3f});
            normalize(b, 0.6f);
            return b;
        case Sfx::Go:
            b = buf(0.6f);
            modes(b, {1540, 3080, 4620}, {9, 16, 25}, {0.8f, 0.3f, 0.12f});
            normalize(b, 0.7f);
            return b;
        case Sfx::MenuMove:
            b = buf(0.05f);
            modes(b, {2400}, {110}, {1.0f});
            normalize(b, 0.35f);
            return b;
        case Sfx::MenuOk:
            b = buf(0.25f);
            mallet(b, 880, 0, 0.8f, 18);
            mallet(b, 1318.5f, 0.06f, 0.8f, 14);
            normalize(b, 0.55f);
            return b;
        case Sfx::MenuBack:
            b = buf(0.25f);
            mallet(b, 698.5f, 0, 0.8f, 18);
            mallet(b, 523.3f, 0.06f, 0.8f, 14);
            normalize(b, 0.5f);
            return b;
        case Sfx::Splash: {  // a body into water: a broad burst, then droplets
            b = buf(0.8f);
            thump(b, 90, 50, 14, 0.6f);
            burst(b, n, 1400, 0.5f, 9, 1.6f, 0, 0.004f);
            burst(b, n, 3800, 0.8f, 14, 0.8f, 0.02f, 0.01f);
            for (int k = 0; k < 9; k++) {  // droplets: little rising blips
                float at = 0.08f + 0.06f * k + 0.03f * std::fabs(n()), f = 900 + 900 * std::fabs(n());
                float ph = 0;
                for (size_t i = (size_t)(at * SR); i < b.size() && tsec(i) - at < 0.05f; i++) {
                    float t = tsec(i) - at;
                    ph += kTau * f * (1.0f + t * 12.0f) / SR;
                    b[i] += std::sin(ph) * 0.25f * std::exp(-t * 70.0f);
                }
            }
            normalize(b, 0.9f);
            return b;
        }
        case Sfx::Squelch: {  // a foot pulling out of mud
            b = buf(0.24f);
            thump(b, 70 + v * 5, 45, 22, 0.8f);
            Band bp;
            for (size_t i = 0; i < b.size(); i++) {
                float t = tsec(i);
                if (i % 64 == 0) bp.set(380 + 600 * t / 0.24f + 40 * v, 4.0f);  // a wet, rising suck
                b[i] += bp.run(n()) * 2.2f * std::min(1.0f, t / 0.03f) * std::exp(-t * 9.0f) * (0.6f + 0.4f * std::sin(t * 140.0f));
            }
            normalize(b, 0.8f);
            return b;
        }
        case Sfx::Creak: {  // dead wood giving way: a slow, grinding groan
            b = buf(1.0f);
            Band fa, fb;
            fa.set(420, 6.0f);
            fb.set(1100, 8.0f);
            float ph = 0;
            for (size_t i = 0; i < b.size(); i++) {
                float t = tsec(i);
                float f = 55 + 25 * t + 6 * std::sin(t * 23.0f);
                ph += f / SR;
                float pulse = (ph - std::floor(ph)) < 0.08f ? 1.0f : 0.0f;  // stick-slip clicks
                float src = pulse + n() * 0.05f;
                float env = std::min(1.0f, t / 0.15f) * (t > 0.8f ? (1.0f - t) / 0.2f : 1.0f);
                b[i] += (fa.run(src) + fb.run(src) * 0.5f) * env;
            }
            for (int k = 0; k < 3; k++) burst(b, n, 2400, 1.5f, 90, 0.8f, 0.55f + 0.08f * k);
            normalize(b, 0.8f);
            return b;
        }
        case Sfx::Crash:  // the trunk hits the ground
            b = buf(1.4f);
            thump(b, 70, 32, 6, 1.0f);
            thump(b, 120, 60, 16, 0.6f, 0.05f);
            burst(b, n, 700, 0.6f, 7, 1.4f);
            burst(b, n, 2600, 1.0f, 30, 1.0f);
            burst(b, n, 5000, 0.7f, 5, 0.25f, 0.1f, 0.05f);  // leaves and twigs settling
            normalize(b, 0.95f);
            return b;
        case Sfx::Clear: {  // an obstacle cleared clean: air rushing past, then two bright notes
            b = whoosh(0.2f, 1800, 5200, 0.5f, 4242);
            b.resize((size_t)(0.6f * SR), 0.0f);
            mallet(b, 1046.5f, 0.03f, 0.7f, 9);
            mallet(b, 1568.0f, 0.085f, 0.8f, 7);
            normalize(b, 0.6f);
            return b;
        }
        case Sfx::Call: {  // the raptor's cry: a rising chirp into a long rasping screech, a growl under it
            b = buf(1.35f);
            voice(b, n, 520, 1250, 0.22f, 0.8f, 0.4f);
            voice(b, n, 1250, 380, 0.9f, 1.0f, 0.55f, 0.2f);
            voice(b, n, 150, 105, 0.95f, 0.45f, 0.7f, 0.15f);
            normalize(b, 0.9f);
            return b;
        }
        case Sfx::Shake: {  // a body shaken hard: a quick flutter of slaps
            b = buf(1.3f);
            for (int k = 0; k < 20; k++)
                burst(b, n, 1200 + 350 * (k % 3), 1.1f, 45, std::sin(kPi * (k + 0.5f) / 20), k * 0.058f, 0.004f);
            normalize(b, 0.7f);
            return b;
        }
        case Sfx::Recover: {  // second wind: a long deep breath in, then a short easy one out
            b = buf(0.95f);
            Band in, out;
            for (size_t i = 0; i < b.size(); i++) {
                float t = tsec(i);
                if (i % 64 == 0) in.set(520 + 1250 * std::min(1.0f, t / 0.5f), 1.1f), out.set(1050 - 500 * clampf((t - 0.58f) / 0.35f, 0, 1), 0.9f);
                float take = std::pow(std::sin(kPi * clampf(t / 0.56f, 0, 1)), 0.7f);
                float give = std::pow(std::sin(kPi * clampf((t - 0.56f) / 0.38f, 0, 1)), 1.4f);
                float x = n();
                b[i] += in.run(x) * take + out.run(x) * give * 0.55f;
            }
            normalize(b, 0.8f);
            return b;
        }
        case Sfx::Rumble: {  // a boulder breaking loose: a deep grinding roll, with stones knocking
            b = buf(1.8f);
            Band low;
            low.set(85, 0.7f);
            for (size_t i = 0; i < b.size(); i++) {
                float t = tsec(i), env = std::min(1.0f, t / 0.25f) * (1.0f - t / 1.8f);
                b[i] += low.run(n()) * env * (0.7f + 0.3f * std::sin(t * 31.0f));
            }
            for (int k = 0; k < 9; k++) burst(b, n, 260 + 90 * (k % 3), 1.4f, 26, 0.5f, 0.1f + k * 0.17f, 0.002f);
            normalize(b, 0.9f);
            return b;
        }
        case Sfx::Count_: break;
    }
    return buf(0.01f);
}

bool varied(Sfx s) { return s == Sfx::StepDirt || s == Sfx::StepLeaf || s == Sfx::Breath || s == Sfx::Squelch; }

Sound g_sounds[(int)Sfx::Count_][VARIANTS];
int g_next[(int)Sfx::Count_] = {};

// ---------------------------------------------------------------- the forest (audio thread)

std::atomic<float> g_speed{0}, g_level{0};
AudioStream g_stream{};

struct Forest {
    Noise n{424242};
    OnePole wind, wind2, leaves, rush_lo, rush_hi;
    float gust = 0.5f, gust_target = 0.5f, gust_t = 0;
    float level = 0, speed = 0;
    // One bird at a time: a call made of a few notes.
    int bird_notes = 0;
    float bird_t = 0, bird_next = 2.0f, bird_f = 3000, bird_amp = 0.05f, bird_kind = 0, bird_ph = 0;
    float insect_ph = 0, insect_am = 0;

    Forest() {
        wind.set(260);
        wind2.set(60);
        leaves.set(2400);
        rush_lo.set(500);
        rush_hi.set(2600);
    }

    float sample() {
        const float dt = 1.0f / SR;
        level += (g_level.load(std::memory_order_relaxed) - level) * 0.0005f;
        speed += (g_speed.load(std::memory_order_relaxed) - speed) * 0.0003f;
        // Wind in the canopy, in gusts.
        gust_t -= dt;
        if (gust_t <= 0) gust_target = 0.2f + 0.8f * std::fabs(n()), gust_t = 1.5f + 3.0f * std::fabs(n());
        gust += (gust_target - gust) * 0.00004f;
        float w = wind.lp(n()) * 1.4f + wind2.lp(n()) * 2.5f;
        float l = leaves.hp(n()) * 0.05f * gust * gust;
        float out = w * (0.10f + 0.12f * gust) + l;
        // Air rushing past the head, rising with speed.
        float s = clampf(speed / 20.0f, 0, 1.2f);
        float r = n();
        out += (rush_lo.lp(r) * 0.9f + (r - rush_hi.lp(r)) * 0.025f) * s * s * 0.5f;
        // Birds.
        bird_next -= dt;
        if (bird_notes == 0 && bird_next <= 0) {
            bird_kind = std::fabs(n());
            bird_notes = 2 + (int)(std::fabs(n()) * 4);
            bird_f = 2200 + std::fabs(n()) * 2600;
            bird_amp = 0.015f + 0.035f * std::fabs(n());
            bird_t = 0;
            bird_next = 2.0f + 5.0f * std::fabs(n());
        }
        if (bird_notes > 0) {
            float len = bird_kind < 0.5f ? 0.16f : 0.07f, gap = bird_kind < 0.5f ? 0.09f : 0.04f;
            if (bird_t < len) {
                float k = bird_t / len;
                float f = bird_kind < 0.5f ? bird_f * (1.0f + 0.25f * std::sin(k * kPi)) : bird_f * (1.0f - 0.3f * k);
                bird_ph += f / SR;
                out += std::sin(kTau * bird_ph) * bird_amp * std::sin(k * kPi);
            }
            bird_t += dt;
            if (bird_t >= len + gap) bird_t = 0, bird_notes--;
        }
        // Insects: a faint, pulsing buzz.
        insect_ph += 5200.0f / SR;
        insect_am += 38.0f / SR;
        out += std::sin(kTau * insect_ph) * 0.004f * (0.5f + 0.5f * std::sin(kTau * insect_am)) * (0.6f + 0.4f * std::sin(insect_am * 0.05f));
        return out * level;
    }
};

Forest* g_forest = nullptr;

void forest_callback(void* buffer, unsigned int frames) {
    float* out = (float*)buffer;
    for (unsigned i = 0; i < frames; i++) out[i] = clampf(g_forest->sample(), -1, 1);
}

const char* name_of(Sfx s) {
    static const char* names[] = {"step_dirt", "step_leaf", "jump", "land", "duck", "hit_log", "hit_rock", "hit_branch", "hit_tree",
                                  "yelp", "breath", "checkpoint", "finish", "count", "go", "menu_move", "menu_ok", "menu_back",
                                  "splash", "squelch", "creak", "crash", "clear", "call", "shake", "recover", "rumble"};
    return names[(int)s];
}

void write_wav(const Buf& b, const std::string& path) {
    std::vector<short> pcm(b.size());
    for (size_t i = 0; i < b.size(); i++) pcm[i] = (short)(clampf(b[i], -1, 1) * 32767);
    Wave w = {(unsigned)pcm.size(), SR, 16, 1, pcm.data()};
    ExportWave(w, path.c_str());
}

}  // namespace

void Audio::dump(const std::string& dir) {
    for (int s = 0; s < (int)Sfx::Count_; s++) write_wav(make((Sfx)s, 0), dir + "/" + name_of((Sfx)s) + ".wav");
    Forest f;
    for (float speed : {0.0f, 20.0f}) {
        g_level.store(1.0f);
        g_speed.store(speed);
        f.level = 1.0f, f.speed = speed;
        Buf b = buf(12.0f);
        for (float& v : b) v = f.sample();
        write_wav(b, dir + (speed > 0 ? "/forest_sprint.wav" : "/forest_still.wav"));
    }
}

void Audio::init() {
    InitAudioDevice();
    ok = IsAudioDeviceReady();
    if (!ok) return;
    for (int s = 0; s < (int)Sfx::Count_; s++)
        for (int v = 0; v < (varied((Sfx)s) ? VARIANTS : 1); v++) {
            Buf b = make((Sfx)s, v);
            std::vector<short> pcm(b.size());
            for (size_t i = 0; i < b.size(); i++) pcm[i] = (short)(clampf(b[i], -1, 1) * 32767);
            Wave w = {(unsigned)pcm.size(), SR, 16, 1, pcm.data()};
            g_sounds[s][v] = LoadSoundFromWave(w);
        }
    g_forest = new Forest();
    SetAudioStreamBufferSizeDefault(2048);
    g_stream = LoadAudioStream(SR, 32, 1);
    SetAudioStreamCallback(g_stream, forest_callback);
    PlayAudioStream(g_stream);
}

void Audio::play(Sfx s, float volume, float pitch) {
    if (!ok || fx_volume <= 0) return;
    int i = (int)s, v = varied(s) ? (g_next[i]++ % VARIANTS) : 0;
    Sound& snd = g_sounds[i][v];
    SetSoundVolume(snd, clampf(volume * fx_volume, 0, 1));
    SetSoundPitch(snd, pitch);
    PlaySound(snd);
}

void Audio::set_forest(float speed, bool on) {
    if (!ok) return;
    g_speed.store(speed, std::memory_order_relaxed);
    g_level.store(on ? forest_volume : 0.0f, std::memory_order_relaxed);
}

void Audio::unload() {
    if (!ok) return;
    StopAudioStream(g_stream);
    UnloadAudioStream(g_stream);
    for (auto& row : g_sounds)
        for (Sound& s : row)
            if (s.frameCount) UnloadSound(s);
    CloseAudioDevice();
    delete g_forest;
    g_forest = nullptr;
    ok = false;
}

}  // namespace rr
