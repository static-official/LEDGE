// LEDGE VR — main: Android glue, OpenXR loop, ImGui menu, editor, multiplayer
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#include <jni.h>
#include "ledui/ledui.h"
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "log.h"
#include "mathlib.h"
#include "xr_backend.h"
#include "renderer.h"
#include "world.h"
#include "anim.h"
#include "textures.h"
#include "photon.h"
#include "save.h"
#include "playfab.h"
#include "jnihost.h"

using namespace lv;

// SFX ids — MUST match AudioBridge.SFX_FILES order (Java)
enum Sfx : int {
    SFX_GRAB = 0, SFX_RELEASE, SFX_LAND, SFX_GOAL, SFX_BOUNCE, SFX_PLACE,
    SFX_DELETE, SFX_CLICK, SFX_OPEN, SFX_SUCCESS, SFX_WHOOSH, SFX_POP, SFX_EMOTE
};

// ---------------------------------------------------------------------------
static struct {
    XrBackend xr;
    Renderer R;
    GameWorld W;
    SaveData save;
    SaveStore store;
    EditorState ed;
    PhotonClient pc;      // realtime
    PhotonClient vpc;     // voice
    std::vector<Avatar> avatars;
    TexLib tex;           // 1.1 material pack
    ParticleSystem ps;    // 1.1 particles
    bool menuOpen = false;
    bool menuTabWasOpen = false;
    int menuTab = 0;
    int lastTab = 0;                 // 1.1 tab-change click sound
    std::string menuInput;           // text field buffer
    bool loading = true;
    std::string status = "Starting";
    float prevTriggerR = 0;
    float prevMenuBtn = 0;
    bool snapCooldown = false;
    std::string dataDir;
    bool pfAuthApplied = false;      // PlayFab token wired into Photon clients
    // 1.1 animation state
    float menuAnimT = 1.0f;          // menu slide-in tween
    float worldClock = 0;            // for hat bob / goal pulse
    bool wasAir = false;             // landing squash detection
    float prevVelY = 0;
    float squash = 1.0f;             // body squash-stretch (1 = rest)
    uint8_t emote = EM_NONE;         // local emote
    float emoteT = 99;
    bool prevA = false, prevB = false, prevStickL = false;
    float mvol = 0.8f, svol = 1.0f;  // audio mixer levels
} G;

static void pushStatus(const std::string& s) { G.status = s; }

// start an emote locally + broadcast it (event 61)
static void startEmote(uint8_t e) {
    G.emote = e;
    G.emoteT = 0;
    jni().sfx(SFX_EMOTE);
    vec3 c = kBodyColors[G.save.settings.bodyColor % kBodyColorCount];
    G.ps.spawn(ParticleSystem::FX_EMOTE, G.W.bodyFeet(), vec3(c.x, c.y, c.z));
    if (G.pc.status == PH_InRoom)
        G.pc.raiseEvent(61, {
            { 1, PV::Int_(G.pc.actorNr) },
            { 2, PV::Int_((int)e) },
        }, true);
}

// Wire the PlayFab Photon token into both Photon clients (custom auth).
// Called each frame; no-op until a token arrives.
static void pfApplyAuth() {
    if (G.pfAuthApplied) return;
    PfSnapshot s = pf().snapshot();
    if (!s.loggedIn || s.photonToken.empty()) return;
    // PlayFab Photon-addon credential format (verified live 2026-11-23):
    // GET query string "username=<PlayFabId>&token=<GetPhotonAuthenticationToken>".
    std::string authParams = "username=" + s.playFabId + "&token=" + s.photonToken;
    for (PhotonClient* pc : { &G.pc, &G.vpc }) {
        pc->useCustomAuth = true;
        pc->authData = authParams;
        pc->userId = s.playFabId;
    }
    G.pfAuthApplied = true;
    pushStatus(std::string("PlayFab auth ready: ") +
               (s.displayName.empty() ? s.playFabId : s.displayName));
}

// ---------------------------------------------------------------------------
// ImGui backend: render into the UI quad swapchain
// ---------------------------------------------------------------------------
static GLuint gUiFboTex = 0;
static int gUiW = 1536, gUiH = 1536;

