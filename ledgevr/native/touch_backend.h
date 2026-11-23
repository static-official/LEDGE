// LEDGE Mobile — touch backend: EGL surface, multi-touch input, HUD controls.
// Replaces the OpenXR XrBackend for non-VR Android phones/tablets.
// Same renderer/world/network stack; camera + controls are touch-driven.
#pragma once
#include <jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <string>
#include "mathlib.h"

namespace lv {

// GL helpers shared by renderer/UI (defined here for the mobile build;
// xr_backend.cpp provides them in the VR build).
GLuint lvCompileShader(GLenum type, const char* src);
GLuint lvLinkProgram(GLuint vs, GLuint fs);
GLuint lvMakeFbo(GLuint tex, int w, int h);

// HUD button ids (fixed screen-space rects, hit-tested in touchEvent)
enum HudBtn {
    HB_NONE = 0,
    HB_MENU,      // hamburger — opens the ImGui menu
    HB_EDITOR,    // quick-open menu on the Editor tab
    HB_LOBBY,     // quick-open menu on the Lobbies tab
    HB_JUMP,
    HB_GRABL,
    HB_GRABR,
    HB_PLACE,     // editor: place palette block at aim point
    HB_SELECT,    // editor: select block under crosshair
    HB_COUNT
};

struct TouchBackend {
    // EGL — context persists across surface loss (GL objects stay valid)
    EGLDisplay eglDisplay = EGL_NO_DISPLAY;
    EGLConfig  eglConfig = nullptr;
    EGLContext eglContext = EGL_NO_CONTEXT;
    EGLSurface eglSurface = EGL_NO_SURFACE;
    ANativeWindow* window = nullptr;
    int32_t w = 0, h = 0;
    bool ctxReady = false;        // display + context created
    bool surfReady = false;       // surface attached + current
    bool es32 = true;             // device supports GLES 3.2 (else 3.0 shim)

    // camera (first person)
    float yaw = 0, pitch = 0;     // radians; yaw=0 faces -Z (matches VR body yaw)

    // gameplay input state (read by main each frame)
    vec2  stickL;                 // virtual joystick, [-1..1]
    bool  jumpDown = false;
    bool  grabL = false, grabR = false;
    bool  placePressed = false;   // edge-triggered by main
    bool  selectPressed = false;

    // HUD
    bool  hudVisible = true;
    bool  editorMode = false;     // shows PLACE/SELECT buttons
    bool  btnDown[HB_COUNT] = {};
    HudBtn btnOfId[8] = {};       // which HUD button each pointer id holds
    struct Rect { float x, y, w, h; };   // screen space, y DOWN from top-left
    Rect  btnRect[HB_COUNT] = {};
    void layoutHud();             // recompute rects for current w/h

    // touch pointers
    enum PtrRole { PR_NONE = 0, PR_JOY, PR_LOOK, PR_UI } ;
    PtrRole role[8] = {};
    float joyOriginX = 0, joyOriginY = 0;
    float lastLookX = 0, lastLookY = 0;
    bool  lookDragging = false;

    // ImGui bridge (menu open: all touches go to ImGui as the mouse)
    bool  mouseDown = false;
    float mouseX = 0, mouseY = 0;

    // grab-pull synthesis (GrabVR movement on touch):
    // while a grab button is held, look-drag deltas accumulate here as
    // per-frame pull amounts (pixels); main_mobile converts them to body
    // motion ("pull the world") + a fling velocity on release.
    float pullDX = 0, pullDY = 0;  // accumulated drag this frame (grab mode)

    bool  initContext();                     // EGL display + ES3 context (once)
    bool  attachSurface(ANativeWindow* win); // create surface + makeCurrent
    void  detachSurface();                   // surface gone (context kept)
    void  shutdown();
    bool  ok() const { return surfReady; }
    void  makeCurrent();
    bool  swap();

    // returns true if the event was handled (passed to AInputQueue_finishEvent)
    bool  touchEvent(AInputEvent* ev);

    // menu state relay (main sets these before routing touches)
    bool  menuOpen = false;

    void  aimRay(vec3 eye, vec3& dir) const; // camera forward ray
};

} // namespace lv
