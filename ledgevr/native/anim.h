// LEDGE 1.1 — animation engine: easings, particles, emotes, menu tween.
// Shared by the VR shell and the mobile port.
#pragma once
#include "mathlib.h"
#include <vector>
#include <cstdint>
#include <cmath>
#include <cstdlib>

namespace lv {

// ---------------- easing ----------------
static inline float easeOutCubic(float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    float u = 1 - t;
    return 1 - u * u * u;
}
static inline float easeInCubic(float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * t;
}
static inline float easeOutBack(float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    float c1 = 1.70158f, c3 = c1 + 1;
    float u = t - 1;
    return 1 + c3 * u * u * u + c1 * u * u;
}
static inline float easeOutElastic(float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    if (t == 0 || t == 1) return t;
    float c4 = 6.2831853f / 3.0f;
    return powf(2, -10 * t) * sinf((t * 10 - 0.75f) * c4) + 1;
}
static inline float easeInOutQuad(float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t < 0.5f ? 2 * t * t : 1 - powf(-2 * t + 2, 2) / 2;
}
static inline float bouncePulse(float t, float period) {
    // 1 -> decaying sine bounce, settles at 1
    if (t <= 0) return 1;
    float e = expf(-t * 3.2f);
    return 1 + sinf(t * 6.2831853f / period) * e * 0.5f;
}

// ---------------- particles ----------------
// GPU-cheap: small rounded boxes, shrink out over life. Max 400 live.
struct Particle {
    vec3 pos, vel;
    vec3 color;
    float size;
    float life = 0, maxLife = 1;
    float spin = 0;          // rad/s around Y
    float gravity = -3.5f;
};

class ParticleSystem {
public:
    // fx kinds (matches GameWorld::onFx)
    static const int FX_PLACE = 0, FX_DELETE = 1, FX_LAND = 2, FX_GOAL = 3, FX_EMOTE = 4;
    std::vector<Particle> parts;