static void uiInit() {
    ledui::CreateContext();
    ledui::IO& io = ledui::GetIO();
    io.Fonts->AddFontDefault();
    unsigned char* pixels = nullptr;
    int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    glGenTextures(1, &gUiFboTex);
    glBindTexture(GL_TEXTURE_2D, gUiFboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    io.Fonts->TexID = (ledui::TextureID)(intptr_t)gUiFboTex;
    G.R.wtext.setAtlas((GLuint)(intptr_t)io.Fonts->TexID, (float)w, (float)h);
    ledui::StyleColorsDark();
    ledui::Style& st = ledui::GetStyle();
    st.WindowRounding = 12;
    st.FrameRounding = 8;
    st.GrabRounding = 8;
    st.TabRounding = 8;
    ledui::Vec4* c = st.Colors;
    c[ledui::Col_WindowBg] = ledui::Vec4(0.13f, 0.14f, 0.19f, 0.97f);
    c[ledui::Col_TitleBg] = ledui::Vec4(0.15f, 0.16f, 0.23f, 1.0f);
    c[ledui::Col_TitleBgActive] = ledui::Vec4(0.24f, 0.30f, 0.42f, 1.0f);
    c[ledui::Col_Button] = ledui::Vec4(0.30f, 0.38f, 0.55f, 1.0f);
    c[ledui::Col_ButtonHovered] = ledui::Vec4(0.42f, 0.52f, 0.72f, 1.0f);
    c[ledui::Col_ButtonActive] = ledui::Vec4(0.24f, 0.30f, 0.42f, 1.0f);
    c[ledui::Col_Tab] = ledui::Vec4(0.24f, 0.28f, 0.40f, 1.0f);
    c[ledui::Col_TabActive] = ledui::Vec4(0.38f, 0.46f, 0.66f, 1.0f);
    c[ledui::Col_CheckMark] = ledui::Vec4(0.65f, 0.85f, 1.0f, 1.0f);
    c[ledui::Col_SliderGrab] = ledui::Vec4(0.55f, 0.75f, 0.95f, 1.0f);
    c[ledui::Col_Text] = ledui::Vec4(0.94f, 0.95f, 1.0f, 1.0f);
}

static GLuint gUiVao = 0, gUiVbo = 0, gUiIbo = 0;
static void uiNewFrame();
static int mainLoop(android_app* app);

static void uiRenderFrame(const ledui::Vec2& sizePx) {
    ledui::IO& io = ledui::GetIO();
    io.DisplaySize = sizePx;
    io.DeltaTime = 1.0f / 72.0f;
    ledui::Render();
    ledui::DrawData* dd = ledui::GetDrawData();
    if (!dd) return;
    glViewport(0, 0, (GLsizei)sizePx.x, (GLsizei)sizePx.y);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(G.R.uiProg);
    mat4 ortho = mat4::ortho(0, sizePx.x, sizePx.y, 0, -1, 1);
    glUniformMatrix4fv(glGetUniformLocation(G.R.uiProg, "uProj"), 1, GL_FALSE, ortho.m);
    glUniform1i(glGetUniformLocation(G.R.uiProg, "uTex"), 0);
    if (!gUiVao) {
        glGenVertexArrays(1, &gUiVao);
        glGenBuffers(1, &gUiVbo);
        glGenBuffers(1, &gUiIbo);
        glBindVertexArray(gUiVao);
        glBindBuffer(GL_ARRAY_BUFFER, gUiVbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(ledui::DrawVert), (void*)offsetof(ledui::DrawVert, pos));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ledui::DrawVert), (void*)offsetof(ledui::DrawVert, uv));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ledui::DrawVert), (void*)offsetof(ledui::DrawVert, col));
    }
    for (int n = 0; n < dd->CmdListsCount; n++) {
        const ledui::DrawList* cl = dd->CmdLists[n];
        glBindVertexArray(gUiVao);
        glBindBuffer(GL_ARRAY_BUFFER, gUiVbo);
        glBufferData(GL_ARRAY_BUFFER, cl->VtxBuffer.Size * sizeof(ledui::DrawVert), cl->VtxBuffer.Data, GL_STREAM_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gUiIbo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, cl->IdxBuffer.Size * sizeof(ledui::DrawIdx), cl->IdxBuffer.Data, GL_STREAM_DRAW);
        for (int ci = 0; ci < cl->CmdBuffer.Size; ci++) {
            const ledui::DrawCmd& cmd = cl->CmdBuffer[ci];
            if (cmd.UserCallback) continue;
            glScissor((int)cmd.ClipRect.x, (int)(sizePx.y - cmd.ClipRect.w),
                      (int)(cmd.ClipRect.z - cmd.ClipRect.x), (int)(cmd.ClipRect.w - cmd.ClipRect.y));
            glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)cmd.GetTexID());
            glDrawElements(GL_TRIANGLES, (GLsizei)cmd.ElemCount, GL_UNSIGNED_SHORT,
                           (void*)(intptr_t)cmd.IdxOffset);
        }
    }
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glBindVertexArray(0);
}

