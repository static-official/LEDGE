// LEDGE VR — world: level data, physics, GrabVR-style grab movement, editor state
#pragma once
#include "mathlib.h"
#include <string>
#include <vector>
#include <functional>

namespace lv {

// block types (editor palette)
enum BlockType : uint8_t {
    BT_Block = 0,    // solid box
    BT_Ledge,        // thin grabbable ledge
    BT_Spinner,      // rotating platform (visual spin; still solid)
    BT_Spawn,        // player spawn point
    BT_Goal,         // level goal — touch to finish
    BT_Bouncy,       // trampoline
    BT_COUNT
};
static const char* blockTypeName(uint8_t t) {
    static const char* names[] = { "Block", "Ledge", "Spinner", "Spawn", "Goal", "Bouncy" };
    return names[t];
}

struct Block {
    vec3 pos;          // center
    vec3 size;         // full extents
    float rotY = 0;    // yaw degrees
    uint8_t type = BT_Block;
    vec3 color = vec3(0.95f, 0.78f, 0.62f);
    float birth = -1;  // world time the block was placed (pop-in anim; not saved)
};

// despawn ghost: deleted block shrinking out (animation only)
struct DespawnFx {
    vec3 pos, size;
    float rotY;
    vec3 color;
    float t = 0;
};

struct Level {
    std::string name = "hub";
    std::string author;
    std::vector<Block> blocks;
    int32_t version = 1;

    // helpers
    vec3 spawnPoint() const {
        for (auto& b : blocks)
            if (b.type == BT_Spawn) return b.pos + vec3(0, b.size.y * 0.5f + 1.0f, 0);
        return vec3(0, 2, 0);
    }
    std::string toJson() const;
    bool fromJson(const std::string& s);
};

// tiny JSON (writer + minimal parser sufficient for levels/saves)
std::string jsonEscape(const std::string& s);
bool jsonParse(const std::string& s,
               std::function<void(const std::string& path, const std::string& key,
                                  const std::string& val)> onLeaf);

struct PlayerBody {
    vec3 pos;          // feet position (bottom of capsule)
    vec3 vel;
    float yaw = 0;
    static constexpr float Radius = 0.28f;
    static constexpr float Height = 1.35f;    // ~Gorilla-ish compact body
};

struct Avatar {
    int32_t actorNr = 0;
    std::string name;
    vec3 pos;
    float yaw = 0;
    vec3 handL, handR;
    vec4 color = vec4(0.95f, 0.6f, 0.65f, 1);
    uint8_t hat = 0;
    double lastSeen = 0;
    vec3 head;
    uint8_t emote = 0;      // Emote enum (anim.h)
    float emoteT = 99;      // seconds since emote started
};

struct RemotePlayer {  // alias with cosmetics
    Avatar av;
};

struct EditorState {
    bool active = false;
    bool testing = false;   // play the level from spawn
    int selected = -1;      // index into level.blocks, -1 = none
    BlockType palette = BT_Block;
    int paletteColor = 0;   // index into color palette
    float gridSize = 0.5f;
    vec3 cursorPos;         // where the ghost block sits
    uint8_t hatPreview = 0;
};

struct GameSettings {
    std::string playerName = "Ledgelord";
    bool strafeEnabled = true;       // left stick walking
    bool snapTurn = true;
    float snapTurnAngle = 45.0f;
    float smoothTurnSpeed = 120.0f;  // deg/s
    float strafeSpeed = 3.2f;
    float grabStrength = 1.0f;       // fling multiplier
    bool voiceEnabled = true;
    bool micOpen = true;             // open mic
    int bodyColor = 3;               // cosmetic palette index
    int hat = 0;
    float menuScale = 1.0f;
    int uiTheme = 0;                 // pastel themes
    std::string region = "eu";
};

struct Cosmetic {
    const char* name;
    int hatId;         // 0 = none, 1 = top hat, 2 = halo, 3 = horns, 4 = cap, 5 = crown
};

static const Cosmetic kCosmetics[] = {
    { "None", 0 }, { "Top Hat", 1 }, { "Halo", 2 }, { "Horns", 3 }, { "Cap", 4 }, { "Crown", 5 },
};
static const int kCosmeticCount = 6;

static const vec3 kBodyColors[] = {
    vec3(0.95f, 0.78f, 0.62f), vec3(0.62f, 0.90f, 0.72f), vec3(0.65f, 0.76f, 0.95f),
    vec3(0.95f, 0.62f, 0.68f), vec3(0.85f, 0.68f, 0.95f), vec3(1.00f, 0.85f, 0.55f),
    vec3(0.75f, 0.92f, 0.92f), vec3(0.55f, 0.58f, 0.65f),
};
static const int kBodyColorCount = 8;

static const vec3 kBlockColors[] = {
    vec3(0.95f, 0.78f, 0.62f), vec3(0.62f, 0.90f, 0.72f), vec3(0.65f, 0.76f, 0.95f),
    vec3(0.95f, 0.62f, 0.68f), vec3(0.90f, 0.90f, 0.92f), vec3(0.55f, 0.58f, 0.65f),
    vec3(1.00f, 0.85f, 0.55f), vec3(0.60f, 0.85f, 0.60f),
};
static const int kBlockColorCount = 8;

// ---------------------------------------------------------------------------
class GameWorld {
public:
    GameWorld();

