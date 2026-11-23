// LEDGE VR — PlayFab client implementation (Title 1762FA)
#include "playfab.h"
#include "jnihost.h"
#include "log.h"
#include "world.h"      // jsonEscape
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace lv {

// LEDGE VR realtime application id (used for GetPhotonAuthenticationToken)
static const char* PHOTON_APP_ID_REALTIME = "15f80a97-41e4-4c8b-a887-9b8de6d103ad";

static PlayFabClient g_pf;
PlayFabClient& pf() { return g_pf; }

std::string PlayFabClient::endpoint() {
    char buf[96];
    snprintf(buf, sizeof(buf), "https://%s.playfabapi.com", TITLE_ID());
    std::string e = buf;
    for (auto& c : e) c = (char)tolower((unsigned char)c);
    return e;
}

// ---------------------------------------------------------------------------
// tiny JSON helpers (self-contained; PlayFab responses are flat enough)
// ---------------------------------------------------------------------------
namespace {

void jskipWs(const char*& p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
}

void jappendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) out += (char)cp;
    else if (cp < 0x800) {
        out += (char)(0xC0 | (cp >> 6));
        out += (char)(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    } else {
        out += (char)(0xF0 | (cp >> 18));
        out += (char)(0x80 | ((cp >> 12) & 0x3F));
        out += (char)(0x80 | ((cp >> 6) & 0x3F));
        out += (char)(0x80 | (cp & 0x3F));
    }
}

// Reads the value starting at *p (ws already skipped): strings come back
// unescaped; numbers/bools as raw tokens; objects/arrays as raw slices.
std::string jrawValue(const char*& p, const char* end) {
    if (p >= end) return "";
    if (*p == '"') {
        p++;
        std::string out;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                p++;
                switch (*p) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'u': {
                    if (p + 4 < end) {
                        char hex[5] = { p[1], p[2], p[3], p[4], 0 };
                        uint32_t cp = (uint32_t)strtoul(hex, nullptr, 16);
                        p += 4;
                        // surrogate pair
                        if (cp >= 0xD800 && cp <= 0xDBFF && p + 6 < end &&
                            p[1] == '\\' && p[2] == 'u') {
                            char hex2[5] = { p[3], p[4], p[5], p[6], 0 };
                            uint32_t lo = (uint32_t)strtoul(hex2, nullptr, 16);
                            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                p += 6;
                            }
                        }
                        jappendUtf8(out, cp);
                    }
                    break;
                }
                default: out += *p; break;
                }
                p++;
            } else {
                out += *p++;
            }
        }
        if (p < end) p++;   // closing quote
        return out;
    }
    if (*p == '[' || *p == '{') {
        // bracket-matched raw slice (string-aware), includes the delimiters
        const char* start = p;
        int depth = 0;
        bool inStr = false;
        while (p < end) {
            char c = *p;
            if (inStr) {
                if (c == '\\') p++;
                else if (c == '"') inStr = false;
            } else if (c == '"') inStr = true;
            else if (c == '[' || c == '{') depth++;
            else if (c == ']' || c == '}') {
                depth--;
                if (!depth) { p++; break; }
            }
            p++;
        }
        return std::string(start, p);
    }
    const char* start = p;
    while (p < end && *p != ',' && *p != ']' && *p != '}') p++;
    return std::string(start, p);
}

// Position OF the ':' belonging to `key` (first occurrence at/after `from`).
size_t jkeyColonPos(const std::string& json, const char* key, size_t from) {
    std::string pat = std::string("\"") + key + "\"";
    size_t pos = json.find(pat, from);
    const char* base = json.c_str();
    const char* end = base + json.size();
    while (pos != std::string::npos) {
        const char* p = base + pos + pat.size();
        jskipWs(p, end);
        if (p < end && *p == ':') return pos + pat.size();
        pos = json.find(pat, pos + 1);
    }
    return std::string::npos;
}

// Get the value of `key` in `json` (unescaped string / raw token / raw slice).
std::string jget(const std::string& json, const char* key, size_t from = 0) {
    size_t cp = jkeyColonPos(json, key, from);
    if (cp == std::string::npos) return "";
    const char* p = json.c_str() + cp;
    const char* end = json.c_str() + json.size();
    jskipWs(p, end);
    if (p < end && *p == ':') p++;
    jskipWs(p, end);
    return jrawValue(p, end);
}

