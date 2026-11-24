// LEDGE iOS — touch backend: EAGL surface, multi-touch input, HUD controls.
// iOS twin of native/touch_backend.h (Android EGL). Same fields the game loop
// and HUD drawing code read, so main_ios.cpp mirrors main_mobile.cpp.
#pragma once
#import <OpenGLES/ES3/gl.h>
#import <OpenGLES/ES3/glext.h>
#import <OpenGLES/EAGL.h>
#import <QuartzCore/QuartzCore.h>
#import <Foundation/Foundation.h>
#include <string>
#include "../../ledgevr/native/mathlib.h"

namespace lv {

GLuint lvCompileShader(GLenum type, const char* src);
GLuint lvLinkProgram(GLuint vs, GLuint fs);
GLuint lvMakeFbo(GLuint tex, int w, int h);

enum HudBtn {
    HB_NONE = 0,
    HB_MENU,
    HB_EDITOR,
    HB_LOBBY,
    HB_JUMP,
    HB_GRABL,
    HB_GRABR,
    HB_PLACE,
    HB_SELECT,
    HB_COUNT
};

struct IosBackend {
    // EAGL
    EAGLContext* ctx = nullptr;
    CAEAGLLayer* layer = nullptr;
    GLuint colorRb = 0, depthRb = 0, fb = 0;
    int w = 0, h = 0;
    bool ctxReady = false;
    bool surfReady = false;

    // camera (first person)
    float yaw = 0, pitch = 0;      // radians; yaw=0 faces -Z

    // gameplay input state (read by main each frame)
    vec2  stickL = vec2();
    bool  jumpDown = false;
    bool  grabL = false, grabR = false;
    bool  placePressed = false;
    bool  selectPressed = false;

    // HUD
    bool  hudVisible = true;
    bool  editorMode = false;
    bool  btnDown[HB_COUNT] = {};
    HudBtn btnOfId[8] = {};
    struct Rect { float x, y, w, h; };
    Rect  btnRect[HB_COUNT] = {};
    void  layoutHud();

    // touch pointers (max 8 like the Android build)
    enum PtrRole { PR_NONE = 0, PR_JOY, PR_LOOK, PR_UI };
    PtrRole role[8] = {};
    float joyOriginX = 0, joyOriginY = 0;
    float lastLookX = 0, lastLookY = 0;
    bool  lookDragging = false;

    // ImGui bridge
    bool  mouseDown = false;
    float mouseX = 0, mouseY = 0;

    // grab-pull synthesis (same units as Android: pixels)
    float pullDX = 0, pullDY = 0;

    bool  menuOpen = false;

    // lifecycle (called from GameViewController)
    bool  init(CAEAGLLayer* layer, int width, int height);
    void  detachSurface();
    void  shutdown();
    bool  ok() const { return surfReady; }
    void  makeCurrent();
    bool  swap();

    // touches: phase 0=began 1=moved 2=ended/cancelled, id = touch identity (0..7)
    void  touches(int phase, uintptr_t id, float x, float y);
    void  aimRay(vec3 eye, vec3& dir) const;
};

} // namespace lv
