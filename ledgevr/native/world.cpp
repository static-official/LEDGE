// LEDGE VR — world implementation: physics + GrabVR movement + editor + levels
#include "world.h"
#include "log.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include <algorithm>

namespace lv {

static const float GRAVITY = -9.81f * 1.55f;   // games feel better with stronger g
static const float MAX_FALL = -14.0f;

// ---------------------------------------------------------------------------
// tiny JSON writer/parser (levels + saves)
// ---------------------------------------------------------------------------
std::string jsonEscape(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
        case '"': r += "\\\""; break;
        case '\\': r += "\\\\"; break;
        case '\n': r += "\\n"; break;
        case '\t': r += "\\t"; break;
        default: r += c;
        }
    }
    return r;
}

// Minimal JSON structure scan: emits leaf (path,key,value) triples for numbers,
// strings and bools. Enough for our flat-ish level + save documents.
struct JParser {
    const char* p;
    const char* end;
    std::string path;
    std::function<void(const std::string&, const std::string&, const std::string&)> leaf;

    void skipWs() { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
    std::string readString() {
        std::string r;
        skipWs();
        if (p >= end || *p != '"') return r;
        p++;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                p++;
                switch (*p) {
                case 'n': r += '\n'; break;
                case 't': r += '\t'; break;
                case '"': r += '"'; break;
                case '\\': r += '\\'; break;
                default: r += *p;
                }
                p++;
            } else {
                r += *p++;
            }
        }
        if (p < end) p++;
        return r;
    }
    std::string readPrimitive() {
        skipWs();
        const char* start = p;
        while (p < end && *p != ',' && *p != '}' && *p != ']') p++;
        std::string s(start, p - start);
        // strip quotes handled by readString; here trim spaces
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
    }
    void value(const std::string& key) {
        skipWs();
        if (p >= end) return;
        char c = *p;
        if (c == '{') {
            std::string sub = path + (key.empty() ? "" : (key + "/"));
            std::string saved = path;
            path = sub;
            p++;
            skipWs();
            if (p < end && *p == '}') { p++; path = saved; return; }
            for (;;) {
                std::string k = readString();
                skipWs();
                if (p < end && *p == ':') p++;
                value(k);
                skipWs();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; break; }
                if (p >= end) break;
                p++;
            }
            path = saved;
        } else if (c == '[') {
            std::string sub = path + (key.empty() ? "" : (key + "/"));
            std::string saved = path;
            path = sub;                    // FIX: array elements must carry the key path
            p++;
            int idx = 0;
            for (;;) {
                skipWs();
                if (p < end && *p == ']') { p++; break; }
                char nc = (p < end) ? *p : 0;
                if (nc == '{') {
                    value(std::to_string(idx));   // object element
                } else {
                    std::string v = readPrimitive();
                    if (leaf) leaf(path, "[" + std::to_string(idx) + "]", v);
                }
                idx++;
                skipWs();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; break; }
                if (p >= end) break;
            }
            path = saved;
        } else {
            std::string v = (*p == '"') ? readString() : readPrimitive();  // FIX: strip string quotes
            if (leaf) leaf(path, key, v);
        }
    }
};

bool jsonParse(const std::string& s,
               std::function<void(const std::string&, const std::string&, const std::string&)> onLeaf) {
    JParser jp;
    jp.p = s.c_str();
    jp.end = s.c_str() + s.size();
    jp.leaf = onLeaf;
    jp.value("");
    return true;
}

std::string Level::toJson() const {
    std::string r = "{";
    r += "\"v\":" + std::to_string(version) + ",";
    r += "\"name\":\"" + jsonEscape(name) + "\",";
    r += "\"author\":\"" + jsonEscape(author) + "\",";
    r += "\"blocks\":[";
    bool first = true;
    for (auto& b : blocks) {
        if (!first) r += ",";
        first = false;
        char buf[512];
        snprintf(buf, sizeof(buf),
            "{\"t\":%u,\"px\":%.3f,\"py\":%.3f,\"pz\":%.3f,\"sx\":%.3f,\"sy\":%.3f,\"sz\":%.3f,"
            "\"r\":%.1f,\"c\":[%.3f,%.3f,%.3f]}",
            b.type, b.pos.x, b.pos.y, b.pos.z, b.size.x, b.size.y, b.size.z, b.rotY,
            b.color.x, b.color.y, b.color.z);
        r += buf;
    }
    r += "]}";
    return r;
}

