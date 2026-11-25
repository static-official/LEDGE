// LEDGE VR — OpenXR backend: instance/session, swapchains, spaces, actions, frame loop.
#pragma once
#include <jni.h>
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "mathlib.h"

namespace lv {

struct HandInput {
    bool   connected = false;
    bool   tracked = false;
    quat   rot;                 // grip orientation in LOCAL space
    vec3   pos;
    quat   aimRot;              // aim orientation (forward = -Z)
    vec3   aimPos;
    float  grab = 0.f;          // squeeze
    float  trigger = 0.f;
    bool   triggerClick = false;
    vec2   stick;
    bool   stickClick = false;
    bool   menuBtn = false;     // left hamburger ("menu") button — NOT the Y button
    bool   xBtn = false, yBtn = false, aBtn = false, bBtn = false;
};

struct EyeRender {
    XrSwapchain swap = XR_NULL_HANDLE;
    uint32_t count = 0;
    GLuint* fbo = nullptr;      // FBO per swapchain image
    GLuint* tex = nullptr;
    int32_t w = 0, h = 0;
    bool acquired = false;
    uint32_t index = 0;
};

struct XrBackend {
    // EGL
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLConfig  eglConfig = nullptr;
    EGLContext eglContext = EGL_NO_CONTEXT;
    EGLSurface eglSurface = EGL_NO_SURFACE;   // 4x4 pbuffer backing the context

    // OpenXR
    XrInstance instance = XR_NULL_HANDLE;
    XrSession  session = XR_NULL_HANDLE;
    XrSystemId systemId = 0;
    XrSpace    localSpace = XR_NULL_HANDLE;
    XrSpace    stageSpace = XR_NULL_HANDLE;
    XrSpace    viewSpace = XR_NULL_HANDLE;
    bool       usingStage = false;

    // swapchains
    EyeRender  eyes[2];
    EyeRender  uiQuad;          // menu texture
    XrSpace    uiSpace = XR_NULL_HANDLE; // world-locked menu anchor
    bool       uiQuadAcquired = false;
    uint32_t   uiQuadIndex = 0;

    // view info
    uint32_t   viewCount = 2;
    XrViewConfigurationView viewCfg[2]{};
    XrReferenceSpaceCreateInfo refCi{};
    float      refreshRate = 0.f;
    bool       hasFbRefresh = false;

    // actions
    XrActionSet actionSet = XR_NULL_HANDLE;
    XrAction    actPoseL = XR_NULL_HANDLE, actPoseR = XR_NULL_HANDLE;
    XrAction    actGrabL = XR_NULL_HANDLE, actGrabR = XR_NULL_HANDLE;
    XrAction    actTrigL = XR_NULL_HANDLE, actTrigR = XR_NULL_HANDLE;
    XrAction    actStickL = XR_NULL_HANDLE, actStickR = XR_NULL_HANDLE;
    XrAction    actMenu = XR_NULL_HANDLE;
    XrAction    actX = XR_NULL_HANDLE, actY = XR_NULL_HANDLE;
    XrAction    actA = XR_NULL_HANDLE, actB = XR_NULL_HANDLE;
    XrAction    actClickL = XR_NULL_HANDLE, actClickR = XR_NULL_HANDLE;
    XrAction    actHapL = XR_NULL_HANDLE, actHapR = XR_NULL_HANDLE;
    XrPath      handPathL = XR_NULL_PATH, handPathR = XR_NULL_PATH;
    XrSpace     gripSpaceL = XR_NULL_HANDLE, gripSpaceR = XR_NULL_HANDLE;
    XrSpace     aimSpaceL = XR_NULL_HANDLE, aimSpaceR = XR_NULL_HANDLE;

    // frame state
    XrFrameState frameState{};
    XrViewState   viewState{};
    XrView        views[2]{};
    bool          sessionFocused = false;
    bool          sessionRunning = false;
    bool          shouldExit = false;
    bool          initOk = false;
    char          runtimeName[256] = {0};

    // UI quad world transform (set from main each frame)
    bool   uiVisible = false;
    quat   uiRot;
    vec3   uiPos;
    float  uiW = 0.5f, uiH = 0.5f;

    HandInput inL, inR;         // updated by syncInput()
    vec3      headPos;
    quat      headRot;          // view orientation (LOCAL)
    float     projM[2][16];     // projection matrices per eye (row-major as mat4 col-major)
    float     viewM[2][16];     // view matrices (inverse of pose)

    // per-frame pointers for the renderer
    XrCompositionLayerProjection projLayer{};
    XrCompositionLayerQuad       quadLayer{};
    XrCompositionLayerBaseHeader* layers[2]{};
    uint32_t layerCount = 0;

    bool init(void* javaVM, void* androidContext, const char* appLabel);
    void shutdown();
    bool createSession();
    bool pumpEvents();
    void beginFrame();
    void endFrame();
    void syncInput();           // actions + spaces
    bool eyeAcquire(int i);     // acquire eye i and bind its FBO
    void eyeRelease(int i);
    bool sessionOk() const { return sessionRunning; }
    void haptic(int hand, float amplitude, float duration);
    void setUiLayer(const quat& rot, const vec3& pos, float w, float h, bool visible);

    // UI quad helpers: acquire texture for rendering the menu, release after.
    bool uiAcquire();           // sets uiQuadAcquired/index, returns fbo
    void uiRelease();           // commit texture

    // local vs stage recentering
    void recenter();
};

// GL helpers shared by renderer and UI
GLuint lvCompileShader(GLenum type, const char* src);
GLuint lvLinkProgram(GLuint vs, GLuint fs);
GLuint lvMakeFbo(GLuint tex, int w, int h);

} // namespace lv
