// LEDGE VR — Photon Realtime client (WebSocket GpBinaryV18, Protocol18/GpType)
// Wire format reverse-engineered + LIVE-VERIFIED against ns.photonengine.io:
//   frame:  [0xF3][msgType]  (1=hello, 2=op request, 3=op response, 4=event, 5=server error)
//   op req: [0xF3][02][opCode][paramCount][ [key u8][GpType][value] ... ]
//   op rsp: [0xF3][03][opCode][returnCode u16 LE][debug (GpType+len)][paramCount][params...]
//   event:  [0xF3][04][eventCode][paramCount][params...]
//   GpType: Null=0, Bool=2, Byte=3, Int16=4, Float=5, Double=6, String=7, ByteArray=8,
//           Int32=9, Int64=10, Custom=19, Dictionary=20, Hashtable=21, ObjectArray=23,
//           typed arrays = 0x40|base (bool[]=66, byte[]=67, short[]=68, double[]=70,
//           string[]=71, int[]=73, long[]=74), Array=64
//   integers (Bool/Byte/Int16/Int32/Int64) are LEB128 varints; Float=4B LE, Double=8B LE;
//   string = varint len + utf8; hashtable = varint count + (key,value) typed pairs.
// Transport: WebSocket (subprotocol GpBinaryV18) bridged to Java (SSLSocket) via JNI.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <deque>

namespace lv {

enum GpType : uint8_t {
    GP_Null = 0, GP_Bool = 2, GP_Byte = 3, GP_Int16 = 4, GP_Float = 5, GP_Double = 6,
    GP_String = 7, GP_ByteArray = 8, GP_Int32 = 9, GP_Int64 = 10, GP_Custom = 19,
    GP_Dictionary = 20, GP_Hashtable = 21, GP_ObjectArray = 23,
    GP_Array = 64, GP_BoolArr = 66, GP_ByteArr = 67, GP_Int16Arr = 68, GP_FloatArr = 69,
    GP_DoubleArr = 70, GP_StringArr = 71, GP_Int32Arr = 73, GP_Int64Arr = 74,
};

struct PV {
    uint8_t t = GP_Null;
    bool b = false;
    int64_t i = 0;              // Bool/Byte/Int16/Int32/Int64 value
    float f = 0;
    double d = 0;
    std::string s;
    std::vector<uint8_t> bytes;
    std::vector<std::pair<PV, PV>> table;
    std::vector<PV> arr;

    PV() {}
    static PV Null_() { return PV(); }
    static PV Bool_(bool v) { PV p; p.t = GP_Bool; p.b = v; p.i = v ? 1 : 0; return p; }
    static PV Byte_(int64_t v) { PV p; p.t = GP_Byte; p.i = v; return p; }
    static PV Int_(int64_t v) { PV p; p.t = GP_Int32; p.i = v; return p; }
    static PV Long_(int64_t v) { PV p; p.t = GP_Int64; p.i = v; return p; }
    static PV Float_(float v) { PV p; p.t = GP_Float; p.f = v; return p; }
    static PV Double_(double v) { PV p; p.t = GP_Double; p.d = v; return p; }
    static PV Str_(const std::string& v) { PV p; p.t = GP_String; p.s = v; return p; }
    static PV Bytes_(const std::vector<uint8_t>& v) { PV p; p.t = GP_ByteArray; p.bytes = v; return p; }
    static PV Table_() { PV p; p.t = GP_Hashtable; return p; }
    static PV Array_() { PV p; p.t = GP_Array; return p; }
    static PV ObjArr_() { PV p; p.t = GP_ObjectArray; return p; }
    static PV StringArr_(const std::vector<std::string>& v) {
        PV p; p.t = GP_StringArr;
        for (auto& x : v) p.arr.push_back(Str_(x));
        return p;
    }
    void add(const PV& k, const PV& v) { table.push_back({k, v}); }
    void push(const PV& v) { arr.push_back(v); }