bool Level::fromJson(const std::string& s) {
    blocks.clear();
    Level tmp;
    jsonParse(s, [&](const std::string& path, const std::string& key, const std::string& val) {
        if (path == "" && key == "name") tmp.name = val;
        else if (path == "" && key == "author") tmp.author = val;
        else if (path == "" && key == "v") tmp.version = atoi(val.c_str());
        else if (path.rfind("blocks/", 0) == 0) {
            // path like blocks/0/key
            size_t s1 = path.find('/') + 1;
            size_t s2 = path.find('/', s1);
            std::string idxs = path.substr(s1, s2 - s1);
            int idx = atoi(idxs.c_str());
            while ((int)tmp.blocks.size() <= idx) tmp.blocks.push_back(Block());
            Block& b = tmp.blocks[idx];
            float v = (float)atof(val.c_str());
            if (key == "t") b.type = (uint8_t)atoi(val.c_str());
            else if (key == "px") b.pos.x = v;
            else if (key == "py") b.pos.y = v;
            else if (key == "pz") b.pos.z = v;
            else if (key == "sx") b.size.x = v;
            else if (key == "sy") b.size.y = v;
            else if (key == "sz") b.size.z = v;
            else if (key == "r") b.rotY = v;
            else if (path.size() >= 3 && path.compare(path.size() - 3, 3, "/c/") == 0) {
                // color components arrive as path ".../c/", key "[0]"
                if (key == "[0]") b.color.x = v;
                else if (key == "[1]") b.color.y = v;
                else if (key == "[2]") b.color.z = v;
            }
        }
    });
    // ensure sane sizes
    for (auto& b : tmp.blocks) {
        if (b.size.x <= 0) b.size.x = 1;
        if (b.size.y <= 0) b.size.y = 1;
        if (b.size.z <= 0) b.size.z = 1;
    }
    blocks = tmp.blocks;
    name = tmp.name;
    author = tmp.author;
    version = tmp.version;
    return !blocks.empty();
}

// ---------------------------------------------------------------------------
GameWorld::GameWorld() { loadHub(); }

void GameWorld::loadHub() { level = makeHubLevel(); body.pos = level.spawnPoint(); body.vel = vec3(); }

bool GameWorld::loadLevelJson(const std::string& json) {
    Level tmp;
    if (!tmp.fromJson(json)) return false;
    level = tmp;
    body.pos = level.spawnPoint();
    body.vel = vec3();
    goalHit = false;
    return true;
}

Level makeHubLevel() {
    Level lv;
    lv.name = "LEDGE Hub";
    lv.author = "MoonyDev";
    vec3 pastel(0.95f, 0.78f, 0.62f);
    vec3 grass(0.62f, 0.90f, 0.72f);
    vec3 wood(0.85f, 0.70f, 0.52f);
    // main island
    lv.blocks.push_back({vec3(0, 0, 0), vec3(7, 1, 7), 0, BT_Block, grass});
    // spawn marker (invisible — actually a flat pad)
    lv.blocks.push_back({vec3(0, 0.1, 0), vec3(1.2, 0.1, 1.2), 0, BT_Spawn, vec3(0.95f, 0.62f, 0.68f)});
    // ledges tower (grab path up)
    lv.blocks.push_back({vec3(4.5, 1.5, 0), vec3(0.8, 0.25, 2), 0, BT_Ledge, wood});
    lv.blocks.push_back({vec3(6.5, 2.6, 1), vec3(0.8, 0.25, 2), 45, BT_Ledge, wood});
    lv.blocks.push_back({vec3(8.5, 3.7, -0.5), vec3(0.8, 0.25, 2), 0, BT_Ledge, wood});
    // high platform
    lv.blocks.push_back({vec3(10.5, 4.8, 0), vec3(2.5, 0.4, 2.5), 0, BT_Block, grass});
    // goal on top
    lv.blocks.push_back({vec3(10.5, 5.6, 0), vec3(0.6, 0.8, 0.6), 0, BT_Goal, vec3(1.0f, 0.85f, 0.4f)});
    // floating islands
    lv.blocks.push_back({vec3(-6, 1.2, -4), vec3(3, 0.8, 3), 0, BT_Block, grass});
    lv.blocks.push_back({vec3(-11, 2.4, -7), vec3(2.5, 0.8, 2.5), 15, BT_Block, grass});
    lv.blocks.push_back({vec3(-15.5, 3.6, -9), vec3(2, 0.8, 2), 0, BT_Block, grass});
    // bouncy pad
    lv.blocks.push_back({vec3(-6, 1.9, -4), vec3(1.2, 0.15, 1.2), 0, BT_Bouncy, vec3(0.65f, 0.76f, 0.95f)});
    // spinner
    lv.blocks.push_back({vec3(0, -2.5, 8), vec3(3, 0.3, 1), 0, BT_Spinner, wood});
    // low ring of ledges to practice grabbing
    lv.blocks.push_back({vec3(3, -1.0, 4), vec3(1.5, 0.25, 0.8), 0, BT_Ledge, wood});
    lv.blocks.push_back({vec3(1.5, 0.0, 6), vec3(1.5, 0.25, 0.8), 30, BT_Ledge, wood});
    lv.blocks.push_back({vec3(-2, -2.0, 7), vec3(1.5, 0.25, 0.8), 0, BT_Ledge, wood});
    return lv;
}

