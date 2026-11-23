// Host unit test for the PlayFab JSON helpers (includes playfab.cpp directly).
// Shims: android/log.h (via /tmp/pfshim) and a stub JNI host.
#include <string>
#include <cstdio>
#include <vector>
#include <cctype>
#include <cstdlib>
#include "jnihost.h"

namespace lv {
// jnihost.h declares the class; provide bodies for what playfab.cpp references.
static JniHost g_shim;
JniHost& jni() { return g_shim; }
std::string JniHost::httpPost(const std::string&, const std::string&, const std::string&, int& code) {
    code = 0;
    return "";
}

// minimal jsonEscape definition for this TU (declared in world.h)
static std::string jsonEscape(const std::string& s) {
    std::string r;
    for (char c : s) {
        if (c == '"' || c == '\\') { r += '\\'; r += c; }
        else if (c == '\n') r += "\\n";
        else r += c;
    }
    return r;
}
} // namespace lv

#include "playfab.cpp"   // brings jget/jrawValue/jforEachObj + PlayFabClient

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) printf("PASS %s\n", msg); else { printf("FAIL %s\n", msg); failures++; } } while (0)

int main() {
    using namespace lv;
    // 1. successful login response (compact, as PlayFab returns)
    std::string login = R"({"code":200,"status":"OK","data":{"SessionTicket":"4FGE2BB2XYZ~abcDEF","PlayFabId":"AB12CD34","NewlyCreated":true,"PlayerProfile":{"DisplayName":"Zed"}}})";
    CHECK(jget(login, "SessionTicket") == "4FGE2BB2XYZ~abcDEF", "SessionTicket");
    CHECK(jget(login, "PlayFabId") == "AB12CD34", "PlayFabId");
    CHECK(jget(login, "NewlyCreated") == "true", "NewlyCreated");
    CHECK(jget(jget(login, "PlayerProfile"), "DisplayName") == "Zed", "DisplayName");

    // 2. error response
    std::string err = R"({"code":400,"status":"BadRequest","error":"PlayerCreationDisabled","errorCode":22000,"errorMessage":"Player creations have been disabled for this API."})";
    CHECK(jget(err, "errorMessage") == "Player creations have been disabled for this API.", "errorMessage");
    CHECK(jget(err, "errorCode") == "22000", "errorCode");

    // 3. leaderboard rows
    std::string lb = R"({"code":200,"data":{"Records":[{"DisplayName":"Alice","PlayFabId":"P1","StatValue":42},{"DisplayName":"Bob","PlayFabId":"P2","StatValue":7}]}})";
    std::vector<PfLeaderRow> rows;
    jforEachObj(jget(lb, "Records"), [&](const std::string& row) {
        PfLeaderRow r;
        r.name = jget(row, "DisplayName");
        r.value = jnum(jget(row, "StatValue"));
        rows.push_back(r);
    });
    CHECK(rows.size() == 2 && rows[0].name == "Alice" && rows[0].value == 42 &&
          rows[1].name == "Bob" && rows[1].value == 7, "leaderboard rows");

    // 4. cloud save value (JSON-in-string, escaped)
    std::string ud = "{\"code\":200,\"data\":{\"Data\":{\"ledgevr_save\":{\"Value\":\"{\\\"name\\\":\\\"a\\\\\\\"b\\\"}\"}}}}";
    std::string val = jget(ud, "Value");
    CHECK(val == "{\"name\":\"a\\\"b\"}", "escaped cloud save value");

    // 5. title news array
    std::string news = R"({"code":200,"data":{"News":[{"Title":"v1.0 live","Body":"Welcome!"},{"Title":"Patch","Body":"Fixes"}]}})";
    std::vector<std::string> titles;
    jforEachObj(jget(news, "News"), [&](const std::string& item) {
        titles.push_back(jget(item, "Title"));
    });
    CHECK(titles.size() == 2 && titles[0] == "v1.0 live" && titles[1] == "Patch", "news items");

    // 6. endpoint construction
    CHECK(PlayFabClient::endpoint() == "https://1762fa.playfabapi.com", "endpoint");

    printf(failures ? "\n%d FAILURES\n" : "\nALL PF JSON TESTS PASSED\n", failures);
    return failures ? 1 : 0;
}