// ---------------------------------------------------------------------------
// menu screens
// ---------------------------------------------------------------------------
static void drawMenu() {
    ledui::IO& io = ledui::GetIO();
    ledui::SetNextWindowPos(ledui::Vec2(0, 0));
    ledui::SetNextWindowSize(io.DisplaySize);
    ledui::Begin("##LEDGE", nullptr,
                 ledui::WinFlags::NoDecoration | ledui::WinFlags::NoMove | ledui::WinFlags::NoSavedSettings);

    const char* tabs[] = { "Home", "Lobbies", "People", "Levels", "Editor", "Settings", "Cosmetics", "Account" };
    for (int i = 0; i < 8; i++) {
        if (ledui::Button(tabs[i], ledui::Vec2(ledui::GetContentRegionAvail().x / 8.0f - 4, 56))) {
            if (G.menuTab != i) jni().sfx(SFX_CLICK);   // 1.1 UI click
            G.menuTab = i;
        }
        if (i < 7) ledui::SameLine();
    }
    ledui::Separator();
    ledui::Text("LEDGE VR  |  %s  |  Photon: %s", G.status.c_str(),
                G.pc.status == PH_InRoom ? "connected" : photonStatusName((int)G.pc.status));

    switch (G.menuTab) {
    case 0: { // Home
        ledui::TextWrapped("Welcome to LEDGE VR! Hold GRIP to grab the world and pull yourself, "
                           "release to fling. Left stick walks. The three-lines button opens this menu. "
                           "1.1: press A to DANCE, B to WAVE, click the left stick to TAUNT — "
                           "everyone in the room sees it!");
        ledui::Separator();
        ledui::Text("Emotes:");
        if (ledui::Button("Wave##vw", ledui::Vec2(150, 50))) startEmote(EM_WAVE);
        ledui::SameLine();
        if (ledui::Button("Dance##vd", ledui::Vec2(150, 50))) startEmote(EM_DANCE);
        ledui::SameLine();
        if (ledui::Button("Taunt##vt", ledui::Vec2(150, 50))) startEmote(EM_TAUNT);
        ledui::Separator();        if (ledui::Button(G.pc.status == PH_InRoom ? "In lobby room" : "Quick join LEDGE-Public",
                          ledui::Vec2(340, 70))) {
            if (G.pc.status == PH_InRoom) G.pc.leaveRoom();
            else G.pc.joinRoom("LEDGE-Public", 16);
        }
        ledui::SameLine();
        if (ledui::Button("Leave room", ledui::Vec2(220, 70)))
            if (G.pc.status == PH_InRoom) G.pc.leaveRoom();
        ledui::Text("Players here: %d", (int)G.pc.actors.size());
        break;
    }
    case 1: { // Lobbies — join / leave / create with room code
        static char roomCode[64] = "LEDGE-Public";
        ledui::InputText("Room code", roomCode, sizeof(roomCode));
        if (ledui::Button("Join / Create", ledui::Vec2(240, 60)))
            G.pc.joinRoom(roomCode, 16);
        ledui::SameLine();
        if (ledui::Button("Leave", ledui::Vec2(140, 60))) G.pc.leaveRoom();
        ledui::Separator();
        ledui::Text("Open rooms (Photon lobby):");
        if (G.pc.roomList.empty()) ledui::Text("  (lobby list empty — join the public room instead)");
        for (auto& r : G.pc.roomList) {
            char label[128];
            snprintf(label, sizeof(label), "%s  (%d/%d)%s", r.name.c_str(), r.playerCount,
                     r.maxPlayers, r.isOpen ? "" : " [closed]");
            if (ledui::Button(label, ledui::Vec2(420, 54))) G.pc.joinRoom(r.name, 16);
        }
        break;
    }
    case 2: { // People — add people, see players
        static char person[64] = "";
        ledui::InputText("Add person (name/user id)", person, sizeof(person));
        ledui::SameLine();
        if (ledui::Button("Add", ledui::Vec2(120, 48)) && person[0]) {
            G.save.friends.push_back(person);
            G.store.save(G.save);
            person[0] = 0;
        }
        ledui::Text("Friends (%d):", (int)G.save.friends.size());
        for (size_t i = 0; i < G.save.friends.size(); i++) {
            char label[160];
            snprintf(label, sizeof(label), "%s  %s", G.save.friends[i].c_str(),
                     G.pc.actorNames.size() ? "" : "");
            ledui::BulletText("%s", G.save.friends[i].c_str());
            ledui::SameLine();
            char rid[32];
            snprintf(rid, sizeof(rid), "rm%zu##f%zu", i, i);
            if (ledui::SmallButton(rid)) {
                G.save.friends.erase(G.save.friends.begin() + i);
                G.store.save(G.save);
            }
        }
        ledui::Separator();
        ledui::Text("Players in current room:");
        for (auto& kv : G.pc.actorNames)
            ledui::BulletText("actor %d: %s%s", kv.first, kv.second.c_str(),
                              kv.first == G.pc.actorNr ? " (you)" : "");
        break;
    }
    case 3: { // Levels — search + play built-ins and my levels
        static char query[64] = "";
        ledui::InputText("Search levels", query, sizeof(query));
        auto matches = [](const std::string& n) {
            if (!query[0]) return true;
            return n.find(query) != std::string::npos;
        };
        ledui::Text("Featured:");
        static std::vector<Level> featured = builtInLevels();
        for (auto& L : featured) {
            if (!matches(L.name)) continue;
            char label[128];
            snprintf(label, sizeof(label), "%s  by %s  (%d blocks)", L.name.c_str(),
                     L.author.c_str(), (int)L.blocks.size());
            if (ledui::Button(label, ledui::Vec2(480, 56))) {
                G.W.setLevel(L);
                G.ed.testing = false;
                pushStatus("playing " + L.name);
            }
        }
        ledui::Text("My levels (saved):");
        for (size_t i = 0; i < G.save.myLevels.size(); i++) {
            auto& L = G.save.myLevels[i];
            if (!matches(L.name)) continue;
            char label[128];
            snprintf(label, sizeof(label), "%s  (%d blocks)", L.name.c_str(), (int)L.blocks.size());
            char id[40];
            snprintf(id, sizeof(id), "p%zu", i);
            if (ledui::Button(label, ledui::Vec2(480, 56))) {
                G.W.setLevel(L);
                pushStatus("playing " + L.name);
            }
            ledui::SameLine();
            snprintf(id, sizeof(id), "pub%zu", i);
            if (ledui::Button("Publish")) {
                if (G.pc.status == PH_InRoom)
                    G.pc.setRoomProps({ { "LEDGE_level_" + std::to_string(i), PV::Str_(L.toJson()) } });
                pushStatus("published " + L.name + " to room");
            }
        }
        break;
    }
    case 4: { // Editor
        ledui::Checkbox("Editor active", &G.ed.active);
        ledui::Checkbox("Test level (respawn)", &G.ed.testing);
        ledui::Separator();
        G.W.editorSelected = G.ed.selected;   // keep world selection in sync
        const char* types[] = { "Block", "Ledge", "Spinner", "Spawn", "Goal", "Bouncy" };
        for (int t = 0; t < BT_COUNT; t++) {
            if (ledui::RadioButton(types[t], (int)G.ed.palette == t)) G.ed.palette = (BlockType)t;
            if (t % 3 != 2) ledui::SameLine();
        }
        ledui::Separator();
        for (int c = 0; c < kBlockColorCount; c++) {
            ledui::PushStyleColor(ledui::Col_Button,
                ledui::Vec4(kBlockColors[c].x, kBlockColors[c].y, kBlockColors[c].z, 1));
            char id[16];
            snprintf(id, sizeof(id), "  ##c%d", c);
            if (ledui::Button(id, ledui::Vec2(64, 44))) G.ed.paletteColor = c;
            ledui::PopStyleColor();
            ledui::SameLine();
        }
        ledui::NewLine();
        ledui::Separator();
        if (G.ed.selected >= 0) {
            ledui::Text("Selected block %d", G.ed.selected);
            if (ledui::Button("Delete", ledui::Vec2(160, 50))) {
                G.W.editorDeleteSelected();
                G.ed.selected = -1;
            }
            ledui::SameLine();
            if (ledui::Button("Grow +", ledui::Vec2(120, 50))) G.W.editorScaleSelected(vec3(0.5f, 0.5f, 0.5f));
            ledui::SameLine();
            if (ledui::Button("Shrink -", ledui::Vec2(120, 50))) G.W.editorScaleSelected(vec3(-0.5f, -0.5f, -0.5f));
            ledui::SameLine();
            if (ledui::Button("Rotate 45°", ledui::Vec2(160, 50))) G.W.editorRotateSelected(45.0f);
        } else {
            ledui::TextWrapped("Point at a block with the right controller and press TRIGGER to select. "
                               "Trigger on empty air places the palette block at the grid cursor.");
        }
        if (ledui::Button("Save level", ledui::Vec2(200, 56))) {
            G.save.myLevels.push_back(G.W.level);
            G.store.save(G.save);
            pushStatus("saved " + G.W.level.name);
        }
        break;
    }
    case 5: { // Settings — customize movement/UI
        ledui::Text("Player name:");
        static char pname[64] = "";
        if (pname[0] == 0) snprintf(pname, sizeof(pname), "%s", G.save.settings.playerName.c_str());
        ledui::InputText("##pn", pname, sizeof(pname));
        ledui::SameLine();
        if (ledui::Button("Set", ledui::Vec2(100, 44))) {
            G.save.settings.playerName = pname;
            G.pc.nick = pname;
            if (G.pc.status == PH_InRoom) G.pc.setPlayerProps({ { "n", PV::Str_(pname) } });
            G.store.save(G.save);
        }
        ledui::Checkbox("Left-stick strafe", &G.save.settings.strafeEnabled);
        ledui::Checkbox("Snap turn", &G.save.settings.snapTurn);
        ledui::SliderFloat("Strafe speed", &G.save.settings.strafeSpeed, 1.0f, 6.0f, "%.1f m/s");
        ledui::SliderFloat("Fling strength", &G.save.settings.grabStrength, 0.5f, 2.0f);
        ledui::Checkbox("Voice chat (open mic)", &G.save.settings.voiceEnabled);
        ledui::SliderFloat("Menu size", &G.save.settings.menuScale, 0.6f, 1.8f, "%.1fx");
        ledui::Separator();
        // 1.1 music player
        ledui::Text("Music: %s", musicTitle(jni().musicTrack()));
        ledui::SameLine();
        if (ledui::Button("Next track", ledui::Vec2(180, 44))) jni().musicNext();
        ledui::SliderFloat("Music volume", &G.mvol, 0.0f, 1.0f, "%.1f");
        if (ledui::IsItemEdited()) jni().musicVol(G.mvol);
        ledui::SliderFloat("SFX volume", &G.svol, 0.0f, 1.0f, "%.1f");
        if (ledui::IsItemEdited()) jni().sfxVol(G.svol);
        ledui::Separator();
        ledui::Text("Photon region:");
        static const char* regions[] = { "eu", "us", "asia", "jp", "au", "kr", "ca", "sa", "in", "ru" };
        for (int i = 0; i < 10; i++) {
            if (ledui::RadioButton(regions[i], G.save.settings.region == regions[i])) {
                G.save.settings.region = regions[i];
                G.pc.region = regions[i];
                G.store.save(G.save);
            }
            if (i % 5 != 4) ledui::SameLine();
        }
        if (ledui::Button("Save settings", ledui::Vec2(200, 52))) G.store.save(G.save);
        break;
    }
    case 6: { // Cosmetics — try on (see yourself in the mirror!)
        ledui::Text("Body color:");
        for (int c = 0; c < kBodyColorCount; c++) {
            ledui::PushStyleColor(ledui::Col_Button,
                ledui::Vec4(kBodyColors[c].x, kBodyColors[c].y, kBodyColors[c].z, 1));
            char id[16];
            snprintf(id, sizeof(id), "  ##b%d", c);
            if (ledui::Button(id, ledui::Vec2(64, 44))) {
                G.save.settings.bodyColor = c;
                G.store.save(G.save);
            }
            ledui::PopStyleColor();
            ledui::SameLine();
        }
        ledui::NewLine();
        ledui::Text("Hats:");
        for (int i = 0; i < kCosmeticCount; i++) {
            char id[40];
            snprintf(id, sizeof(id), "%s##hat%d", kCosmetics[i].name, i);
            if (ledui::Button(id, ledui::Vec2(140, 48))) {
                G.save.settings.hat = kCosmetics[i].hatId;
                G.store.save(G.save);
                if (G.pc.status == PH_InRoom)
                    G.pc.setPlayerProps({ { "hat", PV::Int_(G.save.settings.hat) },
                                          { "c", PV::Int_(G.save.settings.bodyColor) } });
            }
            if (i % 3 != 2) ledui::SameLine();
        }
        ledui::TextWrapped("Walk up to the mirror wall in the hub to try cosmetics on yourself!");
        break;
    }
    case 7: { // Account — PlayFab (Title 1762FA) sign-in, cloud save, leaderboard
        PfSnapshot ps = pf().snapshot();
        ledui::Text("PlayFab Title %s  —  %s", PlayFabClient::TITLE_ID(),
                    ps.loggedIn ? "signed in" : (ps.busy ? ps.status.c_str() : "not signed in"));
        if (ps.loggedIn) {
            ledui::Text("PlayFab ID: %s%s", ps.playFabId.c_str(),
                        ps.newlyCreated ? "  (new account)" : "");
            ledui::Text("Display name: %s", ps.displayName.empty() ? "(none)" : ps.displayName.c_str());
            ledui::Text("Device ID: %s", ps.customId.c_str());
            ledui::Text("Photon token: %s", ps.photonToken.empty() ? "(none yet)"
                                       : (G.pfAuthApplied ? "active" : "received"));
        }
        if (!ps.status.empty()) ledui::Text("Status: %s", ps.status.c_str());
        if (!ps.lastError.empty()) ledui::TextColored(ledui::Vec4(1, 0.55f, 0.55f, 1), "Error: %s", ps.lastError.c_str());
        ledui::Separator();

        if (ledui::Button(ps.loggedIn ? "Re-sign in / refresh" : "Sign in (device)", ledui::Vec2(300, 56)))
            pf().requestLogin();
        ledui::SameLine();
        if (ledui::Button("Title news", ledui::Vec2(200, 56)))
            pf().requestNews();

        ledui::Separator();
        ledui::TextWrapped("Cloud save (PlayFab UserData, key ledgevr_save):");
        if (ledui::Button("Upload local save", ledui::Vec2(300, 56)))
            pf().requestCloudSave(G.save.toJson());
        ledui::SameLine();
        if (ledui::Button("Download", ledui::Vec2(180, 56)))
            pf().requestCloudLoad();
        ledui::SameLine();
        if (ledui::Button("Apply to device", ledui::Vec2(240, 56))) {
            SaveData d;
            if (cloudUnpackSave(ps.cloudSave.empty() ? "{}" : ps.cloudSave, d)) {
                G.save = d;
                G.store.save(G.save);
                G.pc.nick = d.settings.playerName;
                pushStatus("cloud save applied");
            } else {
                pushStatus("cloud save parse failed");
            }
        }
        ledui::Separator();

        ledui::Text("Leaderboard (Goals completed):");
        if (ledui::Button("Upload my goals stat", ledui::Vec2(320, 56)))
            pf().requestUpdateStat("Goals", G.save.goalsCompleted);
        ledui::SameLine();
        if (ledui::Button("Top 10", ledui::Vec2(160, 56)))
            pf().requestLeaderboard("Goals", 10);
        if (!ps.leaderboard.empty()) {
            for (size_t i = 0; i < ps.leaderboard.size(); i++)
                ledui::BulletText("%d. %s — %d", (int)(i + 1),
                                  ps.leaderboard[i].name.c_str(), ps.leaderboard[i].value);
        }
        ledui::Separator();

        if (ledui::Button("Re-fetch Photon token", ledui::Vec2(360, 56)))
            pf().requestPhotonToken("15f80a97-41e4-4c8b-a887-9b8de6d103ad");
        ledui::TextWrapped("The token authenticates this device to Photon via PlayFab "
                           "custom auth automatically once received.");
        if (!ps.news.empty()) {
            ledui::Separator();
            ledui::Text("Title news:");
            for (auto& n : ps.news) {
                ledui::BulletText("%s", n.title.c_str());
                ledui::TextWrapped("  %s", n.body.c_str());
            }
        }
        break;
    }
    }
    ledui::End();
}