    void spawn(int fx, const vec3& pos, const vec3& color) {
        switch (fx) {
        case FX_PLACE: burst(pos + vec3(0, 0.4f, 0), 10, color, 0.09f, 2.2f, 0.45f); break;
        case FX_DELETE: burst(pos, 16, color, 0.12f, 3.6f, 0.5f); break;
        case FX_LAND: ring(pos + vec3(0, 0.06f, 0), 10, vec3(0.92f, 0.94f, 0.98f), 0.07f, 0.4f); break;
        case FX_GOAL: fountain(pos + vec3(0, 0.8f, 0), 44, vec3(1.0f, 0.87f, 0.42f), 0.1f, 0.5f); break;
        case FX_EMOTE: burst(pos + vec3(0, 1.1f, 0), 14, color, 0.08f, 2.0f, 0.7f, -1.2f); break;
        }
    }
    void burst(const vec3& pos, int n, const vec3& c, float sz, float speed, float life,
               float grav = -3.5f) {
        for (int i = 0; i < n && (int)parts.size() < 400; i++) {
            Particle p;
            p.pos = pos;
            float a = (float)(rand() % 6283) * 0.001f;
            float up = 0.6f + (float)(rand() % 100) * 0.006f;
            p.vel = vec3(cosf(a), up, sinf(a)) * (speed * (0.4f + (float)(rand() % 100) * 0.008f));
            p.color = c;
            p.size = sz * (0.7f + (float)(rand() % 100) * 0.006f);
            p.maxLife = life * (0.7f + (float)(rand() % 100) * 0.006f);
            p.spin = ((float)(rand() % 100) * 0.03f - 1.5f) * 6.0f;
            p.gravity = grav;
            parts.push_back(p);
        }
    }
    void ring(const vec3& pos, int n, const vec3& c, float sz, float life) {
        for (int i = 0; i < n && (int)parts.size() < 400; i++) {
            Particle p;
            p.pos = pos + vec3(0, 0.02f, 0);
            float a = (float)i / n * 6.2831853f;
            p.vel = vec3(cosf(a) * 1.6f, 0.25f, sinf(a) * 1.6f);
            p.color = c;
            p.size = sz;
            p.maxLife = life;
            p.gravity = -1.5f;
            parts.push_back(p);
        }
    }
    void fountain(const vec3& pos, int n, const vec3& c, float sz, float life) {
        for (int i = 0; i < n && (int)parts.size() < 400; i++) {
            Particle p;
            p.pos = pos;
            float a = (float)(rand() % 6283) * 0.001f;
            p.vel = vec3(cosf(a) * 1.2f, 4.2f + (float)(rand() % 100) * 0.02f, sinf(a) * 1.2f);
            p.color = c;
            p.size = sz;
            p.maxLife = life + (float)(rand() % 100) * 0.004f;
            p.spin = (float)(rand() % 100) * 0.09f;
            parts.push_back(p);
        }
    }
    void update(float dt) {
        for (size_t i = 0; i < parts.size();) {
            Particle& p = parts[i];
            p.life += dt;
            if (p.life >= p.maxLife) { parts[i] = parts.back(); parts.pop_back(); continue; }
            p.vel.y += p.gravity * dt;
            p.pos += p.vel * dt;
            if (p.pos.y < 0.03f && p.vel.y < 0) { p.pos.y = 0.03f; p.vel.y *= -0.35f; }
            i++;
        }
    }
    void clear() { parts.clear(); }
};

// ---------------- emotes ----------------
enum Emote : uint8_t {
    EM_NONE = 0, EM_WAVE = 1, EM_DANCE = 2, EM_TAUNT = 3, EM_COUNT
};
static const char* emoteName(uint8_t e) {
    static const char* names[] = { "None", "Wave", "Dance", "Taunt" };
    return names[e < EM_COUNT ? e : 0];
}
static const float EMOTE_LEN = 1.6f;  // seconds

// per-frame animated offsets for an avatar playing `emote` since time t
struct EmotePose {
    float bobY = 0;       // body vertical offset
    float leanZ = 0;      // body lean (rotZ radians)
    float spinY = 0;      // extra yaw offset (radians)
    float squash = 1;     // uniform squash-stretch factor
    float hatLift = 0;    // hat vertical offset
};

inline EmotePose emotePose(uint8_t emote, float t) {
    EmotePose p;
    if (emote == EM_NONE || t > EMOTE_LEN) return p;
    float ph = t * 6.2831853f;
    switch (emote) {
    case EM_WAVE:
        p.hatLift = fabsf(sinf(ph * 3)) * 0.06f;
        p.leanZ = sinf(ph * 3) * 0.08f;
        break;
    case EM_DANCE:
        p.bobY = fabsf(sinf(ph * 4)) * 0.12f;
        p.squash = 1 + sinf(ph * 4) * 0.06f;
        p.spinY = sinf(ph * 2) * 0.35f;
        break;
    case EM_TAUNT:
        p.bobY = sinf(ph * 2) * 0.05f + 0.05f;
        p.spinY = ph * 2.2f;
        p.hatLift = 0.1f;
        break;
    }
    // settle back to rest at the end
    float fade = 1 - easeInCubic((t - (EMOTE_LEN - 0.35f)) / 0.35f);
    fade = fade < 0 ? 0 : fade;
    p.bobY *= fade; p.leanZ *= fade; p.spinY *= fade;
    p.hatLift *= fade;
    p.squash = 1 + (p.squash - 1) * fade;
    return p;
}

// ---------------- misc helpers ----------------
// nearest palette index for a block color (drives its material texture)
inline int nearestColorIdx(const vec3& c, const vec3* palette, int n) {
    int best = 0;
    float bd = 1e9f;
    for (int i = 0; i < n; i++) {
        vec3 d = c - palette[i];
        float dd = dot(d, d);
        if (dd < bd) { bd = dd; best = i; }
    }
    return best;
}

} // namespace lv
