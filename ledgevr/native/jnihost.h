// LEDGE VR — JNI bridge to the Java WebSocket transport (WsBridge).
#pragma once
#include <jni.h>
#include <string>
#include <vector>
#include <mutex>
#include <deque>

namespace lv {

struct WsQueue {
    std::mutex mu;
    std::deque<std::vector<uint8_t>> frames;
    bool closed = false;
    std::string closeReason;
};

class JniHost {
public:
    void init(JavaVM* vm, jobject activity);
    void shutdown();

    bool wsConnect(const std::string& url);          // creates WsBridge, opens socket
    void wsSend(const std::vector<uint8_t>& data);
    void wsClose();

    // drains inbound frames (called from game thread)
    bool wsRecv(std::vector<uint8_t>& out);

    // PlayFab HTTPS bridge (called from native worker threads).
    // headers: "Name:Value" lines separated by '\n'. Returns the raw body;
    // httpCode = 0 means transport failure (body holds the error text).
    std::string httpPost(const std::string& url, const std::string& body,
                         const std::string& headers, int& httpCode);

    // ---- audio (AudioBridge): music playlist + SFX ----
    void audioInit();                       // once, after init()
    void musicPlay(int trackIdx);           // -1 = next track
    void musicNext();
    void musicStop();
    void sfx(int idx);                      // native SFX_* enum order
    void musicVol(float v);
    void sfxVol(float v);
    int musicTrack();                       // current playlist index

    // runtime permission state
    bool micPermission = false;

public:
    JavaVM* vm_ = nullptr;
    jobject activity_ = nullptr;
    jobject bridge_ = nullptr;
    jmethodID mConnect_ = nullptr, mSend_ = nullptr, mClose_ = nullptr;
    jclass httpCls_ = nullptr;
    jmethodID mPost_ = nullptr;
    jclass audioCls_ = nullptr;
    jmethodID mMusicPlay_ = nullptr, mMusicNext_ = nullptr, mMusicStop_ = nullptr,
              mSfx_ = nullptr, mMusicVol_ = nullptr, mSfxVol_ = nullptr;
    WsQueue queue_;
    bool ready_ = false;

    friend void wsNativeOnFrame(JNIEnv*, jclass, jbyteArray, jint);
    friend void wsNativeOnClosed(JNIEnv*, jclass, jstring);
};

JniHost& jni();

// track titles mirrored from AudioBridge (order = playlist order)
inline const char* musicTitle(int i) {
    static const char* titles[] = {
        "Candy Skies (menu)", "Ledge Loops (build)", "Pastel Rush",
        "Cloud Hop", "Neon Climb", "Bubble Bass", "Star Splash"
    };
    return titles[(i % 7 + 7) % 7];
}

} // namespace lv