// ---------------------------------------------------------------------------
// photon wiring
// ---------------------------------------------------------------------------
static void photonInit() {
    G.pc.appId = "15f80a97-41e4-4c8b-a887-9b8de6d103ad";     // LEDGE VR realtime
    G.vpc.appId = "b56458c7-e48d-4353-9086-23e822c115cd";    // LEDGE VR voice
    G.pc.appVersion = "1.0";
    G.pc.nick = G.save.settings.playerName;
    G.pc.region = G.save.settings.region;
    G.pc.transportConnect = [](const std::string& url) { return jni().wsConnect(url); };
    G.pc.transportSend = [](const std::vector<uint8_t>& d) { jni().wsSend(d); };
    G.pc.transportClose = [] { jni().wsClose(); };
    G.pc.transportRecv = [](std::vector<uint8_t>& out) { jni().wsRecv(out); };
    G.pc.onStatus = [](int s, const std::string& d) {
        pushStatus(std::string(photonStatusName(s)) + (d.empty() ? "" : (": " + d)));
    };
    G.pc.onPlayerJoin = [&](int32_t a, const std::string& nick) {
        Avatar av;
        av.actorNr = a;
        av.name = nick;
        av.pos = vec3(2, 1, 2);
        av.lastSeen = ledui::GetTime();
        av.color = vec4(0.7f, 0.8f, 0.9f, 1);
        G.avatars.push_back(av);
    };
    G.pc.onPlayerLeave = [&](int32_t a) {
        for (size_t i = 0; i < G.avatars.size(); i++)
            if (G.avatars[i].actorNr == a) { G.avatars.erase(G.avatars.begin() + i); break; }
    };
    G.pc.onEvent = [&](const PhotonEvent& ev) {
        // custom LEDGE events: 60 = avatar state
        if (ev.code == 60) {
            int32_t actor = 0;
            vec3 pos, handL, handR;
            float yaw = 0;
            if (auto* p = ev.get(1)) actor = (int32_t)p->getInt();
            if (auto* p = ev.get(2)) pos = vec3(p->getFloat(), 0, 0);
            if (auto* p = ev.get(3)) pos.y = p->getFloat();
            if (auto* p = ev.get(4)) pos.z = p->getFloat();
            if (auto* p = ev.get(5)) yaw = p->getFloat();
            for (auto& av : G.avatars)
                if (av.actorNr == actor) {
                    av.pos = pos;
                    av.yaw = yaw;
                    av.lastSeen = ledui::GetTime();
                    break;
                }
        }
        // 1.1: event 61 = emote broadcast
        if (ev.code == 61) {
            int32_t actor = 0;
            int emoteId = 0;
            if (auto* p = ev.get(1)) actor = (int32_t)p->getInt();
            if (auto* p = ev.get(2)) emoteId = (int)p->getInt();
            for (auto& av : G.avatars)
                if (av.actorNr == actor) {
                    av.emote = (uint8_t)(emoteId >= 0 && emoteId < EM_COUNT ? emoteId : 0);
                    av.emoteT = 0;
                    vec3 c(av.color.x, av.color.y, av.color.z);
                    G.ps.spawn(ParticleSystem::FX_EMOTE, av.pos + vec3(0, 0.4f, 0), c);
                    break;
                }
        }
    };
    G.pc.onPropsChanged = []() {
        auto it = G.pc.roomProps.find("LEDGE_cloudsave");
        if (it != G.pc.roomProps.end()) {
            SaveData cloud;
            if (cloudUnpackSave(it->second, cloud)) {
                if (cloud.goalsCompleted > G.save.goalsCompleted) {
                    G.save.goalsCompleted = cloud.goalsCompleted;
                    G.store.save(G.save);
                    pushStatus("cloud save synced");
                }
            }
        }
    };
    G.pc.connect(G.pc.appId, "1.0");
}

