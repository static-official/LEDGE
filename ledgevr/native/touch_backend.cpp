// LEDGE Mobile — touch backend implementation.
// EGL + GLES 3.2/3.0 fallback, multi-touch joystick/look/HUD routing.
#include "touch_backend.h"
#include "log.h"
#include <cstring>
#include <cmath>
#include <cstdlib>

namespace lv {

// ---------------------------------------------------------------------------
// GL helpers (same signatures as the VR build's xr_backend.cpp versions).
// ---------------------------------------------------------------------------
static GLuint gMaxEsVersion = 3;   // 3 = ES 3.0, 2 = ES 3.2 present

static bool es32Available() {
    const GLubyte* v = glGetString(GL_VERSION);
    if (!v) return false;
    // e.g. "OpenGL ES 3.2 vendor..." — accept 3.2+
    const char* s = (const char*)v;
    if (strncmp(s, "OpenGL ES ", 10) != 0) return true;   // unknown: assume yes
    int maj = atoi(s + 10);
    int minr = 0;
    const char* dot = strchr(s + 10, '.');
    if (dot) minr = atoi(dot + 1);
    return maj > 3 || (maj == 3 && minr >= 2);
}

// Mobile devices without GLES 3.2 get the same shaders re-targeted to ES 3.0.
static std::string shimVersion(const char* src) {
    if (gMaxEsVersion >= 32) return std::string(src);
    std::string s(src);
    const std::string from = "#version 320 es";
    const std::string to = "#version 300 es";
    size_t p = s.find(from);
    if (p != std::string::npos) s.replace(p, from.size(), to);
    return s;
}

GLuint lvCompileShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    std::string shimmed = shimVersion(src);
    const char* c = shimmed.c_str();
    glShaderSource(sh, 1, &c, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        LOGE("shader compile failed: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint lvLinkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        LOGE("program link failed: %s", log);
        glDeleteProgram(p);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

GLuint lvMakeFbo(GLuint tex, int w, int h) {
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glViewport(0, 0, w, h);
    return fbo;
}

// ---------------------------------------------------------------------------
// EGL lifecycle — display + context are created once and kept across
// surface loss so all GL objects (shaders, meshes, atlas) stay valid.
// ---------------------------------------------------------------------------
bool TouchBackend::initContext() {
    if (ctxReady) return true;
    eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(eglDisplay, nullptr, nullptr);
    eglBindAPI(EGL_OPENGL_ES_API);

    const EGLint cfgAttr[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE };
    EGLConfig cfg = nullptr;
    EGLint num = 0;
    if (!eglChooseConfig(eglDisplay, cfgAttr, &cfg, 1, &num) || num < 1) {
        LOGE("EGL: no config");
        return false;
    }
    eglConfig = cfg;

    const EGLint ctxAttr[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    eglContext = eglCreateContext(eglDisplay, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (eglContext == EGL_NO_CONTEXT) {
        LOGE("EGL: context failed 0x%x", eglGetError());
        return false;
    }
    ctxReady = true;
    LOGI("mobile EGL context ready");
    return true;
}

bool TouchBackend::attachSurface(ANativeWindow* win) {
    if (!ctxReady) return false;
    if (eglSurface != EGL_NO_SURFACE) detachSurface();
    window = win;
    ANativeWindow_acquire(win);
    w = ANativeWindow_getWidth(win);
    h = ANativeWindow_getHeight(win);

    const EGLint surfAttr[] = { EGL_NONE };
    eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, win, surfAttr);
    if (eglSurface == EGL_NO_SURFACE) {
        LOGE("EGL: surface failed 0x%x", eglGetError());
        return false;
    }
    makeCurrent();

    if (w == 0 || h == 0) {
        w = ANativeWindow_getWidth(win);
        h = ANativeWindow_getHeight(win);
    }
    gMaxEsVersion = es32Available() ? 32 : 30;
    es32 = gMaxEsVersion >= 32;
    LOGI("mobile EGL surface %dx%d es=%s", w, h, es32 ? "3.2" : "3.0-shim");
    layoutHud();
    surfReady = true;
    return true;
}

void TouchBackend::detachSurface() {
    if (eglDisplay != EGL_NO_DISPLAY && eglSurface != EGL_NO_SURFACE)
        eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (eglDisplay != EGL_NO_DISPLAY && eglSurface != EGL_NO_SURFACE)
        eglDestroySurface(eglDisplay, eglSurface);
    if (window) ANativeWindow_release(window);
    eglSurface = EGL_NO_SURFACE;
    window = nullptr;
    surfReady = false;
}

void TouchBackend::shutdown() {
    detachSurface();
    if (eglDisplay != EGL_NO_DISPLAY) {
        if (eglContext != EGL_NO_CONTEXT) eglDestroyContext(eglDisplay, eglContext);
        eglTerminate(eglDisplay);
    }
    eglDisplay = EGL_NO_DISPLAY;
    eglContext = EGL_NO_CONTEXT;
    ctxReady = false;
}

void TouchBackend::makeCurrent() {
    eglMakeCurrent(eglDisplay, eglSurface, eglSurface, eglContext);
}

bool TouchBackend::swap() {
    return eglSwapBuffers(eglDisplay, eglSurface);
}

// ---------------------------------------------------------------------------
// HUD layout — rects in y-down screen pixels
// ---------------------------------------------------------------------------
void TouchBackend::layoutHud() {
    if (w <= 0 || h <= 0) return;
    float s = h / 720.0f;                 // scale controls with resolution
    if (s < 0.75f) s = 0.75f;
    float bw = 96 * s, bh = 56 * s;       // standard button
    float pad = 14 * s;
    float big = 108 * s;                  // jump button
    for (int i = 0; i < HB_COUNT; i++) btnRect[i] = { -1, -1, 0, 0 };

    // top-left: menu
    btnRect[HB_MENU]   = { pad, pad, bw * 0.8f, bh };
    // top-right: quick lobby + editor
    btnRect[HB_LOBBY]  = { w - pad - bw, pad, bw, bh };
    btnRect[HB_EDITOR] = { w - pad - bw * 2 - pad, pad, bw, bh };
    // bottom-right cluster: grab L / grab R / jump
    btnRect[HB_JUMP]   = { w - pad - big, h - pad - big, big, big };
    btnRect[HB_GRABR]  = { w - pad - big - pad - bw * 1.1f, h - pad - bh - 10 * s, bw * 1.1f, bh };
    btnRect[HB_GRABL]  = { w - pad - big - pad - bw * 1.1f - pad - bw * 1.1f,
                           h - pad - bh - 10 * s, bw * 1.1f, bh };
    // editor strip (center, above joystick zone)
    float cx = w * 0.5f;
    btnRect[HB_PLACE]  = { cx - bw - pad * 0.5f, h - pad - bh * 3.2f, bw, bh };
    btnRect[HB_SELECT] = { cx + pad * 0.5f, h - pad - bh * 3.2f, bw, bh };
}

static bool rectHit(const TouchBackend::Rect& r, float x, float y) {
    return r.w > 0 && x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
}

// ---------------------------------------------------------------------------
// touch routing
// ---------------------------------------------------------------------------
bool TouchBackend::touchEvent(AInputEvent* ev) {
    int32_t type = AInputEvent_getType(ev);
    if (type != AINPUT_EVENT_TYPE_MOTION) {
        // keys: back button toggles the menu instead of exiting
        if (AKeyEvent_getKeyCode(ev) == AKEYCODE_BACK &&
            AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_DOWN) {
            menuOpen = !menuOpen;
            return true;
        }
        return false;
    }

    int32_t action = AMotionEvent_getAction(ev);
    int32_t actionMasked = action & AMOTION_EVENT_ACTION_MASK;
    size_t ptrIdx = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                    >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

    if (menuOpen) {
        // single-mouse emulation for ImGui
        size_t cnt = AMotionEvent_getPointerCount(ev);
        size_t use = (actionMasked == AMOTION_EVENT_ACTION_POINTER_DOWN ||
                      actionMasked == AMOTION_EVENT_ACTION_POINTER_UP) ? ptrIdx : 0;
        float x = AMotionEvent_getX(ev, use), y = AMotionEvent_getY(ev, use);
        mouseX = x; mouseY = y;
        if (actionMasked == AMOTION_EVENT_ACTION_DOWN ||
            actionMasked == AMOTION_EVENT_ACTION_POINTER_DOWN)
            mouseDown = true;
        if (actionMasked == AMOTION_EVENT_ACTION_UP ||
            actionMasked == AMOTION_EVENT_ACTION_POINTER_UP ||
            actionMasked == AMOTION_EVENT_ACTION_CANCEL)
            mouseDown = false;
        (void)cnt;
        return true;
    }

    switch (actionMasked) {
    case AMOTION_EVENT_ACTION_DOWN:
    case AMOTION_EVENT_ACTION_POINTER_DOWN: {
        float x = AMotionEvent_getX(ev, ptrIdx), y = AMotionEvent_getY(ev, ptrIdx);
        int32_t id = AMotionEvent_getPointerId(ev, ptrIdx);
        if (id < 0 || id > 7) return true;

        // HUD buttons first
        HudBtn hit = HB_NONE;
        for (int i = 1; i < HB_COUNT; i++)
            if (rectHit(btnRect[i], x, y)) { hit = (HudBtn)i; break; }
        if (hit != HB_NONE) {
            role[id] = PR_UI;
            btnOfId[id] = hit;
            btnDown[hit] = true;
            if (hit == HB_GRABL) grabL = true;
            if (hit == HB_GRABR) grabR = true;
            if (hit == HB_JUMP)  jumpDown = true;
            if (hit == HB_PLACE) placePressed = true;
            if (hit == HB_SELECT) selectPressed = true;
            return true;
        }

        // left-bottom region = joystick, everything else = look
        if (x < w * 0.42f && y > h * 0.42f) {
            role[id] = PR_JOY;
            joyOriginX = x; joyOriginY = y;
            stickL = vec2();
        } else {
            role[id] = PR_LOOK;
            lastLookX = x; lastLookY = y;
            lookDragging = true;
        }
        return true;
    }
    case AMOTION_EVENT_ACTION_MOVE: {
        size_t cnt = AMotionEvent_getPointerCount(ev);
        float joyDX = 0, joyDY = 0;
        for (size_t i = 0; i < cnt; i++) {
            int32_t id = AMotionEvent_getPointerId(ev, i);
            if (id < 0 || id > 7 || role[id] == PR_NONE || role[id] == PR_UI) continue;
            float x = AMotionEvent_getX(ev, i), y = AMotionEvent_getY(ev, i);
            if (role[id] == PR_JOY) {
                float r = h * 0.14f;
                if (r < 60) r = 60;
                float dx = (x - joyOriginX) / r, dy = (y - joyOriginY) / r;
                float l = sqrtf(dx * dx + dy * dy);
                if (l > 1.0f) { dx /= l; dy /= l; }
                stickL = vec2(dx, dy);
                joyDX = dx; joyDY = dy;
            } else if (role[id] == PR_LOOK) {
                float dx = x - lastLookX, dy = y - lastLookY;
                lastLookX = x; lastLookY = y;
                if (grabL || grabR) {
                    // grabbing: drag pulls the world (body delta handled by main)
                    pullDX += dx; pullDY += dy;
                } else {
                    float sens = 0.0055f;
                    yaw   -= dx * sens;
                    pitch -= dy * sens;
                    if (pitch > 1.45f) pitch = 1.45f;
                    if (pitch < -1.45f) pitch = -1.45f;
                }
            }
        }
        return true;
    }
    case AMOTION_EVENT_ACTION_UP:
    case AMOTION_EVENT_ACTION_POINTER_UP:
    case AMOTION_EVENT_ACTION_CANCEL: {
        int32_t id = AMotionEvent_getPointerId(ev, actionMasked == AMOTION_EVENT_ACTION_CANCEL ? 0 : ptrIdx);
        if (actionMasked == AMOTION_EVENT_ACTION_CANCEL) {
            for (auto& rr : role) rr = PR_NONE;
            stickL = vec2();
            lookDragging = false;
            for (int i = 0; i < HB_COUNT; i++) btnDown[i] = false;
            for (auto& b : btnOfId) b = HB_NONE;
            grabL = grabR = jumpDown = false;
            return true;
        }
        if (id < 0 || id > 7) return true;
        if (role[id] == PR_UI) {
            HudBtn b = btnOfId[id];
            btnOfId[id] = HB_NONE;
            if (b != HB_NONE && btnDown[b]) {
                btnDown[b] = false;
                if (b == HB_GRABL) grabL = false;
                if (b == HB_GRABR) grabR = false;
                if (b == HB_JUMP)  jumpDown = false;
            }
        } else if (role[id] == PR_JOY) {
            stickL = vec2();
        } else if (role[id] == PR_LOOK) {
            lookDragging = false;
        }
        role[id] = PR_NONE;
        return true;
    }
    }
    return true;
}

void TouchBackend::aimRay(vec3 eye, vec3& dir) const {
    (void)eye;
    dir = vec3(-sinf(yaw) * cosf(pitch), sinf(pitch), -cosf(yaw) * cosf(pitch));
    dir = norm(dir);
}

} // namespace lv