std::vector<Level> builtInLevels() {
    std::vector<Level> v;
    // 1. Hub
    v.push_back(makeHubLevel());

    // 2. "First Climb" — intro parkour
    {
        Level lv;
        lv.name = "First Climb";
        lv.author = "MoonyDev";
        vec3 wood(0.85f, 0.70f, 0.52f);
        vec3 stone(0.90f, 0.90f, 0.92f);
        lv.blocks.push_back({vec3(0, 0, 0), vec3(4, 1, 4), 0, BT_Block, vec3(0.62f, 0.90f, 0.72f)});
        lv.blocks.push_back({vec3(0, 0.1, 0), vec3(1, 0.1, 1), 0, BT_Spawn, vec3(0.95f, 0.62f, 0.68f)});
        for (int i = 0; i < 8; i++) {
            float x = 2.2f + i * 2.0f;
            float y = 0.8f + i * 0.9f;
            lv.blocks.push_back({vec3(x, y, (float)(i % 2) * 1.2f - 0.6f), vec3(0.7, 0.2, 1.2), (float)(i * 15), BT_Ledge, wood});
        }
        lv.blocks.push_back({vec3(18.0f, 8.2f, 0.6f), vec3(3, 0.5, 3), 0, BT_Block, stone});
        lv.blocks.push_back({vec3(18.0f, 9.0f, 0.6f), vec3(0.6, 0.8, 0.6), 0, BT_Goal, vec3(1.0f, 0.85f, 0.4f)});
        v.push_back(lv);
    }

    // 3. "Cloud Leap" — bouncy course
    {
        Level lv;
        lv.name = "Cloud Leap";
        lv.author = "MoonyDev";
        vec3 blue(0.65f, 0.76f, 0.95f);
        vec3 cloud(0.92f, 0.94f, 0.98f);
        lv.blocks.push_back({vec3(0, 0, 0), vec3(4, 1, 4), 0, BT_Block, cloud});
        lv.blocks.push_back({vec3(0, 0.1, 0), vec3(1, 0.1, 1), 0, BT_Spawn, vec3(0.95f, 0.62f, 0.68f)});
        for (int i = 1; i <= 6; i++) {
            lv.blocks.push_back({vec3(i * 4.0f, 1.6f + i * 1.4f, (float)((i % 3) - 1) * 2.0f),
                                 vec3(1.6, 0.2, 1.6), 0, BT_Bouncy, blue});
        }
        lv.blocks.push_back({vec3(28, 12, 0), vec3(3, 0.5, 3), 0, BT_Block, cloud});
        lv.blocks.push_back({vec3(28, 12.8f, 0), vec3(0.6, 0.8, 0.6), 0, BT_Goal, vec3(1.0f, 0.85f, 0.4f)});
        // catch ledges between bouncies
        lv.blocks.push_back({vec3(8, 4.4f, 0), vec3(1, 0.2, 1), 0, BT_Ledge, vec3(0.85f, 0.70f, 0.52f)});
        lv.blocks.push_back({vec3(16, 6.5f, 0), vec3(1, 0.2, 1), 0, BT_Ledge, vec3(0.85f, 0.70f, 0.52f)});
        v.push_back(lv);
    }

    // 4. "The Tower" — vertical grab challenge
    {
        Level lv;
        lv.name = "The Tower";
        lv.author = "MoonyDev";
        vec3 wood(0.85f, 0.70f, 0.52f);
        lv.blocks.push_back({vec3(0, 0, 0), vec3(5, 1, 5), 0, BT_Block, vec3(0.62f, 0.90f, 0.72f)});
        lv.blocks.push_back({vec3(0, 0.1, 0), vec3(1, 0.1, 1), 0, BT_Spawn, vec3(0.95f, 0.62f, 0.68f)});
        for (int i = 0; i < 14; i++) {
            float a = i * 0.9f;
            lv.blocks.push_back({vec3(cosf(a) * 2.4f, 0.6f + i * 1.05f, sinf(a) * 2.4f),
                                 vec3(0.6, 0.22, 1.1), a * 57.3f / 1.5f, BT_Ledge, wood});
        }
        lv.blocks.push_back({vec3(0, 15.5f, 0), vec3(2.5, 0.4, 2.5), 0, BT_Block, vec3(0.90f, 0.90f, 0.92f)});
        lv.blocks.push_back({vec3(0, 16.3f, 0), vec3(0.6, 0.8, 0.6), 0, BT_Goal, vec3(1.0f, 0.85f, 0.4f)});
        v.push_back(lv);
    }
    return v;
}