static void sendAvatarState() {
    if (G.pc.status != PH_InRoom) return;
    vec3 c = G.W.bodyCenter();
    std::vector<uint8_t> blob(64);
    G.pc.raiseEvent(60, {
        { 1, PV::Int_(G.pc.actorNr) },
        { 2, PV::Float_(c.x) },
        { 3, PV::Float_(c.y) },
        { 4, PV::Float_(c.z) },
        { 5, PV::Float_(G.W.bodyYaw()) },
    }, false);
}

// ---------------------------------------------------------------------------
// world rendering (hub, blocks, avatars, hands, editor ghosts)
// ---------------------------------------------------------------------------
static Mesh gBoxMesh, gRBoxMesh, gSphereMesh, gBodyMesh, gHandMesh, gPlaneMesh, gHatMeshes[6];
static bool gGeoBuilt = false;

static void buildGeo() {
    buildBoxMesh(gBoxMesh, vec3(0.5f, 0.5f, 0.5f), vec4(1, 1, 1, 1));
    buildRoundedBoxMesh(gRBoxMesh, vec3(0.5f, 0.5f, 0.5f), vec4(1, 1, 1, 1));
    buildSphereMesh(gSphereMesh, 0.5f, 20, vec4(1, 1, 1, 1));
    buildCapsuleMesh(gBodyMesh, 0.26f, 0.9f, vec4(1, 1, 1, 1));
    buildSphereMesh(gHandMesh, 0.16f, 14, vec4(1, 1, 1, 1));
    buildPlaneMesh(gPlaneMesh, 6, 3.5f, vec4(0.8f, 0.85f, 0.9f, 0.6f));
    // hats (6) — 1.1: crown gets cone spikes, top hat gets a proper brim
    buildCylinderMesh(gHatMeshes[1], 0.16f, 0.22f, 14, vec4(0.2f, 0.2f, 0.25f, 1));
    buildCylinderMesh(gHatMeshes[1], 0.28f, 0.03f, 14, vec4(0.2f, 0.2f, 0.25f, 1));
    buildSphereMesh(gHatMeshes[2], 0.24f, 14, vec4(1.0f, 0.95f, 0.6f, 1));   // halo ring-ish
    buildConeMesh(gHatMeshes[3], 0.09f, 0.24f, 10, vec4(0.8f, 0.3f, 0.3f, 1)); // horns
    buildCylinderMesh(gHatMeshes[4], 0.20f, 0.14f, 14, vec4(0.85f, 0.35f, 0.35f, 1));
    buildCylinderMesh(gHatMeshes[5], 0.22f, 0.18f, 8, vec4(1.0f, 0.9f, 0.4f, 1)); // crown
    for (auto& m : { &gBoxMesh, &gRBoxMesh, &gSphereMesh, &gBodyMesh, &gHandMesh, &gPlaneMesh,
                    &gHatMeshes[0], &gHatMeshes[1], &gHatMeshes[2], &gHatMeshes[3],
                    &gHatMeshes[4], &gHatMeshes[5] })
        m->upload();
    gGeoBuilt = true;
}

