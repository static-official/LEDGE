// LEDGE VR — host-side Photon protocol test harness.
// Verifies the wire protocol against the LIVE Photon cloud using the game's
// AppIds: connect → init(op255) → authenticate(op230) → join lobby(op229)
// → join room(op225) → raise event(op253) → leave(op254).
#include "../ledgevr/native/photon.h"
#include <cstdio>
#include <thread>
#include <chrono>
#include <csignal>

using namespace lv;

static bool g_done = false;
static void sigint(int) { g_done = true; }

int main(int argc, char** argv) {
    const char* appId = argc > 1 ? argv[1] : "15f80a97-41e4-4c8b-a887-9b8de6d103ad";
    const char* room  = argc > 2 ? argv[2] : "LEDGE-TEST";

    signal(SIGINT, sigint);

    PhotonClient pc;
    pc.host = "ns.photonengine.io";
    pc.port = 5055;
    pc.nick = "HarnessBot";

    int stage = 0;
    pc.onStatus = [&](int s, const std::string& d) {
        printf("[status] %s %s\n", photonStatusName(s), d.c_str());
        if (s == PH_InLobby) { printf(">>> joining room %s\n", room); pc.joinRoom(room, 16); stage = 1; }
    };
    pc.onResponse = [&](const PhotonResponse& r) {
        printf("[resp] op=%u ret=%d dbg='%s' params=%u\n", r.op, r.returnCode, r.debug.c_str(), (unsigned)r.params.size());
        for (auto& kv : r.params) printf("       p%u = %s\n", kv.first, pvToString(kv.second).c_str());
    };
    pc.onEvent = [&](const PhotonEvent& e) {
        printf("[event] code=%u params=%u\n", e.code, (unsigned)e.params.size());
        for (auto& kv : e.params) printf("       p%u = %s\n", kv.first, pvToString(kv.second).c_str());
    };
    pc.onPlayerJoin = [&](int32_t a, const std::string& uid, const std::string& nick) {
        printf("[player] join actor=%d uid=%s nick=%s\n", a, uid.c_str(), nick.c_str());
    };
    pc.onPlayerLeave = [&](int32_t a) {
        printf("[player] leave actor=%d\n", a);
    };

    pc.connect(appId, "1.0");
    printf("connecting to %s:%d app=%s...\n", pc.host.c_str(), pc.port, appId);

    auto t0 = std::chrono::steady_clock::now();
    while (!g_done) {
        pc.tick();
        auto el = std::chrono::steady_clock::now() - t0;
        if (std::chrono::duration_cast<std::chrono::seconds>(el).count() > 25) break;
        if (pc.status == PH_Error) break;
        if (stage == 1 && pc.status == PH_InRoom) {
            stage = 2;
            printf(">>> in room! raising test event + setting room props\n");
            std::vector<uint8_t> blob; for (int i = 0; i < 8; i++) blob.push_back(i);
            pc.raiseEvent(60, { { 248, PV::Str_("hello from LEDGE VR harness") } }, true);
            pc.setRoomProps({ { "L", PV::Str_("hub") } });
            pc.setPlayerProps({ { "c", PV::Int_(5) } });
        }
        if (stage == 2) {
            // linger 6 seconds, then leave + disconnect
            static auto tInRoom = std::chrono::steady_clock::now();
            auto inRoomEl = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - tInRoom).count();
            if (inRoomEl > 6) { printf(">>> leaving room\n"); pc.leaveRoom(); stage = 3; tInRoom = std::chrono::steady_clock::now(); }
            if (stage == 3) { break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    printf("final status: %s  lastError='%s'\n", photonStatusName(pc.status), pc.lastError.c_str());
    printf("actors: %zu  room: '%s'\n", pc.actors.size(), pc.currentRoom.c_str());
    pc.disconnect();
    return (pc.status == PH_Disconnected || pc.status == PH_InLobby || pc.status == PH_InRoom) ? 0 : 1;
}
