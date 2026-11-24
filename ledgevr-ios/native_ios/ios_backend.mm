// LEDGE iOS — touch backend implementation: EAGL surface + multi-touch HUD.
// Behavior mirrors native/touch_backend.cpp (Android EGL) so the game loop
// code in main_ios.cpp is a near copy of main_mobile.cpp.
#include "ios_backend.h"
#import <UIKit/UIKit.h>
#include "../../ledgevr/native/log.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace lv {

// ---------------------------------------------------------------------------
// EAGL lifecycle
// ---------------------------------------------------------------------------
bool IosBackend::init(CAEAGLLayer* l, int width, int height) {
    (void)width; (void)height;
    if (!ctxReady) {
        ctx = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
        if (!ctx || ![EAGLContext setCurrentContext:ctx]) {
            LOGE("iOS: EAGL ES3 context failed");
            return false;
        }
        ctxReady = true;
    }
    layer = l;
    layer.drawableProperties = @{
        kEAGLDrawablePropertyColorFormat : kEAGLColorFormatRGBA8,
        kEAGLDrawablePropertyRetainedBacking : @NO,
    };
    layer.opaque = YES;
    // rebuild the drawable-sized surface when missing OR resized (rotation)
    CGFloat scale = [UIScreen mainScreen].scale;
    int nw = (int)roundf((float)(l.bounds.size.width * scale));
    int nh = (int)roundf((float)(l.bounds.size.height * scale));
    if (surfReady && nw == w && nh == h) return true;
    if (surfReady) detachSurface();
    layer.contentsScale = scale;
    glGenFramebuffers(1, &fb);
    glGenRenderbuffers(1, &colorRb);
    glGenRenderbuffers(1, &depthRb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glBindRenderbuffer(GL_RENDERBUFFER, colorRb);
    if (![ctx renderbufferStorage:GL_RENDERBUFFER fromDrawable:layer]) {
        LOGE("iOS: renderbufferStorage fromDrawable failed");
        return false;
    }
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &w);
    glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &h);
    glBindRenderbuffer(GL_RENDERBUFFER, depthRb);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, colorRb);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depthRb);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("iOS: framebuffer incomplete");
        return false;
    }
    surfReady = true;
    layoutHud();
    LOGI("iOS: EAGL surface %dx%d ready", w, h);
    return surfReady;
}

void IosBackend::detachSurface() {
    if (fb) glDeleteFramebuffers(1, &fb);
    if (colorRb) glDeleteRenderbuffers(1, &colorRb);
    if (depthRb) glDeleteRenderbuffers(1, &depthRb);
    fb = colorRb = depthRb = 0;
    surfReady = false;
}

void IosBackend::shutdown() {
    detachSurface();
    if (ctx) {
        [EAGLContext setCurrentContext:nil];
        ctx = nullptr;
    }
    ctxReady = false;
}

void IosBackend::makeCurrent() {
    if (ctxReady) [EAGLContext setCurrentContext:ctx];
}

bool IosBackend::swap() {
    return ctx && [ctx presentRenderbuffer:GL_RENDERBUFFER];
}

// ---------------------------------------------------------------------------
// HUD layout — proportional rects mirroring the Android build
// ---------------------------------------------------------------------------
void IosBackend::layoutHud() {
    float s = (float)h / 720.0f;                       // design for 720p height
    if (s < 0.75f) s = 0.75f;
    float m = 18 * s;                                  // margin
    float bs = 64 * s;                                 // base button size
    auto R = [&](float x, float y, float bw, float bh) {
        Rect r; r.x = x; r.y = y; r.w = bw; r.h = bh; return r;
    };
    btnRect[HB_MENU]   = R(w - m - bs, m, bs, bs);              // top-right
    btnRect[HB_LOBBY]  = R(w - m - bs * 2.2f, m, bs * 1.2f, bs); // top-right #2
    btnRect[HB_EDITOR] = R(w - m - bs * 3.6f, m, bs * 1.2f, bs); // top-right #3
    btnRect[HB_JUMP]   = R(w - m - bs * 1.35f, h - m - bs * 1.35f, bs * 1.35f, bs * 1.35f);
    btnRect[HB_GRABL]  = R(m, h - m - bs, bs, bs);
    btnRect[HB_GRABR]  = R(m + bs * 1.2f, h - m - bs, bs, bs);
    btnRect[HB_PLACE]  = R(w - m - bs * 1.35f, h - m - bs * 3.0f, bs * 1.35f, bs * 1.35f);
    btnRect[HB_SELECT] = R(w - m - bs * 2.9f, h - m - bs * 3.0f, bs * 1.35f, bs * 1.35f);
}

