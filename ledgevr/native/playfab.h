// LEDGE VR — PlayFab client (Title 1762FA)
// Minimal async REST client for the PlayFab Client API (JSON over HTTPS,
// bridged through Java HttpBridge). Endpoints used:
//   POST /Client/LoginWithCustomID            (device sign-in, CreateAccount)
//   POST /Client/UpdateUserTitleDisplayName
//   POST /Client/UpdateUserData               (cloud save upload)
//   POST /Client/GetUserData                  (cloud save download)
//   POST /Client/UpdatePlayerStatistics       (Goals stat)
//   POST /Client/GetLeaderboard
//   POST /Client/GetTitleNews
//   POST /Client/GetPhotonAuthenticationToken (Photon custom-auth token)
#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace lv {

struct PfLeaderRow {
    std::string name;
    std::string id;
    int32_t value = 0;
};
struct PfNewsItem {
    std::string title;
    std::string body;
};

// Render-thread view of PlayFab state (copied under the mutex).
struct PfSnapshot {
    bool loggedIn = false;
    bool busy = false;
    bool newlyCreated = false;
    bool hasCloudSave = false;
    std::string customId, playFabId, displayName, sessionTicket, photonToken;
    std::string lastError, status;
    std::string cloudSave;            // last downloaded cloud-save blob
    std::vector<PfLeaderRow> leaderboard;
    std::vector<PfNewsItem> news;
};

class PlayFabClient {
public:
    static const char* TITLE_ID() { return "1762FA"; }
    static std::string endpoint();

    void init(const std::string& storeDir);   // loads or creates the device custom id
    std::string customId() const;             // persisted device id (thread-safe copy)

    // All actions run on a worker thread; returns false if one is already running.
    bool requestLogin();                                  // login + news + photon token
    bool requestCloudSave(const std::string& saveJson);   // upload local save blob
    bool requestCloudLoad();                              // download save blob (snapshot().cloudSave)
    bool requestUpdateStat(const std::string& stat, int32_t value);
    bool requestLeaderboard(const std::string& stat, int maxRows = 10);
    bool requestNews();
    bool requestPhotonToken(const std::string& photonAppId);
    bool requestSetName(const std::string& name);

    PfSnapshot snapshot() const;   // render thread: copy of current state
    bool busy() const { return busy_; }

private:
    struct St {
        bool loggedIn = false, newlyCreated = false, hasCloudSave = false;
        std::string customId, playFabId, displayName, sessionTicket, photonToken;
        std::string lastError, status;
        std::vector<PfLeaderRow> leaderboard;
        std::vector<PfNewsItem> news;
        std::string cloudSave;
    };

    bool run(std::function<void(St&)> job, const char* busyMsg);
    bool httpJson(const std::string& path, const std::string& body, const std::string& ticket,
                  int& code, std::string& responseJson);

    mutable std::mutex mu_;
    St st_;
    std::atomic<bool> busy_{ false };
    std::string storeDir_;
};

PlayFabClient& pf();

} // namespace lv
