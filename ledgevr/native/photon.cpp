// LEDGE VR — Photon Realtime client implementation (Protocol18 over WebSocket)
#include "photon.h"
#include <cstring>
#include <cstdio>
#include <chrono>
#include <algorithm>

namespace lv {

static double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// varint (LEB128) helpers
// ---------------------------------------------------------------------------
static void putVarint(std::vector<uint8_t>& v, uint64_t x) {
    do {
        uint8_t b = x & 0x7F;
        x >>= 7;
        if (x) b |= 0x80;
        v.push_back(b);
    } while (x);
}
static bool getVarint(const uint8_t* d, uint32_t size, uint32_t& o, int64_t& out) {
    uint64_t r = 0; int s = 0;
    while (o < size) {
        uint8_t b = d[o++];
        r |= (uint64_t)(b & 0x7F) << s;
        if (!(b & 0x80)) { out = (int64_t)r; return true; }
        s += 7;
        if (s > 63) return false;
    }
    return false;
}
static void putF32LE(std::vector<uint8_t>& v, float x) {
    uint32_t u; memcpy(&u, &x, 4);
    v.push_back(u & 0xFF); v.push_back((u >> 8) & 0xFF);
    v.push_back((u >> 16) & 0xFF); v.push_back((u >> 24) & 0xFF);
}
static void putF64LE(std::vector<uint8_t>& v, double x) {
    uint64_t u; memcpy(&u, &x, 8);
    for (int s = 0; s < 64; s += 8) v.push_back((uint8_t)(u >> s));
}
static bool getF32LE(const uint8_t* d, uint32_t size, uint32_t& o, float& out) {
    if (o + 4 > size) return false;
    uint32_t u = d[o] | (d[o+1] << 8) | (d[o+2] << 16) | ((uint32_t)d[o+3] << 24);
    memcpy(&out, &u, 4); o += 4; return true;
}
static bool getF64LE(const uint8_t* d, uint32_t size, uint32_t& o, double& out) {
    if (o + 8 > size) return false;
    uint64_t u = 0;
    for (int s = 0; s < 64; s += 8) u |= (uint64_t)d[o + s / 8] << s;
    memcpy(&out, &u, 8); o += 8; return true;
}

// ---------------------------------------------------------------------------
// Protocol18 value serialization
// ---------------------------------------------------------------------------
static bool isVarintType(uint8_t t) {
    return t == GP_Bool || t == GP_Byte || t == GP_Int16 || t == GP_Int32 || t == GP_Int64;
}
static bool isArrayType(uint8_t t) {
    return t == GP_Array || t == GP_BoolArr || t == GP_ByteArr || t == GP_Int16Arr ||
           t == GP_FloatArr || t == GP_DoubleArr || t == GP_StringArr || t == GP_Int32Arr ||
           t == GP_Int64Arr;
}

void p18Serialize(std::vector<uint8_t>& out, const PV& v) {
    switch (v.t) {
    case GP_Null: out.push_back(GP_Null); break;
    case GP_Bool: out.push_back(GP_Bool); putVarint(out, v.b ? 1 : 0); break;
    case GP_Byte: out.push_back(GP_Byte); putVarint(out, (uint64_t)v.i); break;
    case GP_Int16: out.push_back(GP_Int16); putVarint(out, (uint64_t)(int16_t)v.i); break;
    case GP_Int32: out.push_back(GP_Int32); putVarint(out, (uint64_t)(int32_t)v.i); break;
    case GP_Int64: out.push_back(GP_Int64); putVarint(out, (uint64_t)v.i); break;
    case GP_Float: out.push_back(GP_Float); putF32LE(out, v.f); break;
    case GP_Double: out.push_back(GP_Double); putF64LE(out, v.d); break;
    case GP_String: {
        out.push_back(GP_String);
        putVarint(out, v.s.size());
        for (char c : v.s) out.push_back((uint8_t)c);
        break;
    }
    case GP_ByteArray: {
        out.push_back(GP_ByteArray);
        putVarint(out, v.bytes.size());
        out.insert(out.end(), v.bytes.begin(), v.bytes.end());
        break;
    }
    case GP_Hashtable: {
        out.push_back(GP_Hashtable);
        putVarint(out, v.table.size());
        for (auto& kv : v.table) { p18Serialize(out, kv.first); p18Serialize(out, kv.second); }
        break;
    }
    case GP_Dictionary: {
        out.push_back(GP_Dictionary);
        putVarint(out, v.table.size());
        for (auto& kv : v.table) { p18Serialize(out, kv.first); p18Serialize(out, kv.second); }
        break;
    }
    case GP_ObjectArray: {
        out.push_back(GP_ObjectArray);
        putVarint(out, v.arr.size());
        for (auto& e : v.arr) p18Serialize(out, e);
        break;
    }
    case GP_Array: {
        out.push_back(GP_Array);
        putVarint(out, v.arr.size());
        for (auto& e : v.arr) p18Serialize(out, e);
        break;
    }
    case GP_BoolArr: case GP_ByteArr: case GP_Int16Arr: case GP_Int32Arr: case GP_Int64Arr: {
        out.push_back(v.t);
        putVarint(out, v.arr.size());
        for (auto& e : v.arr) putVarint(out, (uint64_t)e.i);
        break;
    }
    case GP_FloatArr: {
        out.push_back(v.t);
        putVarint(out, v.arr.size());
        for (auto& e : v.arr) putF32LE(out, e.f);
        break;
    }
    case GP_DoubleArr: {
        out.push_back(v.t);
        putVarint(out, v.arr.size());
        for (auto& e : v.arr) putF64LE(out, e.d);
        break;
    }
    case GP_StringArr: {
        out.push_back(v.t);
        putVarint(out, v.arr.size());
        for (auto& e : v.arr) {
            putVarint(out, e.s.size());
            for (char c : e.s) out.push_back((uint8_t)c);
        }
        break;
    }
    default: out.push_back(GP_Null); break;
    }
}

bool p18Deserialize(const uint8_t* d, uint32_t size, uint32_t& o, uint8_t type, PV& out) {
    out = PV();
    out.t = type;
    switch (type) {
    case GP_Null: return true;
    case GP_Bool: case GP_Byte: case GP_Int16: case GP_Int32: case GP_Int64: {
        int64_t x;
        if (!getVarint(d, size, o, x)) return false;
        out.i = x; out.b = x != 0;
        return true;
    }
    case GP_Float: return getF32LE(d, size, o, out.f);
    case GP_Double: return getF64LE(d, size, o, out.d);
    case GP_String: {
        int64_t n;
        if (!getVarint(d, size, o, n) || n < 0 || o + n > (int64_t)size) return false;
        out.s.assign((const char*)d + o, (size_t)n);
        o += (uint32_t)n;
        return true;
    }
    case GP_ByteArray: {
        int64_t n;
        if (!getVarint(d, size, o, n) || n < 0 || o + n > (int64_t)size) return false;
        out.bytes.assign(d + o, d + o + n);
        o += (uint32_t)n;
        return true;
    }
    case GP_Hashtable: case GP_Dictionary: {
        int64_t n;
        if (!getVarint(d, size, o, n) || n < 0) return false;
        for (int64_t i = 0; i < n && o < size; i++) {
            if (o >= size) return false;
            uint8_t kt = d[o++];
            PV k;
            if (!p18Deserialize(d, size, o, kt, k)) return false;
            if (o >= size) return false;
            uint8_t vt = d[o++];
            PV v;
            if (!p18Deserialize(d, size, o, vt, v)) return false;
            out.table.push_back({k, v});
        }
        return true;
    }
    case GP_ObjectArray: {
        int64_t n;
        if (!getVarint(d, size, o, n) || n < 0) return false;
        for (int64_t i = 0; i < n && o < size; i++) {
            if (o >= size) return false;
            uint8_t et = d[o++];
            PV e;
            if (!p18Deserialize(d, size, o, et, e)) return false;
            out.arr.push_back(e);
        }
        return true;
    }
    case GP_BoolArr: case GP_ByteArr: case GP_Int16Arr: case GP_Int32Arr: case GP_Int64Arr: {
        int64_t n;
        if (!getVarint(d, size, o, n) || n < 0) return false;
        for (int64_t i = 0; i < n && o < size; i++) {
            int64_t x;
            if (!getVarint(d, size, o, x)) return false;
            PV e; e.t = GP_Int64; e.i = x;
            out.arr.push_back(e);
        }
        return true;
    }
    case GP_FloatArr: {
        int64_t n;
        if (!getVarint(d, size, o, n) || n < 0) return false;
        for (int64_t i = 0; i < n && o < size; i++) {
            float f;
            if (!getF32LE(d, size, o, f)) return false;
            PV e; e.t = GP_Float; e.f = f;
            out.arr.push_back(e);
        }
        return true;
    }
    case GP_DoubleArr: {
        int64_t n;
        if (!getVarint(d, size, o, n) || n < 0) return false;
        for (int64_t i = 0; i < n && o < size; i++) {
            double dd;
            if (!getF64LE(d, size, o, dd)) return false;
            PV e; e.t = GP_Double; e.d = dd;
            out.arr.push_back(e);
        }
        return true;
    }
    case GP_StringArr: {
        int64_t n;
        if (!getVarint(d, size, o, n) || n < 0) return false;
        for (int64_t i = 0; i < n && o < size; i++) {
            int64_t ln;
            if (!getVarint(d, size, o, ln) || ln < 0 || o + ln > (int64_t)size) return false;
            PV e; e.t = GP_String; e.s.assign((const char*)d + o, (size_t)ln);
            o += (uint32_t)ln;
            out.arr.push_back(e);
        }
        return true;
    }
    case GP_Array: {
        int64_t n;
        if (!getVarint(d, size, o, n) || n < 0) return false;
        for (int64_t i = 0; i < n && o < size; i++) {
            if (o >= size) return false;
            uint8_t et = d[o++];
            PV e;
            if (!p18Deserialize(d, size, o, et, e)) return false;
            out.arr.push_back(e);
        }
        return true;
    }
    case GP_Custom: {
        // custom: [customType varint][data ...] — skip defensively
        int64_t cid;
        if (!getVarint(d, size, o, cid)) return false;
        out.i = cid;
        // treat rest as opaque until next parse fails; skip 4 bytes if present
        if (o + 4 <= size) o += 4;
        return true;
    }
    default:
        return false;
    }
}

std::string pvToString(const PV& v) {
    char buf[64];
    switch (v.t) {
    case GP_Null: return "null";
    case GP_Bool: return v.b ? "true" : "false";
    case GP_Byte: case GP_Int16: case GP_Int32: case GP_Int64:
        snprintf(buf, sizeof(buf), "%lld", (long long)v.i); return buf;
    case GP_Float: snprintf(buf, sizeof(buf), "%.2f", v.f); return buf;
    case GP_Double: snprintf(buf, sizeof(buf), "%.2f", v.d); return buf;
    case GP_String: return v.s;
    case GP_ByteArray: snprintf(buf, sizeof(buf), "bytes[%u]", (unsigned)v.bytes.size()); return buf;
    case GP_Hashtable: case GP_Dictionary: {
        std::string r = "{";
        for (auto& kv : v.table) r += pvToString(kv.first) + ":" + pvToString(kv.second) + ", ";
        return r + "}";
    }
    default: {
        std::string r = "[";
        for (auto& e : v.arr) r += pvToString(e) + ", ";
        return r + "]";
    }
    }
}

// ---------------------------------------------------------------------------
PhotonClient::PhotonClient() {}
PhotonClient::~PhotonClient() { disconnect(); }

void PhotonClient::setState(PhotonStatus s, const std::string& detail) {
    status = s;
    if (!detail.empty()) lastError = detail;
    if (onStatus) onStatus((int)s, detail);
}

std::vector<uint8_t> PhotonClient::encodeOp(uint8_t opCode,
        const std::vector<std::pair<uint8_t, PV>>& params) const {
    std::vector<uint8_t> out;
    out.push_back(0xF3);
    out.push_back(0x02);
    out.push_back(opCode);
    out.push_back((uint8_t)params.size());
    for (auto& kv : params) {
        out.push_back(kv.first);
        p18Serialize(out, kv.second);
    }
    return out;
}

void PhotonClient::openTransport(const std::string& hostPart) {
    if (transportClose) transportClose();
    std::string h = hostPart.empty() ? "ns.photonengine.io:19093" : hostPart;
    // wss url with official client's query shape
    char url[512];
    static int sid = 40;
    sid++;
    snprintf(url, sizeof(url),
        "wss://%s/?libversion=6.0.2.0&sid=%d&app=%s",
        h.c_str(), sid, appId.c_str());
    // strip dashes in app id for the URL (gateway expects stripped)
    std::string u = url;
    {
        std::string appParam = "app=" + appId;
        std::string stripped;
        for (char c : appId) if (c != '-') stripped += c;
        size_t p = u.find(appParam);
        if (p != std::string::npos) u.replace(p, appParam.size(), "app=" + stripped);
    }
    if (!transportConnect || !transportConnect(u)) {
        setState(PH_Error, "websocket connect failed");
        return;
    }
    hsPhase = HS_WAIT_HELLO;
    hsSentTime = nowSec();
    setState(PH_Connecting, h);
}

void PhotonClient::connect(const std::string& appId_, const std::string& appVersion_) {
    appId = appId_;
    appVersion = appVersion_;
    onGameServer = false;
    switchToken.clear();
    switchAddress.clear();
    pendingRoom.clear();
    actors.clear(); actorNames.clear(); roomProps.clear(); roomList.clear();
    playerProps.clear();
    actorNr = 0; currentRoom.clear();
    openTransport("");
}

void PhotonClient::sendAuth() {
    std::vector<std::pair<uint8_t, PV>> params;
    params.push_back({ PC_APPLICATION_ID, PV::Str_(appId) });
    params.push_back({ PC_APP_VERSION, PV::Str_(appVersion) });
    if (!userId.empty()) params.push_back({ PC_USER_ID, PV::Str_(userId) });
    if (!onGameServer) params.push_back({ PC_REGION, PV::Str_(region) });
    if (!switchToken.empty()) params.push_back({ PC_SECRET, PV::Str_(switchToken) });
    if (!authData.empty()) {
        if (useCustomAuth) {
            // PlayFab Photon-addon custom auth (verified live end-to-end 2026-11-23:
            // authenticated against the real master server with a real
            // GetPhotonAuthenticationToken): AuthType custom = 0 at key 217, and the
            // GET query string at key 216: username=<PlayFabId>&token=<PhotonToken>.
            // Key 214 is AuthPostData → Photon sends POST, which PlayFab's
            // /photon/authenticate rejects with 404 — do not use it here.
            params.push_back({ PC_AUTH_TYPE, PV::Byte_(0) });
            params.push_back({ PC_AUTH_PARAMS, PV::Str_(authData) });
        } else {
            params.push_back({ PC_AUTH_TYPE, PV::Byte_(255) });
            std::vector<uint8_t> bytes(authData.begin(), authData.end());
            params.push_back({ PC_AUTH_DATA, PV::Bytes_(bytes) });
        }
    }
    hsSentTime = nowSec();
    if (transportSend) transportSend(encodeOp(OP_AUTHENTICATE, params));
}

void PhotonClient::disconnect() {
    if (transportClose) transportClose();
    hsPhase = HS_CLOSED;
    setState(PH_Disconnected, "");
}

void PhotonClient::joinLobby() {
    if (transportSend) transportSend(encodeOp(OP_JOIN_LOBBY, {}));
}

void PhotonClient::joinRoom(const std::string& name, int maxPlayers) {
    if (status != PH_InLobby) { setState(PH_Error, "not in lobby"); return; }
    pendingRoom = name;
    pendingRoomMax = maxPlayers;
    setState(PH_JoiningRoom, name);
    std::vector<std::pair<uint8_t, PV>> params;
    params.push_back({ PC_ROOM_NAME, PV::Str_(name) });
    params.push_back({ PC_JOIN_MODE, PV::Byte_(1) });   // create if not exists
    PV gameProps = PV::Table_();
    gameProps.add(PV::Byte_(PC_MAX_PLAYERS_G), PV::Int_(maxPlayers));
    gameProps.add(PV::Byte_(PC_IS_OPEN_G), PV::Bool_(true));
    gameProps.add(PV::Byte_(PC_IS_VISIBLE_G), PV::Bool_(true));
    params.push_back({ PC_GAME_PROPERTIES, gameProps });
    params.push_back({ PC_LOBBY_TYPE, PV::Byte_(0) });
    if (transportSend) transportSend(encodeOp(OP_JOIN_GAME, params));
}

void PhotonClient::leaveRoom() {
    if (status != PH_InRoom) return;
    setState(PH_Disconnecting, "");
    if (transportSend) transportSend(encodeOp(OP_LEAVE, {}));
}

void PhotonClient::raiseEvent(uint8_t code, const std::vector<std::pair<uint8_t, PV>>& ps, bool reliable) {
    std::vector<std::pair<uint8_t, PV>> params;
    params.push_back({ PC_EVENT_CODE, PV::Byte_(code) });
    for (auto& kv : ps) params.push_back(kv);
    // mark unreliable via receiver group omission; reliable flag not represented
    (void)reliable;
    if (transportSend) transportSend(encodeOp(OP_RAISE_EVENT, params));
}

void PhotonClient::setRoomProps(const std::vector<std::pair<std::string, PV>>& props) {
    PV table = PV::Table_();
    for (auto& kv : props) table.add(PV::Str_(kv.first), kv.second);
    if (transportSend) transportSend(encodeOp(OP_SET_PROPERTIES, {
        { PC_PROPERTIES, table },
        { PC_BROADCAST, PV::Bool_(true) },
    }));
}

void PhotonClient::setPlayerProps(const std::vector<std::pair<std::string, PV>>& props) {
    PV table = PV::Table_();
    for (auto& kv : props) table.add(PV::Str_(kv.first), kv.second);
    if (transportSend) transportSend(encodeOp(OP_SET_PROPERTIES, {
        { PC_PROPERTIES, table },
        { PC_BROADCAST, PV::Bool_(true) },
    }));
}

void PhotonClient::tick() {
    // pull inbound bytes from transport and parse frames
    if (hsPhase == HS_CLOSED) return;
    if (transportRecv) {
        std::vector<uint8_t> data;
        for (int it = 0; it < 32; it++) {
            data.clear();
            transportRecv(data);
            if (data.empty()) break;
            // frames may be concatenated; process all F3 markers
            size_t i = 0;
            while (i < data.size()) {
                if (data[i] == 0xF3) {
                    // one WebSocket message = one frame (transport guarantees)
                    std::vector<uint8_t> frame(data.begin() + i, data.end());
                    processFrame(frame);
                    break;
                }
                i++;
            }
        }
    }
    // handshake watchdog
    if (hsPhase != HS_DONE && hsPhase != HS_CLOSED && nowSec() - hsSentTime > 6.0) {
        setState(PH_Error, "photon handshake timeout");
        hsPhase = HS_CLOSED;
        if (transportClose) transportClose();
    }
}

bool PhotonClient::feedFrame(const std::vector<uint8_t>& frame) {
    processFrame(frame);
    return true;
}

void PhotonClient::processFrame(const std::vector<uint8_t>& f) {
    if (f.size() < 3 || f[0] != 0xF3) return;
    uint8_t mt = f[1];
    const uint8_t* d = f.data();
    uint32_t size = (uint32_t)f.size();
    uint32_t o = 2;
    if (mt == 0x01) { // hello
        if (hsPhase == HS_WAIT_HELLO) {
            hsPhase = HS_WAIT_AUTH;
            sendAuth();
        }
        return;
    }
    if (mt == 0x03) { // operation response
        PhotonResponse resp;
        if (o >= size) return;
        resp.op = d[o++];
        if (o + 2 > size) return;
        resp.returnCode = (int16_t)(d[o] | (d[o+1] << 8));
        o += 2;
        if (o < size) {
            uint8_t dbgType = d[o++];
            if (dbgType == GP_String) {
                int64_t n;
                if (getVarint(d, size, o, n) && n > 0 && o + n <= (int64_t)size) {
                    resp.debug.assign((const char*)d + o, (size_t)n);
                    o += (uint32_t)n;
                }
            } else if (dbgType != GP_Null) {
                PV tmp;
                p18Deserialize(d, size, o, dbgType, tmp);
            }
        }
        if (o < size) {
            int64_t pc;
            if (getVarint(d, size, o, pc)) {
                for (int64_t i = 0; i < pc && o < size; i++) {
                    uint8_t key = d[o++];
                    uint8_t vt = d[o++];
                    PV v;
                    if (!p18Deserialize(d, size, o, vt, v)) break;
                    resp.params.push_back({key, v});
                }
            }
        }
        processResponse(resp);
    } else if (mt == 0x04) { // event
        PhotonEvent ev;
        if (o >= size) return;
        ev.code = d[o++];
        int64_t pc = 0;
        if (o < size) getVarint(d, size, o, pc);
        for (int64_t i = 0; i < pc && o < size; i++) {
            if (o >= size) break;
            uint8_t key = d[o++];
            uint8_t vt = d[o++];
            PV v;
            if (!p18Deserialize(d, size, o, vt, v)) break;
            ev.params.push_back({key, v});
        }
        processEvent(ev);
    } else if (mt == 0x05) { // server parse error / info
        // extract debug string if present
        if (o < size) {
            uint8_t dbgType = d[o++];
            if (dbgType == GP_String) {
                int64_t n;
                if (getVarint(d, size, o, n) && n > 0 && o + n <= (int64_t)size) {
                    lastError.assign((const char*)d + o, (size_t)n);
                    printf("[photon] server error: %s\n", lastError.c_str());
                }
            }
        }
    }
}

void PhotonClient::processResponse(const PhotonResponse& resp) {
    if (onResponse) onResponse(resp);
    if (resp.returnCode != 0) {
        lastError = "op " + std::to_string(resp.op) + " err " + std::to_string(resp.returnCode) +
                    (resp.debug.empty() ? "" : (": " + resp.debug));
        if (resp.op == OP_AUTHENTICATE) {
            setState(PH_Error, "photon auth: " + (resp.debug.empty() ? std::to_string(resp.returnCode) : resp.debug));
            hsPhase = HS_CLOSED;
            if (transportClose) transportClose();
        }
        return;
    }
    switch (resp.op) {
    case OP_AUTHENTICATE: {
        if (!onGameServer) {
            const PV* sec = resp.get(PC_SECRET);
            const PV* addr = resp.get(PC_ADDRESS);
            if (sec && addr && !addr->getStr().empty()) {
                switchToken = sec->getStr();
                switchAddress = addr->getStr();
                hsPhase = HS_CLOSED;
                openTransport(switchAddress);
                // after transport opens, hello → sendAuth() sends secret
                onGameServer = false;
            } else {
                // no address: we're on the final server
                hsPhase = HS_DONE;
                setState(PH_Connected, "master");
                joinLobby();
            }
        } else {
            hsPhase = HS_DONE;
            setState(PH_Connected, "gameserver");
            std::vector<std::pair<uint8_t, PV>> params;
            params.push_back({ PC_ROOM_NAME, PV::Str_(pendingRoom) });
            PV pp = PV::Table_();
            pp.add(PV::Str_("n"), PV::Str_(nick));
            params.push_back({ PC_PLAYER_PROPERTIES, pp });
            params.push_back({ PC_BROADCAST, PV::Bool_(true) });
            if (transportSend) transportSend(encodeOp(OP_JOIN_GAME, params));
            setState(PH_JoiningRoom, pendingRoom);
        }
        break;
    }
    case OP_JOIN_LOBBY:
        setState(PH_InLobby, "");
        break;
    case OP_JOIN_GAME: {
        const PV* an = resp.get(PC_ACTOR_NR);
        if (an) actorNr = (int32_t)an->getInt();
        currentRoom = pendingRoom;
        setState(PH_InRoom, "");
        if (transportSend) transportSend(encodeOp(OP_SET_PROPERTIES, {
            { PC_PROPERTIES, [&]{ PV t = PV::Table_(); t.add(PV::Str_("n"), PV::Str_(nick)); return t; }() },
            { PC_BROADCAST, PV::Bool_(true) },
        }));
        break;
    }
    case OP_LEAVE:
        currentRoom.clear();
        actorNr = 0;
        actors.clear();
        actorNames.clear();
        setState(PH_InLobby, "");
        break;
    default:
        break;
    }
}

void PhotonClient::processEvent(const PhotonEvent& ev) {
    switch (ev.code) {
    case EV_JOIN: {
        const PV* nr = ev.get(PC_ACTOR_NR);
        const PV* ap = ev.get(PC_PLAYER_PROPERTIES);
        const PV* al = ev.get(PC_ACTOR_LIST);
        if (al && al->t == GP_Int32Arr) {
            actors.clear();
            for (auto& e : al->arr) actors.push_back((int32_t)e.i);
        }
        if (nr && ap && ap->t == GP_Hashtable) {
            int32_t a = (int32_t)nr->getInt();
            for (auto& pkv : ap->table) {
                if (pkv.first.getStr() == "n") actorNames[a] = pkv.second.getStr();
                else playerProps[a][pkv.first.getStr()] = pvToString(pkv.second);
            }
            if (a != actorNr && onPlayerJoin) onPlayerJoin(a, actorNames[a]);
        }
        break;
    }
    case EV_LEAVE: {
        const PV* nr = ev.get(PC_ACTOR_NR);
        if (nr) {
            int32_t a = (int32_t)nr->getInt();
            actors.erase(std::remove(actors.begin(), actors.end(), a), actors.end());
            actorNames.erase(a);
            playerProps.erase(a);
            if (onPlayerLeave) onPlayerLeave(a);
        }
        break;
    }
    case EV_PROPERTIES: {
        const PV* target = ev.get(PC_TARGET_ACTOR);
        const PV* props = ev.get(PC_PROPERTIES);
        int32_t t = target ? (int32_t)target->getInt() : 0;
        if (props && props->t == GP_Hashtable) {
            if (t > 0) {
                for (auto& kv : props->table) {
                    if (kv.first.getStr() == "n") actorNames[t] = kv.second.getStr();
                    else playerProps[t][kv.first.getStr()] = pvToString(kv.second);
                }
            } else {
                for (auto& kv : props->table) roomProps[kv.first.getStr()] = pvToString(kv.second);
            }
        }
        if (onPropsChanged) onPropsChanged();
        break;
    }
    case EV_GAME_LIST: roomList.clear(); [[fallthrough]];
    case EV_GAME_LIST_UPDATE: {
        const PV* gl = ev.get(PC_GAME_LIST);
        if (gl && gl->t == GP_Hashtable) {
            for (auto& kv : gl->table) {
                RoomInfo ri;
                ri.name = kv.first.getStr();
                if (kv.second.t == GP_Hashtable) {
                    for (auto& pkv : kv.second.table) {
                        uint8_t k = (uint8_t)pkv.first.getInt();
                        if (k == 254) ri.playerCount = (int32_t)pkv.second.getInt();
                        else if (k == 253) ri.isOpen = pkv.second.getBool();
                        else if (k == 255) ri.maxPlayers = (int32_t)pkv.second.getInt();
                        else if (k == 251) ri.isOpen = false;
                    }
                }
                if (!ri.name.empty()) {
                    bool found = false;
                    for (auto& ex : roomList) if (ex.name == ri.name) { ex = ri; found = true; }
                    if (!found) roomList.push_back(ri);
                }
            }
            if (onRoomList) onRoomList();
        }
        break;
    }
    default:
        if (onEvent) onEvent(ev);
        break;
    }
}

void PhotonClient::handleGameJoin() {}

} // namespace lv
