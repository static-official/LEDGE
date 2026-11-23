// LEDGE VR — JNI bridge implementation
#include "jnihost.h"
#include "log.h"
#include <cstdlib>

namespace lv {

static JniHost g_host;

JniHost& jni() { return g_host; }

static std::string jstrToStd(JNIEnv* env, jstring j) {
    if (!j) return "";
    const char* c = env->GetStringUTFChars(j, nullptr);
    std::string s = c ? c : "";
    env->ReleaseStringUTFChars(j, c);
    return s;
}

void JniHost::init(JavaVM* vm, jobject activity) {
    vm_ = vm;
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return;
    activity_ = env->NewGlobalRef(activity);

    jclass cls = env->FindClass("com/moonydev/ledgevr/WsBridge");
    if (!cls) { LOGE("WsBridge class not found"); return; }

    mConnect_ = env->GetMethodID(cls, "connect", "(Ljava/lang/String;)Z");
    mSend_ = env->GetMethodID(cls, "send", "([B)V");
    mClose_ = env->GetMethodID(cls, "close", "()V");
    jmethodID ctor = env->GetMethodID(cls, "<init>", "()V");
    if (!mConnect_ || !mSend_ || !mClose_ || !ctor) { LOGE("WsBridge methods missing"); return; }

    jobject local = env->NewObject(cls, ctor);
    bridge_ = env->NewGlobalRef(local);
    env->DeleteLocalRef(local);
    env->DeleteLocalRef(cls);

    // PlayFab HTTPS bridge (static methods, no instance needed)
    jclass hcls = env->FindClass("com/moonydev/ledgevr/HttpBridge");
    if (hcls) {
        mPost_ = env->GetStaticMethodID(hcls, "post",
            "(Ljava/lang/String;[BLjava/lang/String;)[B");
        httpCls_ = (jclass)env->NewGlobalRef(hcls);
        env->DeleteLocalRef(hcls);
        if (!mPost_) LOGE("HttpBridge.post not found");
    } else {
        LOGE("HttpBridge class not found");
    }
    ready_ = true;
    LOGI("JNI host ready");
    audioInit();
}

// ---------------------------------------------------------------------------
// audio bridge: static methods on com.moonydev.ledgevr.AudioBridge
// ---------------------------------------------------------------------------
static JNIEnv* audioEnv(JniHost* h, bool& attached) {
    JNIEnv* env = nullptr;
    attached = false;
    if (!h->vm_) return nullptr;
    if (h->vm_->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (h->vm_->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
        attached = true;
    }
    return env;
}

void JniHost::audioInit() {
    bool attached = false;
    JNIEnv* env = audioEnv(this, attached);
    if (!env) return;
    jclass cls = env->FindClass("com/moonydev/ledgevr/AudioBridge");
    if (!cls) {
        if (attached) vm_->DetachCurrentThread();
        LOGE("AudioBridge class not found");
        return;
    }
    audioCls_ = (jclass)env->NewGlobalRef(cls);
    env->DeleteLocalRef(cls);
    mMusicPlay_ = env->GetStaticMethodID(audioCls_, "playMusic", "(I)V");
    mMusicNext_ = env->GetStaticMethodID(audioCls_, "nextTrack", "()V");
    mMusicStop_ = env->GetStaticMethodID(audioCls_, "stopMusic", "()V");
    mSfx_ = env->GetStaticMethodID(audioCls_, "playSfx", "(I)V");
    mMusicVol_ = env->GetStaticMethodID(audioCls_, "setMusicVol", "(F)V");
    mSfxVol_ = env->GetStaticMethodID(audioCls_, "setSfxVol", "(F)V");
    jmethodID mInit = env->GetStaticMethodID(audioCls_, "init", "(Landroid/content/Context;)V");
    if (mInit && activity_)
        env->CallStaticVoidMethod(audioCls_, mInit, activity_);
    if (attached) vm_->DetachCurrentThread();
    LOGI("audio bridge ready");
}

void JniHost::musicPlay(int trackIdx) {
    if (!audioCls_ || !mMusicPlay_) return;
    bool attached = false;
    JNIEnv* env = audioEnv(this, attached);
    if (!env) return;
    env->CallStaticVoidMethod(audioCls_, mMusicPlay_, (jint)trackIdx);
    if (attached) vm_->DetachCurrentThread();
}

void JniHost::musicNext() {
    if (!audioCls_ || !mMusicNext_) return;
    bool attached = false;
    JNIEnv* env = audioEnv(this, attached);
    if (!env) return;
    env->CallStaticVoidMethod(audioCls_, mMusicNext_);
    if (attached) vm_->DetachCurrentThread();
}

void JniHost::musicStop() {
    if (!audioCls_ || !mMusicStop_) return;
    bool attached = false;
    JNIEnv* env = audioEnv(this, attached);
    if (!env) return;
    env->CallStaticVoidMethod(audioCls_, mMusicStop_);
    if (attached) vm_->DetachCurrentThread();
}

void JniHost::sfx(int idx) {
    if (!audioCls_ || !mSfx_) return;
    bool attached = false;
    JNIEnv* env = audioEnv(this, attached);
    if (!env) return;
    env->CallStaticVoidMethod(audioCls_, mSfx_, (jint)idx);
    if (attached) vm_->DetachCurrentThread();
}

void JniHost::musicVol(float v) {
    if (!audioCls_ || !mMusicVol_) return;
    bool attached = false;
    JNIEnv* env = audioEnv(this, attached);
    if (!env) return;
    env->CallStaticVoidMethod(audioCls_, mMusicVol_, (jfloat)v);
    if (attached) vm_->DetachCurrentThread();
}

void JniHost::sfxVol(float v) {
    if (!audioCls_ || !mSfxVol_) return;
    bool attached = false;
    JNIEnv* env = audioEnv(this, attached);
    if (!env) return;
    env->CallStaticVoidMethod(audioCls_, mSfxVol_, (jfloat)v);
    if (attached) vm_->DetachCurrentThread();
}

int JniHost::musicTrack() {
    if (!audioCls_) return 0;
    bool attached = false;
    JNIEnv* env = audioEnv(this, attached);
    if (!env) return 0;
    static jmethodID mGet = env->GetStaticMethodID(audioCls_, "getTrack", "()I");
    int r = mGet ? (int)env->CallStaticIntMethod(audioCls_, mGet) : 0;
    if (attached) vm_->DetachCurrentThread();
    return r;
}

void JniHost::shutdown() {
    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm_ && vm_->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (vm_->AttachCurrentThread(&env, nullptr) == JNI_OK) attached = true;
    }
    wsClose();
    if (env && activity_) env->DeleteGlobalRef(activity_);
    if (env && bridge_) env->DeleteGlobalRef(bridge_);
    if (env && httpCls_) env->DeleteGlobalRef(httpCls_);
    if (env && audioCls_) env->DeleteGlobalRef(audioCls_);
    activity_ = nullptr; bridge_ = nullptr; httpCls_ = nullptr; audioCls_ = nullptr;
    if (attached && vm_) vm_->DetachCurrentThread();
}

bool JniHost::wsConnect(const std::string& url) {
    if (!ready_) return false;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm_->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (vm_->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
        attached = true;
    }
    jstring jurl = env->NewStringUTF(url.c_str());
    jboolean ok = env->CallBooleanMethod(bridge_, mConnect_, jurl);
    env->DeleteLocalRef(jurl);
    if (attached) vm_->DetachCurrentThread();
    {
        std::lock_guard<std::mutex> lk(queue_.mu);
        queue_.frames.clear();
        queue_.closed = false;
    }
    return ok == JNI_TRUE;
}

void JniHost::wsSend(const std::vector<uint8_t>& data) {
    if (!ready_ || data.empty()) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm_->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (vm_->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    jbyteArray arr = env->NewByteArray((jsize)data.size());
    env->SetByteArrayRegion(arr, 0, (jsize)data.size(), (const jbyte*)data.data());
    env->CallVoidMethod(bridge_, mSend_, arr);
    env->DeleteLocalRef(arr);
    if (attached) vm_->DetachCurrentThread();
}

void JniHost::wsClose() {
    if (!ready_) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm_->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (vm_->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    }
    env->CallVoidMethod(bridge_, mClose_);
    if (attached) vm_->DetachCurrentThread();
}

bool JniHost::wsRecv(std::vector<uint8_t>& out) {
    std::lock_guard<std::mutex> lk(queue_.mu);
    if (queue_.frames.empty()) return false;
    out = std::move(queue_.frames.front());
    queue_.frames.pop_front();
    return true;
}

std::string JniHost::httpPost(const std::string& url, const std::string& body,
                              const std::string& headers, int& httpCode) {
    httpCode = 0;
    if (!ready_ || !mPost_) return "HTTP bridge not ready";
    JNIEnv* env = nullptr;
    bool attached = false;
    if (vm_->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        if (vm_->AttachCurrentThread(&env, nullptr) != JNI_OK) return "JNI attach failed";
        attached = true;
    }
    std::string out;
    jstring ju = env->NewStringUTF(url.c_str());
    jstring jh = env->NewStringUTF(headers.c_str());
    jbyteArray jb = env->NewByteArray((jsize)body.size());
    env->SetByteArrayRegion(jb, 0, (jsize)body.size(), (const jbyte*)body.data());
    jbyteArray jr = (jbyteArray)env->CallStaticObjectMethod(httpCls_, mPost_, ju, jb, jh);
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        out = "java exception in HttpBridge.post";
    } else if (jr) {
        jsize n = env->GetArrayLength(jr);
        out.resize((size_t)n);
        env->GetByteArrayRegion(jr, 0, n, (jbyte*)out.data());
    }
    if (jr) env->DeleteLocalRef(jr);
    env->DeleteLocalRef(jb);
    env->DeleteLocalRef(ju);
    env->DeleteLocalRef(jh);
    if (attached) vm_->DetachCurrentThread();

    // response format: "<code>\n<body>"
    size_t nl = out.find('\n');
    if (nl != std::string::npos) {
        httpCode = atoi(out.substr(0, nl).c_str());
        out = out.substr(nl + 1);
    }
    return out;
}

// ---- Java callbacks ----
extern "C" JNIEXPORT void JNICALL
Java_com_moonydev_ledgevr_WsBridge_nativeOnFrame(JNIEnv* env, jclass, jbyteArray data, jint len) {
    jbyte* p = env->GetByteArrayElements(data, nullptr);
    std::vector<uint8_t> frame((uint8_t*)p, (uint8_t*)p + len);
    env->ReleaseByteArrayElements(data, p, JNI_ABORT);
    std::lock_guard<std::mutex> lk(g_host.queue_.mu);
    g_host.queue_.frames.push_back(std::move(frame));
}

extern "C" JNIEXPORT void JNICALL
Java_com_moonydev_ledgevr_WsBridge_nativeOnClosed(JNIEnv* env, jclass, jstring reason) {
    std::lock_guard<std::mutex> lk(g_host.queue_.mu);
    g_host.queue_.closed = true;
    g_host.queue_.closeReason = jstrToStd(env, reason);
}

} // namespace lv
