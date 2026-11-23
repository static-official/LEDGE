// LEDGE iOS shim — jnihost.h is included by shared engine files (photon.cpp,
// playfab.cpp). This TU defines lv::jni() (JniHost&) for them and delegates
// every method to the real iOS host via lv::ioshim free functions.
#include "../../ledgevr/native/jnihost.h"

namespace lv {
namespace ioshim {
bool wsConnect(const std::string& url);
void wsSend(const std::vector<uint8_t>& d);
void wsClose();
bool wsRecv(std::vector<uint8_t>& out);
std::string httpPost(const std::string& url, const std::string& body,
                     const std::string& headers, int& code);
void audioInit();
void musicPlay(int i);
void musicNext();
void musicStop();
void sfx(int i);
void musicVol(float v);
void sfxVol(float v);
int musicTrack();
} // namespace ioshim

static JniHost g_jh;
JniHost& jni() { return g_jh; }

void JniHost::init(JavaVM* vm, jobject activity) { (void)vm; (void)activity; }
void JniHost::shutdown() {}

bool JniHost::wsConnect(const std::string& url) { return ioshim::wsConnect(url); }
void JniHost::wsSend(const std::vector<uint8_t>& data) { ioshim::wsSend(data); }
void JniHost::wsClose() { ioshim::wsClose(); }
bool JniHost::wsRecv(std::vector<uint8_t>& out) { return ioshim::wsRecv(out); }
std::string JniHost::httpPost(const std::string& url, const std::string& body,
                              const std::string& headers, int& httpCode) {
    return ioshim::httpPost(url, body, headers, httpCode);
}

void JniHost::audioInit() { ioshim::audioInit(); }
void JniHost::musicPlay(int trackIdx) {
    if (trackIdx < 0) ioshim::musicNext();          // Android: -1 = next track
    else ioshim::musicPlay(trackIdx);
}
void JniHost::musicNext() { ioshim::musicNext(); }
void JniHost::musicStop() { ioshim::musicStop(); }
void JniHost::sfx(int idx) { ioshim::sfx(idx); }
void JniHost::musicVol(float v) { ioshim::musicVol(v); }
void JniHost::sfxVol(float v) { ioshim::sfxVol(v); }
int JniHost::musicTrack() { return ioshim::musicTrack(); }

} // namespace lv