    void loadHub();                      // built-in hub map
    bool loadLevelJson(const std::string& json);
    void setLevel(const Level& lv) { level = lv; }

    // input per frame: hand poses in world space (LOCAL room space), buttons
    struct FrameInput {
        bool grabL, grabR;
        vec3 handL, handR;
        vec3 headPos;
        float headYaw;
        vec2 stickL, stickR;
        bool menuPressed;             // three-lines (left Y) — opens menu
        float dt;
        bool inMenu;                  // menu open: freeze grab movement
        bool jumpPressed = false;     // mobile jump button (VR build: always false)
    };

    void update(const FrameInput& in, GameSettings& settings, EditorState& ed);

    // physics query for grabbing: is there world surface near hand?
    bool handNearWorld(const vec3& hand) const;
    vec3 bodyFeet() const { return body.pos; }
    vec3 bodyCenter() const { return body.pos + vec3(0, PlayerBody::Height * 0.5f, 0); }
    float bodyYaw() const { return body.yaw; }
    vec3 velocity() const { return body.vel; }
    bool onGroundFlag() const { return onGround; }
    bool goalReached() const { return goalHit; }
    void resetGoal() { goalHit = false; }

    // events for sounds
    std::function<void(int /*0 grab,1 release,2 land,3 goal,4 bounce,5 jump*/)> onSound;
    // events for visual fx: 0 place puff, 1 delete puff, 2 land dust, 3 goal burst, 4 emote sparkle
    std::function<void(int, const vec3& pos, const vec3& color)> onFx;

    Level level;
    PlayerBody body;
    float worldTime = 0;                    // advances in update()
    std::vector<DespawnFx> despawns;        // shrinking deleted blocks (anim)

    // editor helpers
    bool raycastBlocks(const vec3& origin, const vec3& dir, float maxDist, int& hitIdx, vec3& hitPos) const;
    void editorAddBlock(const vec3& pos, BlockType t, int colorIdx);
    void editorDeleteSelected();
    void editorMoveSelected(const vec3& delta);
    void editorScaleSelected(const vec3& deltaSize);
    void editorRotateSelected(float dDeg);

    int editorSelected = -1;

private:
    int selected_ = -1;
    bool onGround = false;
    bool grabbingL = false, grabbingR = false;
    vec3 prevHandL, prevHandR;
    bool havePrevHands = false;
    bool goalHit = false;
    vec3 snapStickL, snapStickR;
    float spinnerAngle = 0;

    void integrate(const FrameInput& in, const GameSettings& s);
    void collide();
    bool collideBox(const Block& b, vec3& pos, vec3& vel, bool& grounded) const;
};

// built-in featured levels (searchable offline)
std::vector<Level> builtInLevels();
Level makeHubLevel();

} // namespace lv
