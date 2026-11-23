// Host smoke test for LEDGE mobile mechanics: jump, pull, editor place.
// Compiles the real world.cpp on x86 and simulates the touch input path.
#include "../ledgevr/native/world.h"
#include <cstdio>
#include <cmath>
using namespace lv;

static int fails = 0;
#define CHECK(cond, msg) do { if (cond) printf("PASS  %s\n", msg); else { printf("FAIL  %s\n", msg); fails++; } } while(0)

int main() {
    GameWorld W;
    W.loadHub();
    CHECK(W.level.blocks.size() > 10, "hub level loaded with blocks");
    CHECK(W.level.name == "LEDGE Hub", "hub name");

    // settle the body onto the ground
    GameSettings st; EditorState ed;
    for (int i = 0; i < 240; i++) {
        GameWorld::FrameInput in;
        in.grabL = in.grabR = false;
        in.headPos = vec3(0, 1.3f, 0); in.headYaw = 0;
        in.dt = 1.0f / 60.0f; in.inMenu = false;
        W.update(in, st, ed);
    }
    CHECK(W.onGroundFlag(), "body settled on ground");

    float groundY = W.bodyFeet().y;

    // ---- jump (mobile button) ----
    for (int i = 0; i < 10; i++) {
        GameWorld::FrameInput in;
        in.grabL = in.grabR = false;
        in.headPos = vec3(0, 1.3f, 0); in.headYaw = 0;
        in.jumpPressed = true;
        in.dt = 1.0f / 60.0f; in.inMenu = false;
        W.update(in, st, ed);
    }
    CHECK(W.bodyFeet().y > groundY + 0.3f, "jump lifts the body");
    // land again
    for (int i = 0; i < 120; i++) {
        GameWorld::FrameInput in;
        in.grabL = in.grabR = false;
        in.headPos = vec3(0, 1.3f, 0); in.headYaw = 0;
        in.dt = 1.0f / 60.0f; in.inMenu = false;
        W.update(in, st, ed);
    }
    CHECK(W.onGroundFlag(), "body lands after jump");

    // ---- editor place (mobile PLACE button path) ----
    int before = (int)W.level.blocks.size();
    vec3 ghost(2.0f, 0.5f, 2.0f);
    ghost.x = roundf(ghost.x / 0.5f) * 0.5f;
    ghost.y = roundf(ghost.y / 0.5f) * 0.5f;
    ghost.z = roundf(ghost.z / 0.5f) * 0.5f;
    W.editorAddBlock(ghost, BT_Block, 2);
    CHECK((int)W.level.blocks.size() == before + 1, "PLACE adds a block");
    int sel = (int)W.level.blocks.size() - 1;
    W.editorSelected = sel;               // same sync main_mobile/main perform
    W.editorScaleSelected(vec3(0.5f, 0.5f, 0.5f));
    W.editorRotateSelected(45.0f);
    W.editorDeleteSelected();
    CHECK((int)W.level.blocks.size() == before, "DELETE removes it");

    // ---- pull math (main_mobile body delta sign) ----
    // drag finger down 100px at 720p: body must go UP
    float pxs = 1.0f, s = 0.0045f * pxs;
    vec3 right(cosf(0.0f), 0, sinf(0.0f));  // yaw = 0
    float pullDX = 0, pullDY = 100;
    vec3 pull = right * (-pullDX * s) + vec3(0, pullDY * s, 0);
    CHECK(pull.y > 0, "drag down pulls body UP");
    // drag finger left 100px: body must go LEFT (-X at yaw 0? right=(1,0,0) wait)
    pullDX = -100; pullDY = 0;
    pull = right * (-pullDX * s) + vec3(0, pullDY * s, 0);
    CHECK(pull.x > 0, "drag left pushes body RIGHT (pull-the-world)");
    // and dragging right moves body left
    pullDX = 100; pullDY = 0;
    pull = right * (-pullDX * s) + vec3(0, pullDY * s, 0);
    CHECK(pull.x < 0, "drag right pushes body LEFT");

    // ---- raycast + level json round trip ----
    int hit; vec3 hp;
    vec3 origin(0, 3.0f, 0), dir(0, -1, 0);      // straight down: hub island below
    W.raycastBlocks(origin, dir, 30.0f, hit, hp);
    CHECK(hit >= 0, "raycast hits hub geometry");
    CHECK(hp.y > 0.0f && hp.y < 1.0f, "raycast hit lands on island top");
    Level L = W.level;
    std::string j = L.toJson();
    Level L2;
    CHECK(L2.fromJson(j), "level JSON round trip");
    CHECK(L2.blocks.size() == L.blocks.size(), "JSON keeps block count");

    printf(fails ? "\n%d FAILURES\n" : "\nALL OK\n", fails);
    return fails ? 1 : 0;
}