// ---------------------------------------------------------------------------
// physics helpers
// ---------------------------------------------------------------------------
static bool obbContains(const Block& b, const vec3& p, float inflate = 0) {
    // rotate p into block space
    float c = cosf(-b.rotY * 3.14159265f / 180.0f), s = sinf(-b.rotY * 3.14159265f / 180.0f);
    vec3 rel = p - b.pos;
    vec3 lr(rel.x * c - rel.z * s, rel.y, rel.x * s + rel.z * c);
    return fabsf(lr.x) <= b.size.x * 0.5f + inflate &&
           fabsf(lr.y) <= b.size.y * 0.5f + inflate &&
           fabsf(lr.z) <= b.size.z * 0.5f + inflate;
}

static vec3 obbClosest(const Block& b, const vec3& p) {
    float c = cosf(-b.rotY * 3.14159265f / 180.0f), s = sinf(-b.rotY * 3.14159265f / 180.0f);
    vec3 rel = p - b.pos;
    vec3 lr(rel.x * c - rel.z * s, rel.y, rel.x * s + rel.z * c);
    vec3 clamped(std::max(-b.size.x * 0.5f, std::min(b.size.x * 0.5f, lr.x)),
                 std::max(-b.size.y * 0.5f, std::min(b.size.y * 0.5f, lr.y)),
                 std::max(-b.size.z * 0.5f, std::min(b.size.z * 0.5f, lr.z)));
    float c2 = cosf(b.rotY * 3.14159265f / 180.0f), s2 = sinf(b.rotY * 3.14159265f / 180.0f);
    vec3 world(clamped.x * c2 - clamped.z * s2, clamped.y, clamped.x * s2 + clamped.z * c2);
    return b.pos + world;
}

bool GameWorld::handNearWorld(const vec3& hand) const {
    for (auto& b : level.blocks) {
        if (b.type == BT_Spawn) continue;
        vec3 cp = obbClosest(b, hand);
        float d2 = dot(cp - hand, cp - hand);
        if (d2 < 0.23f * 0.23f) return true;
    }
    return false;
}

bool GameWorld::collideBox(const Block& b, vec3& pos, vec3& vel, bool& grounded) const {
    // capsule approximated as sphere at feet + sphere at head against OBB
    bool hit = false;
    for (int sphere = 0; sphere < 2; sphere++) {
        vec3 sc = pos + vec3(0, sphere == 0 ? PlayerBody::Radius : PlayerBody::Height - PlayerBody::Radius, 0);
        vec3 cp = obbClosest(b, sc);
        vec3 dv = sc - cp;
        float d = len(dv);
        if (d < PlayerBody::Radius && d > 1e-6f) {
            vec3 n = dv * (1.0f / d);
            float push = PlayerBody::Radius - d;
            pos += n * push;
            float vn = dot(vel, n);
            if (vn < 0) vel -= n * vn;
            if (n.y > 0.55f) grounded = true;
            if (n.y < -0.55f && vel.y < 0) vel.y = 0;
            hit = true;
        }
    }
    return hit;
}

void GameWorld::collide() {
    for (int iter = 0; iter < 3; iter++) {
        bool grounded = false;
        for (auto& b : level.blocks) {
            if (b.type == BT_Spawn) continue;
            collideBox(b, body.pos, body.vel, grounded);
        }
        onGround = grounded;
        if (!grounded) break;
    }
}