static void renderAvatar(const Avatar& av) {
    vec4 c = av.color;
    EmotePose ep = emotePose(av.emote, av.emoteT);
    mat4 bodyT = mat4::translate(av.pos + vec3(0, ep.bobY, 0))
               * mat4::rotY(av.yaw + ep.spinY) * mat4::rotZ(ep.leanZ);
    mat4 bodyScale = mat4::scale(vec3(2 - ep.squash, ep.squash, 2 - ep.squash));
    G.R.drawMesh(gBodyMesh, bodyT * mat4::translate(vec3(0, 0.45f, 0)) * bodyScale, c);
    G.R.drawMesh(gSphereMesh, bodyT * mat4::translate(vec3(0, 1.15f, 0)) * mat4::scale(vec3(0.56f)), c);
    if (av.hat > 0 && av.hat < 6) {
        // 1.1: hats bob; crowns spin; horns lean with the head
        float bob = sinf((float)ledui::GetTime() * 2.2f + av.actorNr * 1.7f) * 0.018f;
        mat4 hatT = bodyT * mat4::translate(vec3(0, 1.38f + bob + ep.hatLift, 0))
                  * (av.hat == 5 ? mat4::rotY((float)ledui::GetTime() * 1.6f) : mat4());
        if (av.hat == 3) {   // horns: two cones offset at render
            G.R.drawMesh(gHatMeshes[3], hatT * mat4::translate(vec3(-0.14f, 0.1f, 0)), vec4(1, 1, 1, 1));
            G.R.drawMesh(gHatMeshes[3], hatT * mat4::translate(vec3(0.14f, 0.1f, 0)), vec4(1, 1, 1, 1));
        } else {
            G.R.drawMesh(gHatMeshes[av.hat], hatT, vec4(1, 1, 1, 1));
        }
    }
    G.R.wtext.addText(av.name, av.pos + vec3(0, 1.7f, 0), 0.18f, vec4(1, 1, 1, 1));
}

static void renderWorld() {
    vec3 head = G.xr.headPos;
    G.R.beginFrame(head);
    G.R.drawSky();

    // world blocks — 1.1: rounded geometry + material textures + pop-in anim
    for (size_t i = 0; i < G.W.level.blocks.size(); i++) {
        const Block& b = G.W.level.blocks[i];
        vec4 c(b.color, 1);
        float s = 1;
        if (b.birth >= 0) {
            float age = G.W.worldTime - b.birth;
            if (age < 0.32f) s = 0.15f + 0.85f * easeOutBack(age / 0.32f);
        }
        int mat = TexLib::materialForPalette(
            nearestColorIdx(b.color, kBlockColors, kBlockColorCount));
        G.R.drawMeshTex(gRBoxMesh,
            mat4::translate(b.pos) * mat4::rotY(b.rotY * 3.14159265f / 180.0f)
                * mat4::scale(b.size * s),
            c, G.tex.tex(mat), vec2(1, 1));
        if (b.type == BT_Goal) {
            float pulse = 1.3f + sinf(G.worldClock * 4.0f) * 0.15f;
            vec4 glow(1.0f, 0.9f, 0.5f, 0.4f);
            G.R.drawMesh(gSphereMesh,
                mat4::translate(b.pos) * mat4::scale(vec3(pulse, pulse + 0.2f, pulse)), glow);
        }
    }

    // despawn ghosts — deleted blocks shrink out
    for (auto& d : G.W.despawns) {
        float k = easeInCubic(d.t / 0.28f);
        G.R.drawMeshTex(gRBoxMesh,
            mat4::translate(d.pos) * mat4::rotY(d.rotY * 3.14159265f / 180.0f)
                * mat4::scale(d.size * (1.0f - k * 0.92f)),
            vec4(d.color, 1 - k * 0.5f), 0);
    }

    // particles — 1.1
    for (auto& p : G.ps.parts) {
        float lf = p.life / p.maxLife;
        float sc = p.size * (1.0f - lf * 0.8f);
        G.R.drawMesh(gSphereMesh,
            mat4::translate(p.pos) * mat4::rotY(p.spin * p.life)
                * mat4::scale(vec3(sc, sc, sc)),
            vec4(p.color, 1));
    }

    // mirror wall in hub
    if (G.W.level.name == "LEDGE Hub") {
        mat4 m = mat4::translate(vec3(0, 1.8f, -8.2f)) * mat4::rotY(0);
        G.R.drawMesh(gPlaneMesh, m * mat4::rotY(0), vec4(0.75f, 0.85f, 0.95f, 0.5f));
        // draw "reflection": player body flipped
        const vec3 c = kBodyColors[G.save.settings.bodyColor % kBodyColorCount];
        vec4 bc(c.x, c.y, c.z, 1.0f);
        mat4 refl = mat4::translate(vec3(G.W.bodyFeet().x, G.W.bodyFeet().y, -16.4f - G.W.bodyFeet().z))
                  * mat4::rotY(G.W.bodyYaw() + 3.14159265f);
        G.R.drawMesh(gBodyMesh, refl * mat4::translate(vec3(0, 0.45f, 0)), bc);
        G.R.drawMesh(gSphereMesh, refl * mat4::translate(vec3(0, 1.15f, 0)) * mat4::scale(vec3(0.56f)), bc);
        if (G.save.settings.hat > 0 && G.save.settings.hat < 6)
            G.R.drawMesh(gHatMeshes[G.save.settings.hat], refl * mat4::translate(vec3(0, 1.38f, 0)), vec4(1, 1, 1, 1));
        G.R.wtext.addText("LEDGE VR — MoonyDev", vec3(0, 3.6f, -8.0f), 0.3f, vec4(1, 1, 1, 1), false);
        G.R.wtext.addText("mirror", vec3(0, 1.0f, -8.0f), 0.2f, vec4(1, 1, 1, 0.8f), false);
    }

    // own body (visible when looking down) — 1.1 squash & stretch
    {
        const vec3 c = kBodyColors[G.save.settings.bodyColor % kBodyColorCount];
        vec4 bc(c.x, c.y, c.z, 1.0f);
        vec3 vel = G.W.velocity();
        float stretch = G.squash + vel.y * 0.018f;          // stretch up when rising
        stretch = std::max(0.7f, std::min(1.35f, stretch));
        float wide = 2.0f - stretch;
        mat4 bt = mat4::translate(G.W.bodyFeet()) * mat4::rotY(G.W.bodyYaw());
        G.R.drawMesh(gBodyMesh,
            bt * mat4::translate(vec3(0, 0.45f, 0)) * mat4::scale(vec3(wide, stretch, wide)), bc);
        if (G.save.settings.hat > 0 && G.save.settings.hat < 6)
            G.R.drawMesh(gHatMeshes[G.save.settings.hat],
                mat4::translate(head + vec3(0, -0.08f, 0)), vec4(1, 1, 1, 1));
    }

    // hands
    if (G.xr.inL.connected) {
        const vec3 c = kBodyColors[G.save.settings.bodyColor % kBodyColorCount];
        vec4 hc(c.x, c.y, c.z, 1.0f);
        // 1.1: hands squeeze slightly while grabbing
        float squeezeL = G.xr.inL.grab * 0.12f, squeezeR = G.xr.inR.grab * 0.12f;
        G.R.drawMesh(gHandMesh, mat4::translate(G.xr.inL.pos)
            * mat4::scale(vec3(1 - squeezeL, 1 - squeezeL, 1 - squeezeL)), hc);
        G.R.drawMesh(gHandMesh, mat4::translate(G.xr.inR.pos)
            * mat4::scale(vec3(1 - squeezeR, 1 - squeezeR, 1 - squeezeR)),
            vec4(hc.x * 1.1f, hc.y * 1.1f, hc.z * 1.1f, 1.0f));
    }

    // remote avatars
    for (auto& av : G.avatars) renderAvatar(av);

    // editor ghost / selection
    if (G.ed.active) {
        G.R.drawGrid(12, 0.5f, G.W.bodyFeet().y - 1.0f + 0.02f, vec4(1, 1, 1, 1));
        if (G.ed.selected >= 0 && G.ed.selected < (int)G.W.level.blocks.size()) {
            const Block& b = G.W.level.blocks[G.ed.selected];
            G.R.drawMesh(gBoxMesh,
                mat4::translate(b.pos) * mat4::rotY(b.rotY * 3.14159265f / 180.0f) * mat4::scale(b.size + vec3(0.06f)),
                vec4(0.4f, 0.9f, 1.0f, 0.35f));
        }
    }

    // goal feedback
    if (G.W.goalReached())
        G.R.wtext.addText("GOAL REACHED!", head + vec3(0, 0.2f, -1.2f), 0.25f, vec4(1.0f, 0.9f, 0.4f, 1));
}