int jnum(const std::string& s, int def = 0) {
    if (s.empty()) return def;
    return atoi(s.c_str());
}

// Iterate a JSON array slice, calling fn with each raw {...} element.
void jforEachObj(const std::string& arr, const std::function<void(const std::string&)>& fn) {
    size_t from = 0;
    while (from < arr.size()) {
        size_t start = arr.find('{', from);
        if (start == std::string::npos) break;
        int depth = 0;
        bool inStr = false;
        size_t i = start;
        for (; i < arr.size(); i++) {
            char c = arr[i];
            if (inStr) {
                if (c == '\\') i++;
                else if (c == '"') inStr = false;
            } else if (c == '"') inStr = true;
            else if (c == '{') depth++;
            else if (c == '}') { depth--; if (!depth) break; }
        }
        if (i >= arr.size()) break;
        fn(arr.substr(start, i - start + 1));
        from = i + 1;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// init / custom id persistence
// ---------------------------------------------------------------------------
void PlayFabClient::init(const std::string& storeDir) {
    storeDir_ = storeDir;
    std::lock_guard<std::mutex> lk(mu_);
    if (st_.customId.empty()) {
        FILE* f = fopen((storeDir + "/pf_custom_id.txt").c_str(), "rb");
        if (f) {
            char buf[64] = {};
            size_t n = fread(buf, 1, sizeof(buf) - 1, f);
            fclose(f);
            if (n > 4) st_.customId = buf;
        }
        if (st_.customId.empty()) {
            // device identifier: 16 hex chars from /dev/urandom
            unsigned char r[8] = {};
            FILE* u = fopen("/dev/urandom", "rb");
            if (u) { size_t n = fread(r, 1, 8, u); (void)n; fclose(u); }
            char buf[40];
            snprintf(buf, sizeof(buf), "ledge%02x%02x%02x%02x%02x%02x%02x%02x",
                     r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
            st_.customId = buf;
        }
        FILE* w = fopen((storeDir + "/pf_custom_id.txt").c_str(), "wb");
        if (w) { fwrite(st_.customId.c_str(), 1, st_.customId.size(), w); fclose(w); }
    }
}

std::string PlayFabClient::customId() const {
    std::lock_guard<std::mutex> lk(mu_);
    return st_.customId;
}

PfSnapshot PlayFabClient::snapshot() const {
    std::lock_guard<std::mutex> lk(mu_);
    PfSnapshot s;
    s.loggedIn = st_.loggedIn;
    s.busy = busy_.load();
    s.newlyCreated = st_.newlyCreated;
    s.hasCloudSave = st_.hasCloudSave;
    s.customId = st_.customId;
    s.playFabId = st_.playFabId;
    s.displayName = st_.displayName;
    s.sessionTicket = st_.sessionTicket;
    s.photonToken = st_.photonToken;
    s.lastError = st_.lastError;
    s.status = st_.status;
    s.cloudSave = st_.cloudSave;
    s.leaderboard = st_.leaderboard;
    s.news = st_.news;
    return s;
}

// ---------------------------------------------------------------------------
// transport
// ---------------------------------------------------------------------------
bool PlayFabClient::httpJson(const std::string& path, const std::string& body,
                             const std::string& ticket, int& code, std::string& responseJson) {
    std::string headers;
    if (!ticket.empty()) headers = "X-Authorization:" + ticket;
    int httpCode = 0;
    std::string resp = jni().httpPost(endpoint() + path, body, headers, httpCode);
    code = httpCode;
    responseJson = resp;
    return httpCode >= 200 && httpCode < 300;
}

// Worker runner: one job at a time (busy_ CAS). The job mutates a COPY of the
// state without holding the lock (long HTTP ops), results are merged back.
bool PlayFabClient::run(std::function<void(St&)> job, const char* busyMsg) {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) return false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        st_.lastError.clear();
        st_.status = busyMsg;
    }
    std::thread([this, job]() {
        St tmp;
        try {
            { std::lock_guard<std::mutex> lk(mu_); tmp = st_; }
            job(tmp);
        } catch (const std::exception& e) {
            tmp.lastError = e.what();
        }
        { std::lock_guard<std::mutex> lk(mu_); st_ = tmp; st_.status.clear(); }
        busy_.store(false);
    }).detach();
    return true;
}

// ---------------------------------------------------------------------------
// API jobs (executed on the worker thread against a state copy)
// ---------------------------------------------------------------------------
bool PlayFabClient::requestLogin() {
    return run([this](St& st) {
        if (st.customId.empty()) {
            init(storeDir_);
            st.customId = customId();
        }
        char body[256];
        snprintf(body, sizeof(body),
                 "{\"TitleId\":\"%s\",\"CustomId\":\"%s\",\"CreateAccount\":true,"
                 "\"InfoRequestParameters\":{\"GetPlayerProfile\":true}}",
                 TITLE_ID(), st.customId.c_str());
        int code = 0;
        std::string resp;
        if (!httpJson("/Client/LoginWithCustomID", body, "", code, resp)) {
            st.lastError = "login failed (HTTP " + std::to_string(code) + "): " +
                           resp.substr(0, 200);
            return;
        }
        if (code != 200) {
            st.lastError = "login: " + jget(resp, "errorMessage") +
                           " (code " + jget(resp, "errorCode") + ")";
            return;
        }
        st.sessionTicket = jget(resp, "SessionTicket");
        st.playFabId = jget(resp, "PlayFabId");
        st.newlyCreated = jget(resp, "NewlyCreated") == "true";
        if (!st.sessionTicket.empty() && !st.playFabId.empty()) st.loggedIn = true;
        st.displayName = jget(jget(resp, "PlayerProfile"), "DisplayName");
        st.status = "signed in";
        st.lastError.clear();

        // auto-chain: title news + photon token so multiplayer can use custom auth
        int c2 = 0;
        std::string r2;
        if (httpJson("/Client/GetTitleNews", "{\"Count\":5}", st.sessionTicket, c2, r2) && c2 == 200) {
            st.news.clear();
            jforEachObj(jget(r2, "News"), [&](const std::string& item) {
                PfNewsItem n;
                n.title = jget(item, "Title");
                n.body = jget(item, "Body");
                if (!n.title.empty()) st.news.push_back(n);
            });
        }
        std::string r3;
        char b3[160];
        snprintf(b3, sizeof(b3), "{\"PhotonApplicationId\":\"%s\"}", PHOTON_APP_ID_REALTIME);
        if (httpJson("/Client/GetPhotonAuthenticationToken", b3, st.sessionTicket, c2, r3) && c2 == 200) {
            st.photonToken = jget(r3, "Token");
            st.status = st.photonToken.empty() ? "signed in (no photon token)"
                                               : "signed in (photon token ok)";
        }
    }, "signing in…");
}

bool PlayFabClient::requestCloudSave(const std::string& saveJson) {
    return run([this, saveJson](St& st) {
        if (!st.loggedIn) { st.lastError = "not signed in"; return; }
        // PlayFab UserData values must be strings — escape the save blob.
        std::string escaped = jsonEscape(saveJson);
        std::string body = "{\"Data\":{\"ledgevr_save\":\"" + escaped + "\"}}";
        int code = 0;
        std::string resp;
        if (!httpJson("/Client/UpdateUserData", body, st.sessionTicket, code, resp) || code != 200) {
            st.lastError = "cloud save: " + jget(resp, "errorMessage");
            return;
        }
        st.hasCloudSave = true;
        st.status = "cloud save uploaded (" + std::to_string(saveJson.size()) + " B)";
    }, "uploading cloud save…");
}

bool PlayFabClient::requestCloudLoad() {
    return run([this](St& st) {
        if (!st.loggedIn) { st.lastError = "not signed in"; return; }
        int code = 0;
        std::string resp;
        if (!httpJson("/Client/GetUserData", "{\"Keys\":[\"ledgevr_save\"]}",
                      st.sessionTicket, code, resp) || code != 200) {
            st.lastError = "cloud load: " + jget(resp, "errorMessage");
            return;
        }
        std::string val = jget(resp, "Value");
        st.cloudSave = val;
        st.hasCloudSave = !val.empty();
        st.status = val.empty() ? "no cloud save yet" : "cloud save downloaded";
    }, "downloading cloud save…");
}

bool PlayFabClient::requestUpdateStat(const std::string& stat, int32_t value) {
    return run([this, stat, value](St& st) {
        if (!st.loggedIn) { st.lastError = "not signed in"; return; }
        char body[160];
        snprintf(body, sizeof(body),
                 "{\"Statistics\":[{\"StatisticName\":\"%s\",\"Value\":%d}]}",
                 stat.c_str(), value);
        int code = 0;
        std::string resp;
        if (!httpJson("/Client/UpdatePlayerStatistics", body, st.sessionTicket, code, resp) || code != 200) {
            st.lastError = "stat update: " + jget(resp, "errorMessage");
            return;
        }
        st.status = stat + " updated to " + std::to_string(value);
    }, "updating stat…");
}

bool PlayFabClient::requestLeaderboard(const std::string& stat, int maxRows) {
    return run([this, stat, maxRows](St& st) {
        if (!st.loggedIn) { st.lastError = "not signed in"; return; }
        char body[160];
        snprintf(body, sizeof(body),
                 "{\"StatisticName\":\"%s\",\"MaxResultsCount\":%d}", stat.c_str(), maxRows);
        int code = 0;
        std::string resp;
        if (!httpJson("/Client/GetLeaderboard", body, st.sessionTicket, code, resp) || code != 200) {
            st.lastError = "leaderboard: " + jget(resp, "errorMessage");
            return;
        }
        st.leaderboard.clear();
        jforEachObj(jget(resp, "Records"), [&](const std::string& row) {
            PfLeaderRow r;
            r.name = jget(row, "DisplayName");
            r.id = jget(row, "PlayFabId");
            r.value = jnum(jget(row, "StatValue"));
            if (r.name.empty()) r.name = "(unnamed)";
            st.leaderboard.push_back(r);
        });
        char msg[64];
        snprintf(msg, sizeof(msg), "leaderboard: %d rows", (int)st.leaderboard.size());
        st.status = msg;
    }, "fetching leaderboard…");
}

bool PlayFabClient::requestNews() {
    return run([this](St& st) {
        if (!st.loggedIn) { st.lastError = "not signed in"; return; }
        int code = 0;
        std::string resp;
        if (!httpJson("/Client/GetTitleNews", "{\"Count\":5}", st.sessionTicket, code, resp) || code != 200) {
            st.lastError = "news: " + jget(resp, "errorMessage");
            return;
        }
        st.news.clear();
        jforEachObj(jget(resp, "News"), [&](const std::string& item) {
            PfNewsItem n;
            n.title = jget(item, "Title");
            n.body = jget(item, "Body");
            if (!n.title.empty()) st.news.push_back(n);
        });
        st.status = "news refreshed";
    }, "fetching news…");
}

bool PlayFabClient::requestPhotonToken(const std::string& photonAppId) {
    return run([this, photonAppId](St& st) {
        if (!st.loggedIn) { st.lastError = "not signed in"; return; }
        char body[160];
        snprintf(body, sizeof(body), "{\"PhotonApplicationId\":\"%s\"}", photonAppId.c_str());
        int code = 0;
        std::string resp;
        if (!httpJson("/Client/GetPhotonAuthenticationToken", body, st.sessionTicket, code, resp) || code != 200) {
            st.lastError = "photon token: " + jget(resp, "errorMessage");
            return;
        }
        st.photonToken = jget(resp, "Token");
        st.status = st.photonToken.empty() ? "no token returned" : "photon token ok";
    }, "requesting photon token…");
}

bool PlayFabClient::requestSetName(const std::string& name) {
    return run([this, name](St& st) {
        if (!st.loggedIn) { st.lastError = "not signed in"; return; }
        std::string body = "{\"DisplayName\":\"" + jsonEscape(name) + "\"}";
        int code = 0;
        std::string resp;
        if (!httpJson("/Client/UpdateUserTitleDisplayName", body, st.sessionTicket, code, resp) || code != 200) {
            st.lastError = "set name: " + jget(resp, "errorMessage");
            return;
        }
        st.displayName = name;
        st.status = "display name set to " + name;
    }, "setting name…");
}

} // namespace lv