void GameWorld::update(const FrameInput& in, GameSettings& settings, EditorState& ed) {
    float dt = std::min(in.dt, 0.05f);
    worldTime += dt;

    // despawn ghosts shrink out
    for (size_t i = 0; i < despawns.size();) {
        despawns[i].t += dt;
        if (despawns[i].t > 0.28f) { despawns[i] = despawns.back(); despawns.pop_back(); }
        else i++;
    }

    // spinner rotation
    spinnerAngle += dt * 60.0f;
    for (auto& b : level.blocks)
        if (b.type == BT_Spinner) b.rotY = spinnerAngle;

    body.yaw = in.headYaw;

    if (!havePrevHands) {
        prevHandL = in.handL;
        prevHandR = in.handR;
        havePrevHands = true;
    }

    // ---------------- grab movement (GrabVR style) ----------------
    vec3 bodyDelta(0, 0, 0);
    bool anyGrab = false;

    if (!in.inMenu) {
        if (in.grabL > 0.6f && handNearWorld(in.handL)) {
            bodyDelta += (prevHandL - in.handL);
            anyGrab = true;
            if (!grabbingL) { grabbingL = true; if (onSound) onSound(0); }
        } else if (grabbingL) {
            grabbingL = false;
            // fling on release: hand velocity
            vec3 handVel = (in.handL - prevHandL) / std::max(dt, 0.001f);
            if (len(handVel) > 1.0f && len(body.vel) < 9.0f) {
                body.vel += handVel * (-0.35f * settings.grabStrength);
                if (onSound) onSound(1);
            }
        }
        if (in.grabR > 0.6f && handNearWorld(in.handR)) {
            bodyDelta += (prevHandR - in.handR);
            anyGrab = true;
            if (!grabbingR) { grabbingR = true; if (onSound) onSound(0); }
        } else if (grabbingR) {
            grabbingR = false;
            vec3 handVel = (in.handR - prevHandR) / std::max(dt, 0.001f);
            if (len(handVel) > 1.0f && len(body.vel) < 9.0f) {
                body.vel += handVel * (-0.35f * settings.grabStrength);
                if (onSound) onSound(1);
            }
        }
        if (anyGrab) {
            body.pos += bodyDelta;
            // pulling down/up moves body — cancel small vel
            body.vel.y *= 0.55f;
        }
    }

    prevHandL = in.handL;
    prevHandR = in.handR;

    // ---------------- strafe (left stick) ----------------
    if (!in.inMenu && settings.strafeEnabled && len(in.stickL) > 0.15f) {
        float c = cosf(body.yaw), s = sinf(body.yaw);
        // stick forward = -Z in OpenXR head space; map to body yaw
        vec3 fwd(-sinf(body.yaw), 0, -cosf(body.yaw));
        vec3 right(c, 0, s);
        vec3 move = fwd * (-in.stickL.y) + right * in.stickL.x;
        if (len(move) > 0.001f) {
            move = norm(move) * settings.strafeSpeed;
            body.pos.x += move.x * dt;
            body.pos.z += move.z * dt;
        }
    }

    // ---------------- jump (mobile button; VR build passes false) ----------------
    if (in.jumpPressed && onGround) {
        body.vel.y = 6.8f;
        if (onSound) onSound(4);
    }

    // ---------------- gravity ----------------
    body.vel.y += GRAVITY * dt;
    if (body.vel.y < MAX_FALL) body.vel.y = MAX_FALL;

    // damping when not grabbing
    if (!anyGrab) {
        body.vel.x *= (1.0f - 3.0f * dt);
        body.vel.z *= (1.0f - 3.0f * dt);
    }

    vec3 oldPos = body.pos;
    integrate(in, settings);

    // landing detection
    if (!onGround && oldPos.y > body.pos.y) {} // handled after collide
    bool wasAir = !onGround;
    collide();
    if (wasAir && onGround) {
        if (oldPos.y - body.pos.y > 0.4f) {
            if (onSound) onSound(2);
            if (onFx) onFx(2, body.pos, vec3(0.92f, 0.94f, 0.98f));
        }
    }

    // bouncy / goal / void checks
    for (auto& b : level.blocks) {
        if (b.type == BT_Bouncy && obbContains(b, body.pos + vec3(0, PlayerBody::Radius, 0), 0.08f)) {
            body.vel.y = 9.5f;
            if (onSound) onSound(4);
        }
        if (b.type == BT_Goal) {
            vec3 cp = obbClosest(b, bodyCenter());
            if (dot(cp - bodyCenter(), cp - bodyCenter()) < 1.0f && !goalHit) {
                goalHit = true;
                if (onSound) onSound(3);
                if (onFx) onFx(3, b.pos, vec3(1.0f, 0.87f, 0.42f));
            }
        }
    }
    if (body.pos.y < -30) {  // fell out: respawn
        body.pos = level.spawnPoint();
        body.vel = vec3();
    }

    
}