// ---------------------------------------------------------------------------
static float gBodyYawOffset = 0;

static void handleInput() {
    XrBackend& xr = G.xr;
    // three-lines button (left Y) toggles menu
    if (xr.inL.menuBtn && !G.prevMenuBtn) {
        G.menuOpen = !G.menuOpen;
        G.menuAnimT = 0;                       // 1.1 slide-in tween
        jni().sfx(G.menuOpen ? SFX_OPEN : SFX_CLICK);
        xr.haptic(0, 0.4f, 15);
    }
    G.prevMenuBtn = xr.inL.menuBtn;

    // 1.1 emotes: A = dance, B = wave, left stick click = taunt
    if (!G.menuOpen) {
        if (xr.inR.aBtn && !G.prevA) startEmote(EM_DANCE);
        if (xr.inR.bBtn && !G.prevB) startEmote(EM_WAVE);
        if (xr.inL.stickClick && !G.prevStickL) startEmote(EM_TAUNT);
    }
    G.prevA = xr.inR.aBtn;
    G.prevB = xr.inR.bBtn;
    G.prevStickL = xr.inL.stickClick;

    // snap turn (right stick)
    vec2 s = xr.inR.stick;
    if (G.save.settings.snapTurn) {
        if (fabsf(s.x) > 0.7f && !G.snapCooldown) {
            float ang = (s.x > 0 ? -1 : 1) * G.save.settings.snapTurnAngle * 3.14159265f / 180.0f;
            xr.recenter();
            // rotate body position around head? simple: rotate yaw
            gBodyYawOffset += ang;
            G.snapCooldown = true;
        }
        if (fabsf(s.x) < 0.4f) G.snapCooldown = false;
    } else if (fabsf(s.x) > 0.3f) {
        gBodyYawOffset += -s.x * G.save.settings.smoothTurnSpeed * 0.014f * 3.14159265f / 180.0f;
    }
}