    int64_t getInt() const { return i; }
    float getFloat() const { return t == GP_Float ? f : (t == GP_Double ? (float)d : (float)i); }
    bool getBool() const { return t == GP_Bool ? b : i != 0; }
    const std::string& getStr() const { static const std::string empty; return t == GP_String ? s : empty; }
    const std::vector<uint8_t>& getBytes() const { static const std::vector<uint8_t> empty; return t == GP_ByteArray ? bytes : empty; }
    const std::vector<PV>& getArr() const { static const std::vector<PV> empty; return arr; }
};

void p18Serialize(std::vector<uint8_t>& out, const PV& v);
bool p18Deserialize(const uint8_t* d, uint32_t size, uint32_t& o, uint8_t type, PV& out);
std::string pvToString(const PV& v);

struct PhotonEvent {
    uint8_t code = 0;
    std::vector<std::pair<uint8_t, PV>> params;
    const PV* get(uint8_t key) const {
        for (auto& kv : params) if (kv.first == key) return &kv.second;
        return nullptr;
    }
};
struct PhotonResponse {
    uint8_t op = 0;
    int16_t returnCode = 0;
    std::string debug;
    std::vector<std::pair<uint8_t, PV>> params;
    const PV* get(uint8_t key) const {
        for (auto& kv : params) if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

enum PhotonStatus {
    PH_Disconnected = 0, PH_Connecting, PH_Connected, PH_Authenticating,
    PH_InLobby, PH_JoiningRoom, PH_InRoom, PH_Error, PH_Disconnecting, PH_SwitchingServer
};
static const char* photonStatusName(int s) {
    static const char* names[] = { "Disconnected", "Connecting", "Connected", "Authenticating",
        "In lobby", "Joining room", "In room", "Error", "Leaving", "Switching server" };
    return names[s];
}

struct RoomInfo {
    std::string name;
    int32_t playerCount = 0;
    int32_t maxPlayers = 0;
    bool isOpen = true;
};

class PhotonClient {
public:
    std::function<void(const PhotonEvent&)> onEvent;
    std::function<void(const PhotonResponse&)> onResponse;
    std::function<void(int status, const std::string& detail)> onStatus;
    std::function<void(int32_t actorNr, const std::string& nick)> onPlayerJoin;
    std::function<void(int32_t actorNr)> onPlayerLeave;
    std::function<void()> onRoomList;
    std::function<void()> onPropsChanged;

    std::string appId, appVersion, userId, nick;
    std::string region = "eu";
    std::string authData;         // custom auth payload (JSON for PlayFab, param 214)
    bool useCustomAuth = false;   // true → AuthType=2 (custom) with authData as string

    PhotonStatus status = PH_Disconnected;
    std::string lastError;
    int32_t actorNr = 0;
    std::string currentRoom;
    std::vector<int32_t> actors;
    std::map<int32_t, std::string> actorNames;
    std::map<std::string, std::string> roomProps;
    std::map<int32_t, std::map<std::string, std::string>> playerProps;
    std::vector<RoomInfo> roomList;
    std::vector<std::pair<std::string, std::string>> regionList; // name, url

    PhotonClient();
    ~PhotonClient();

    // transport injection: returns bytes to send, called from C++ side
    std::function<bool(const std::string& url)> transportConnect;   // open wss url (subprotocol GpBinaryV18)
    std::function<void(const std::vector<uint8_t>&)> transportSend;
    std::function<void()> transportClose;
    // C++ polls inbound bytes delivered by the transport
    std::function<void(std::vector<uint8_t>& out)> transportRecv;

    void connect(const std::string& appId_, const std::string& appVersion_ = "1.0");
    void disconnect();
    void joinLobby();
    void joinRoom(const std::string& name, int maxPlayers = 16);
    void leaveRoom();
    void raiseEvent(uint8_t code, const std::vector<std::pair<uint8_t, PV>>& params, bool reliable = true);
    void setRoomProps(const std::vector<std::pair<std::string, PV>>& props);
    void setPlayerProps(const std::vector<std::pair<std::string, PV>>& props);
    void tick();

    // direct frame access for tests
    std::vector<uint8_t> encodeOp(uint8_t opCode, const std::vector<std::pair<uint8_t, PV>>& params) const;
    bool feedFrame(const std::vector<uint8_t>& frame);   // parse one F3 frame (tests)

private:
    enum HSPhase { HS_WAIT_HELLO = 0, HS_WAIT_AUTH, HS_DONE, HS_CLOSED };
    HSPhase hsPhase = HS_CLOSED;
    double hsSentTime = 0;
    std::string switchToken, switchAddress;
    bool onGameServer = false;
    std::string pendingRoom;
    int pendingRoomMax = 16;

    void setState(PhotonStatus s, const std::string& detail = "");
    void openTransport(const std::string& hostPart);
    void sendOp(uint8_t opCode, const std::vector<std::pair<uint8_t, PV>>& params);
    void sendAuth();
    void processFrame(const std::vector<uint8_t>& f);
    void processEvent(const PhotonEvent& ev);
    void processResponse(const PhotonResponse& resp);
    void handleGameJoin();
};

// op codes (LoadBalancing, verified from Photon SDK source + live server)
enum : uint8_t {
    OP_AUTHENTICATE   = 230,
    OP_JOIN_LOBBY     = 229,
    OP_JOIN_GAME      = 226,
    OP_LEAVE          = 254,
    OP_RAISE_EVENT    = 253,
    OP_SET_PROPERTIES = 252,
    OP_GET_PROPERTIES = 251,
    OP_GET_REGIONS    = 220,
};
// parameter codes
enum : uint8_t {
    PC_ROOM_NAME       = 255,
    PC_MAX_PLAYERS_G   = 255,
    PC_IS_OPEN_G       = 253,
    PC_IS_VISIBLE_G    = 254,
    PC_ACTOR_NR        = 254,
    PC_TARGET_ACTOR    = 253,
    PC_ACTOR_LIST      = 252,
    PC_BROADCAST       = 250,
    PC_GAME_LIST       = 222,
    PC_GAME_PROPERTIES = 248,
    PC_PLAYER_PROPERTIES = 249,
    PC_PROPERTIES      = 251,
    PC_CUSTOM_CONTENT  = 245,
    PC_EVENT_CODE      = 244,
    PC_RECEIVER_GROUP  = 246,
    PC_JOIN_MODE       = 215,
    PC_CLEANUP_CACHE   = 241,
    PC_PUBLISH_USERID  = 239,
    PC_IS_INACTIVE     = 233,
    PC_PEER_COUNT      = 229,
    PC_GAME_COUNT      = 228,
    PC_MASTER_PEER_COUNT = 227,
    PC_USER_ID         = 225,
    PC_APPLICATION_ID  = 224,
    PC_SECRET          = 221,
    PC_APP_VERSION     = 220,
    PC_ADDRESS         = 230,
    PC_REGION          = 210,
    PC_LOBBY_TYPE      = 212,
    PC_NICK_NAME       = 202,
    PC_MASTER_CLIENT   = 203,
    PC_AUTH_DATA       = 214,   // AuthPostData → Photon sends POST (PlayFab 404s it)
    PC_AUTH_PARAMS     = 216,   // GET query string (verified live vs PlayFab addon)
    PC_AUTH_TYPE       = 217,   // value: 0=custom (verified), 255=none
};
// event codes (LoadBalancing)
enum : uint8_t {
    EV_JOIN              = 255,
    EV_LEAVE             = 254,
    EV_PROPERTIES        = 253,
    EV_ERROR_INFO        = 251,
    EV_GAME_LIST         = 230,
    EV_GAME_LIST_UPDATE  = 229,
    EV_APP_STATS         = 226,
};

} // namespace lv
