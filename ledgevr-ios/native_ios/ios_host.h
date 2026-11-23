// LEDGE iOS — platform host: WebSocket transport, HTTPS, audio.
// Same interface as native/jnihost.h so main_ios.cpp + photon.cpp/playfab.cpp
// work unmodified. Implementation: URLSessionWebSocketTask / NSURLSession /
// AVAudioPlayer (music playlist + SFX pool). Files live in the app bundle:
// music/*.m4a, sfx/*.m4a (AAC — AVAudioPlayer can't play OGG).
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <deque>
#include <cstdint>

namespace lv {

struct WsQueue {
    std::mutex mu;
    std::deque<std::vector<uint8_t>> frames;
    bool closed = false;
    std::string closeReason;
};

class IosHost {
public:
    void init();          // audio session + SFX pool
    void shutdown();

    bool wsConnect(const std::string& url);
    void wsSend(const std::vector<uint8_t>& data);
    void wsClose();
    bool wsRecv(std::vector<uint8_t>& out);

    std::string httpPost(const std::string& url, const std::string& body,
                         const std::string& headers, int& httpCode);

    void audioInit();
    void musicPlay(int trackIdx);
    void musicNext();
    void musicStop();
    void sfx(int idx);
    void musicVol(float v);
    void sfxVol(float v);
    int musicTrack();

    bool micPermission = false;

public:
    void* ws_ = nullptr;       // BridgeWS*
    WsQueue queue_;
    bool ready_ = false;
};

IosHost& jni();

// music titles mirrored for the menu (order = playlist order)
inline const char* musicTitle(int i) {
    static const char* titles[] = {
        "Candy Skies (menu)", "Ledge Loops (build)", "Pastel Rush",
        "Cloud Hop", "Neon Climb", "Bubble Bass", "Star Splash"
    };
    return titles[(i % 7 + 7) % 7];
}

} // namespace lv