int mainLoop(android_app* app) {
    // ---- world + input each frame ----
    XrBackend& xr = G.xr;
    pfApplyAuth();   // wire PlayFab photon token once it arrives
    if (xr.session == XR_NULL_HANDLE || !xr.sessionRunning) {
        // session creation is per-frame until it succeeds (createSession is a
        // no-op once the session exists); pumpEvents then handles READY→begin
        if (xr.session == XR_NULL_HANDLE) xr.createSession();
        xr.pumpEvents();
        return xr.shouldExit ? 1 : 0;
    }

    xr.beginFrame();
    if (!xr.sessionRunning) return xr.shouldExit ? 1 : 0;
    xr.syncInput();

    G.worldClock += 1.0f / 72.0f;

    // movement
    GameWorld::FrameInput in;
    in.grabL = xr.inL.grab > 0.6f;
    in.grabR = xr.inR.grab > 0.6f;
    in.handL = xr.inL.pos;
    in.handR = xr.inR.pos;
    in.headPos = xr.headPos;
    in.headYaw = xr.headRot.yaw() + gBodyYawOffset;
    in.stickL = xr.inL.stick;
    in.stickR = xr.inR.stick;
    in.menuPressed = false;
    in.inMenu = G.menuOpen || G.ed.active;
    in.dt = 1.0f / 72.0f;

    // 1.1 squash & stretch bookkeeping
    G.wasAir = !G.W.onGroundFlag();
    G.prevVelY = G.W.velocity().y;
    G.W.update(in, G.save.settings, G.ed);
    if (G.W.onGroundFlag() && G.wasAir && G.prevVelY < -4.0f) {
        G.squash = std::max(0.68f, 1.0f + G.prevVelY * 0.032f);   // land squash
        jni().sfx(SFX_LAND);
    }
    G.squash += (1.0f - G.squash) * std::min(1.0f, in.dt * 9.0f);

    // 1.1 particles + emotes
    G.ps.update(in.dt);
    G.emoteT += in.dt;
    if (G.emote != EM_NONE && G.emoteT > EMOTE_LEN + 0.2f) G.emote = EM_NONE;

    // editor interactions (right controller ray)
    if (G.ed.active) {
        G.W.editorSelected = G.ed.selected;   // keep world selection in sync
        vec3 origin = xr.inR.aimPos;
        vec3 dir = xr.inR.aimRot.rotate(vec3(0, 0, -1));
        int hit;
        vec3 hp;
        bool click = xr.inR.triggerClick && !G.prevTriggerR;
        if (G.W.raycastBlocks(origin, dir, 12.0f, hit, hp)) {
            if (click) G.ed.selected = hit;
        } else if (click) {
            vec3 ghost = origin + dir * 5.0f;
            ghost.x = roundf(ghost.x / G.ed.gridSize) * G.ed.gridSize;
            ghost.y = roundf(ghost.y / G.ed.gridSize) * G.ed.gridSize;
            ghost.z = roundf(ghost.z / G.ed.gridSize) * G.ed.gridSize;
            G.W.editorAddBlock(ghost, G.ed.palette, G.ed.paletteColor);
            G.ed.selected = (int)G.W.level.blocks.size() - 1;
        }
        if (G.ed.testing) {
            G.W.body.pos = G.W.level.spawnPoint();
            G.W.body.vel = vec3();
            G.ed.testing = false;
        }
        G.prevTriggerR = xr.inR.triggerClick ? 1 : 0;
    }

    // goal → cloud save sync
    if (G.W.goalReached()) {
        G.save.goalsCompleted++;
        G.W.resetGoal();
        G.store.save(G.save);
        if (G.pc.status == PH_InRoom)
            G.pc.setRoomProps({ { "LEDGE_cloudsave", PV::Str_(cloudPackSave(G.save)) } });
    }

    // photon tick
    G.pc.tick();
    sendAvatarState();

    // ---- eyes ----
    for (int e = 0; e < 2; e++) {
        if (!xr.eyeAcquire(e)) continue;
        // build matrices from OpenXR views
        float zn = 0.05f, zf = 300.0f;
        float l = tanf(xr.views[e].fov.angleLeft) * zn, r = tanf(xr.views[e].fov.angleRight) * zn;
        float t = tanf(xr.views[e].fov.angleUp) * zn, bt = tanf(xr.views[e].fov.angleDown) * zn;
        mat4 proj = mat4::identity();
        proj.m[0] = 2 * zn / (r - l); proj.m[5] = 2 * zn / (t - bt);
        proj.m[8] = (r + l) / (r - l); proj.m[9] = (t + bt) / (t - bt);
        proj.m[10] = -(zf + zn) / (zf - zn); proj.m[11] = -1;
        proj.m[14] = -2 * zf * zn / (zf - zn); proj.m[15] = 0;
        quat q(xr.views[e].pose.orientation.x, xr.views[e].pose.orientation.y,
               xr.views[e].pose.orientation.z, xr.views[e].pose.orientation.w);
        vec3 tp(xr.views[e].pose.position.x, xr.views[e].pose.position.y, xr.views[e].pose.position.z);
        mat4 view = mat4::fromQuat(q.conj());
        vec3 rc = q.conj().rotate(tp);
        view.m[12] = -rc.x; view.m[13] = -rc.y; view.m[14] = -rc.z;
        G.R.setViewProjEye(proj.m, view.m, e);
        G.R.wtext.begin();

        if (G.loading) {
            glClearColor(0.72f, 0.89f, 0.93f, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            G.R.wtext.addText("LEDGE VR", vec3(0, 0.4f, -2.0f), 0.35f, vec4(0.2f, 0.3f, 0.4f, 1), true);
            G.R.wtext.addText("loading…", vec3(0, -0.2f, -2.0f), 0.18f, vec4(0.3f, 0.4f, 0.5f, 1), true);
            G.R.wtext.render(proj.m, view.m);
        } else {
            glClearColor(0.0f, 0.0f, 0.0f, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            renderWorld();
            rendererFlushLines(G.R);
            G.R.wtext.render(proj.m, view.m);
        }
        xr.eyeRelease(e);
    }

    // ---- UI quad ----
    if (G.menuOpen && xr.uiAcquire()) {
        uiNewFrame();
        ledui::NewFrame();
        drawMenu();
        uiRenderFrame(ledui::Vec2((float)gUiW, (float)gUiH));
        xr.uiRelease();
        // menu quad: anchored in front of the left controller aim, world-locked
        // 1.1: slide-up + scale tween on open
        G.menuAnimT = std::min(1.0f, G.menuAnimT + 1.0f / 72.0f);
        float e = easeOutCubic(G.menuAnimT / 0.35f > 1 ? 1 : G.menuAnimT / 0.35f);
        quat menuRot = xr.inL.aimRot;
        vec3 fwd = menuRot.rotate(vec3(0, 0, -1));
        vec3 menuPos = xr.inL.aimPos + fwd * 0.55f - vec3(0, (1 - e) * 0.45f, 0);
        float mw = 0.62f * G.save.settings.menuScale * (0.85f + 0.15f * e);
        float mh = 0.62f * G.save.settings.menuScale * (0.85f + 0.15f * e);
        xr.setUiLayer(menuRot, menuPos, mw, mh, true);
    } else {
        xr.setUiLayer(quat(), vec3(), 0.5f, 0.5f, false);
    }

    xr.endFrame();
    return xr.shouldExit ? 1 : 0;
}

// ImGui per-frame bookkeeping (single-frame reuse: NewFrame/EndFrame paired)
static void uiNewFrame() {
    ledui::IO& io = ledui::GetIO();
    io.DisplaySize = ledui::Vec2((float)gUiW, (float)gUiH);
}

// ---------------------------------------------------------------------------
void android_main(android_app* app) {
    app_dummy();
    LOGI("LEDGE VR shell starting");

    jni().init(app->activity->vm, app->activity->clazz);
    G.dataDir = app->activity->internalDataPath ? app->activity->internalDataPath : "/data/local";
    G.store.init(G.dataDir);
    G.store.load(G.save);

    // 1.1: world audio hooks → Java AudioBridge
    G.W.onSound = [](int s) { jni().sfx(s); };
    G.W.onFx = [](int fx, const vec3& pos, const vec3& color) {
        G.ps.spawn(fx, pos, color);
    };

    // PlayFab (Title 1762FA): device sign-in + news + Photon auth token, in background
    pf().init(G.dataDir);
    pf().requestLogin();

    if (!G.xr.init(app->activity->vm, app->activity->clazz, "LEDGE VR")) {
        LOGE("OpenXR init failed");
        return;
    }
    if (!G.R.init()) { LOGE("renderer init failed"); return; }
    uiInit();
    photonInit();
    G.W.loadHub();
    G.status = "ready";

    // 1.1: material pack + background music
    G.tex.init(app->activity->assetManager);
    G.tex.loadAll();
    jni().musicPlay(0);   // Candy Skies while you settle in

    // init is synchronous and done — clear the in-VR loading screen
    // (the mobile shell does the same in its first-frame init block)
    G.loading = false;

    // poll lifecycle events + pump render loop
    while (true) {
        int ident, events;
        android_poll_source* source;
        while ((ident = ALooper_pollAll(0, nullptr, &events, (void**)&source)) >= 0) {
            if (source != nullptr) source->process(app, source);
            if (app->destroyRequested) return;
        }
        int r = mainLoop(app);
        if (r) break;
    }

    G.xr.shutdown();
    jni().shutdown();
    LOGI("LEDGE VR shell exit");
}