void GameWorld::integrate(const FrameInput& in, const GameSettings& s) {
    (void)in; (void)s;
    body.pos += body.vel * (1.0f / 60.0f); // positional integration happens in collide
    // NOTE: velocity integration uses dt directly for stability:
    // body.pos += body.vel * dt;  (kept at fixed step via caller)
}

// raycast for editor selection
bool GameWorld::raycastBlocks(const vec3& origin, const vec3& dir, float maxDist,
                              int& hitIdx, vec3& hitPos) const {
    float best = maxDist;
    hitIdx = -1;
    for (size_t i = 0; i < level.blocks.size(); i++) {
        const Block& b = level.blocks[i];
        if (b.type == BT_Spawn) continue;
        // slab test in block space
        float c = cosf(-b.rotY * 3.14159265f / 180.0f), s = sinf(-b.rotY * 3.14159265f / 180.0f);
        vec3 rel = origin - b.pos;
        vec3 lo(rel.x * c - rel.z * s, rel.y, rel.x * s + rel.z * c);
        vec3 ld(dir.x * c - dir.z * s, dir.y, dir.x * s + dir.z * c);
        float tmin = 0, tmax = best;
        bool ok = true;
        for (int a = 0; a < 3; a++) {
            float o = (&lo.x)[a], dd = (&ld.x)[a], mn = -(&b.size.x)[a] * 0.5f, mx = (&b.size.x)[a] * 0.5f;
            if (fabsf(dd) < 1e-8f) {
                if (o < mn || o > mx) { ok = false; break; }
            } else {
                float t1 = (mn - o) / dd, t2 = (mx - o) / dd;
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax) { ok = false; break; }
            }
        }
        if (ok && tmin < best) { best = tmin; hitIdx = (int)i; hitPos = origin + dir * tmin; }
    }
    return hitIdx >= 0;
}

void GameWorld::editorAddBlock(const vec3& pos, BlockType t, int colorIdx) {
    Block b;
    b.pos = pos;
    b.type = (uint8_t)t;
    b.size = t == BT_Ledge ? vec3(1.5f, 0.25f, 0.8f) : vec3(1, 1, 1);
    if (t == BT_Goal || t == BT_Spawn) b.size = vec3(0.6f, 0.8f, 0.6f);
    if (t == BT_Bouncy) b.size = vec3(1.2f, 0.2f, 1.2f);
    if (t == BT_Spinner) b.size = vec3(2.5f, 0.3f, 1.0f);
    b.color = kBlockColors[colorIdx % kBlockColorCount];
    b.birth = worldTime;
    level.blocks.push_back(b);
    if (onSound) onSound(5);   // place
    if (onFx) onFx(0, pos, b.color);
}

void GameWorld::editorDeleteSelected() {
    if (editorSelected >= 0 && editorSelected < (int)level.blocks.size()) {
        const Block& b = level.blocks[editorSelected];
        despawns.push_back({b.pos, b.size, b.rotY, b.color, 0});
        if (onSound) onSound(6);   // delete
        if (onFx) onFx(1, b.pos, b.color);
        level.blocks.erase(level.blocks.begin() + editorSelected);
        editorSelected = -1;
    }
}

void GameWorld::editorMoveSelected(const vec3& delta) {
    if (editorSelected >= 0 && editorSelected < (int)level.blocks.size())
        level.blocks[editorSelected].pos += delta;
}

void GameWorld::editorScaleSelected(const vec3& deltaSize) {
    if (editorSelected >= 0 && editorSelected < (int)level.blocks.size()) {
        Block& b = level.blocks[editorSelected];
        b.size.x = std::max(0.2f, b.size.x + deltaSize.x);
        b.size.y = std::max(0.2f, b.size.y + deltaSize.y);
        b.size.z = std::max(0.2f, b.size.z + deltaSize.z);
    }
}

void GameWorld::editorRotateSelected(float dDeg) {
    if (editorSelected >= 0 && editorSelected < (int)level.blocks.size())
        level.blocks[editorSelected].rotY += dDeg;
}

} // namespace lv