// ---------------------------------------------------------------------------
// touches — phase 0 began, 1 moved, 2 ended/cancelled
// ---------------------------------------------------------------------------
void IosBackend::touches(int phase, uintptr_t id, float x, float y) {
    if (id >= 8) id = 7;   // clamp like Android pointer cap
    if (menuOpen) {
        // menu mode: first touch drives ImGui as the mouse
        mouseX = x; mouseY = y;
        if (phase == 0) mouseDown = true;
        if (phase == 2) mouseDown = false;
        return;
    }
    switch (phase) {
    case 0: {
        // HUD buttons first
        HudBtn hitBtn = HB_NONE;
        for (int i = 1; i < HB_COUNT; i++) {
            const Rect& r = btnRect[i];
            if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h) { hitBtn = (HudBtn)i; break; }
        }
        if (hitBtn == HB_MENU) { btnDown[HB_MENU] = true; role[id] = PR_UI; return; }
        if (hitBtn != HB_NONE) {
            btnDown[hitBtn] = true;
            btnOfId[id] = hitBtn;
            role[id] = PR_UI;
            return;
        }
        // left third = joystick, else look-drag
        if (x < w * 0.32f) {
            role[id] = PR_JOY;
            joyOriginX = x; joyOriginY = y;
            stickL = vec2();
        } else {
            role[id] = PR_LOOK;
            lastLookX = x; lastLookY = y;
            lookDragging = true;
        }
        break;
    }
    case 1: {
        if (role[id] == PR_JOY) {
            float dx = x - joyOriginX, dy = y - joyOriginY;
            float rad = 90.0f;
            stickL.x = dx / rad;
            stickL.y = dy / rad;
            float l = len(stickL);
            if (l > 1.0f) stickL = stickL * (1.0f / l);
        } else if (role[id] == PR_LOOK) {
            float dx = x - lastLookX, dy = y - lastLookY;
            lastLookX = x; lastLookY = y;
            if (grabL || grabR) {
                pullDX += dx; pullDY += dy;      // grab mode: pull the world
            } else {
                float s = 0.0038f;
                yaw -= dx * s;
                pitch -= dy * s;
                pitch = std::max(-1.45f, std::min(1.45f, pitch));
            }
        } else if (role[id] == PR_UI) {
            const Rect& r = btnRect[btnOfId[id]];
            bool in = x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h;
            btnDown[btnOfId[id]] = in;
        }
        break;
    }
    case 2: {
        if (role[id] == PR_JOY) {
            stickL = vec2();
        } else if (role[id] == PR_UI) {
            if (btnDown[btnOfId[id]]) {
                // edge triggers
                if (btnOfId[id] == HB_JUMP) jumpDown = false;
                if (btnOfId[id] == HB_PLACE) placePressed = true;
                if (btnOfId[id] == HB_SELECT) selectPressed = true;
            }
            btnDown[btnOfId[id]] = false;
            btnOfId[id] = HB_NONE;
        }
        if (role[id] == PR_LOOK) lookDragging = false;
        role[id] = PR_NONE;
        break;
    }
    }
    // button held states → gameplay
    jumpDown = btnDown[HB_JUMP];
    grabL = btnDown[HB_GRABL];
    grabR = btnDown[HB_GRABR];
}

void IosBackend::aimRay(vec3 eye, vec3& dir) const {
    dir = vec3(cosf(pitch) * -sinf(yaw), sinf(pitch), cosf(pitch) * -cosf(yaw));
    (void)eye;
}

} // namespace lv
