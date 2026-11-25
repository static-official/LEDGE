// LEDGE VR — OpenXR backend implementation (Quest-first, GLES surfaceless)
#include "xr_backend.h"
#include "log.h"
#include <jni.h>
#include <android/native_activity.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <cmath>

namespace lv {

#define OXR(x) do { XrResult _r = (x); if (XR_FAILED(_r)) LOGE("OXR fail %d @ %d (%s)", (int)_r, __LINE__, #x); } while(0)

// ---------------------------------------------------------------------------
GLuint lvCompileShader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GLsizei l = 0;
        glGetShaderInfoLog(s, sizeof(log), &l, log);
        LOGE("shader compile error:\n%s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
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
        GLsizei l = 0;
        glGetProgramInfoLog(p, sizeof(log), &l, log);
        LOGE("program link error:\n%s\n", log);
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
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        LOGE("FBO incomplete");
    return fbo;
}
// ---------------------------------------------------------------------------

static bool glInit(XrBackend* B) {
    B->eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (B->eglDisplay == EGL_NO_DISPLAY) return false;
    if (!eglInitialize(B->eglDisplay, nullptr, nullptr)) return false;

    const EGLint cfgAttrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,   // pbuffer-capable config: works even
                                             // without EGL_KHR_surfaceless_context
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE };
    EGLConfig cfg = nullptr;
    EGLint n = 0;
    if (!eglChooseConfig(B->eglDisplay, cfgAttrs, &cfg, 1, &n) || n < 1) return false;
    B->eglConfig = cfg;

    const EGLint ctxAttrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    B->eglContext = eglCreateContext(B->eglDisplay, cfg, EGL_NO_CONTEXT, ctxAttrs);
    if (B->eglContext == EGL_NO_CONTEXT) return false;

    // 4x4 pbuffer as the draw surface — guaranteed current on every ES3 device
    // (the unchecked surfaceless makeCurrent before this could silently leave
    //  NO context current, failing every GL call made afterwards)
    const EGLint pbAttrs[] = { EGL_WIDTH, 4, EGL_HEIGHT, 4, EGL_NONE };
    B->eglSurface = eglCreatePbufferSurface(B->eglDisplay, cfg, pbAttrs);
    if (!eglMakeCurrent(B->eglDisplay, B->eglSurface, B->eglSurface, B->eglContext)) {
        LOGE("eglMakeCurrent failed (pbuffer)");
        return false;
    }
    LOGI("EGL %s | GL %s", eglQueryString(B->eglDisplay, EGL_VERSION),
         (const char*)glGetString(GL_VERSION));
    return true;
}

static void createSwapchain(XrBackend* B, uint32_t w, uint32_t h, EyeRender* E) {
    XrSwapchainCreateInfo ci{};
    ci.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    ci.width = w;
    ci.height = h;
    ci.format = 0x8058; // GL_RGBA8
    ci.sampleCount = 1;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    OXR(xrCreateSwapchain(B->session, &ci, &E->swap));
    E->w = (int32_t)w; E->h = (int32_t)h;

    uint32_t cnt = 0;
    OXR(xrEnumerateSwapchainImages(E->swap, 0, &cnt, nullptr));
    std::vector<XrSwapchainImageOpenGLESKHR> imgs(cnt);
    for (auto& im : imgs) { im.type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR; }
    OXR(xrEnumerateSwapchainImages(E->swap, cnt, &cnt, (XrSwapchainImageBaseHeader*)imgs.data()));
    E->count = cnt;
    E->tex = new GLuint[cnt];
    E->fbo = new GLuint[cnt];
    for (uint32_t i = 0; i < cnt; i++) {
        E->tex[i] = (GLuint)imgs[i].image;
        glGenFramebuffers(1, &E->fbo[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, E->fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, E->tex[i], 0);
        glViewport(0, 0, w, h);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            LOGE("FBO incomplete (swap) %u", i);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    E->acquired = false;
}

static void destroySwapchain(EyeRender* E) {
    if (E->fbo) { for (uint32_t i = 0; i < E->count; i++) glDeleteFramebuffers(1, &E->fbo[i]); delete[] E->fbo; E->fbo = nullptr; }
    if (E->tex) { for (uint32_t i = 0; i < E->count; i++) glDeleteTextures(1, &E->tex[i]); delete[] E->tex; E->tex = nullptr; }
    if (E->swap != XR_NULL_HANDLE) { xrDestroySwapchain(E->swap); E->swap = XR_NULL_HANDLE; }
    E->count = 0;
}

static XrAction createFloatAction(XrActionSet as, const char* name, const char* loc) {
    XrActionCreateInfo ci{ XR_TYPE_ACTION_CREATE_INFO };
    strcpy(ci.actionName, name);
    strcpy(ci.localizedActionName, loc);
    ci.actionType = XR_ACTION_TYPE_FLOAT_INPUT;
    XrAction a = XR_NULL_HANDLE;
    OXR(xrCreateAction(as, &ci, &a));
    return a;
}
static XrAction createBoolAction(XrActionSet as, const char* name, const char* loc) {
    XrActionCreateInfo ci{ XR_TYPE_ACTION_CREATE_INFO };
    strcpy(ci.actionName, name);
    strcpy(ci.localizedActionName, loc);
    ci.actionType = XR_ACTION_TYPE_BOOLEAN_INPUT;
    XrAction a = XR_NULL_HANDLE;
    OXR(xrCreateAction(as, &ci, &a));
    return a;
}
static XrAction createVec2Action(XrActionSet as, const char* name, const char* loc) {
    XrActionCreateInfo ci{ XR_TYPE_ACTION_CREATE_INFO };
    strcpy(ci.actionName, name);
    strcpy(ci.localizedActionName, loc);
    ci.actionType = XR_ACTION_TYPE_VECTOR2F_INPUT;
    XrAction a = XR_NULL_HANDLE;
    OXR(xrCreateAction(as, &ci, &a));
    return a;
}
static XrAction createPoseAction(XrActionSet as, const char* name, const char* loc) {
    XrActionCreateInfo ci{ XR_TYPE_ACTION_CREATE_INFO };
    strcpy(ci.actionName, name);
    strcpy(ci.localizedActionName, loc);
    ci.actionType = XR_ACTION_TYPE_POSE_INPUT;
    XrAction a = XR_NULL_HANDLE;
    OXR(xrCreateAction(as, &ci, &a));
    return a;
}

bool XrBackend::init(void* javaVM, void* androidContext, const char* appLabel) {
    if (!glInit(this)) { LOGE("EGL init failed"); return false; }

    // bootstrap loader (Android) before creating the instance
    {
        PFN_xrGetInstanceProcAddr gpa = xrGetInstanceProcAddr;
        XrLoaderInitInfoAndroidKHR li{};
        li.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
        li.next = nullptr;
        li.applicationVM = (void*)javaVM;
        li.applicationContext = (void*)androidContext;
        PFN_xrInitializeLoaderKHR initF = nullptr;
        if (XR_SUCCEEDED(gpa(XR_NULL_HANDLE, "xrInitializeLoaderKHR", (PFN_xrVoidFunction*)&initF)) && initF)
            initF((const XrLoaderInitInfoBaseHeaderKHR*)&li);
    }

    const char* exts[] = {
        "XR_KHR_android_create_instance",
        "XR_KHR_opengl_es_enable",
        "XR_FB_display_refresh_rate",
    };
    XrInstanceCreateInfo ici{ XR_TYPE_INSTANCE_CREATE_INFO };
    ici.enabledExtensionCount = 3;
    ici.enabledExtensionNames = exts;
    ici.enabledApiLayerCount = 0;
    strcpy(ici.applicationInfo.applicationName, appLabel);
    ici.applicationInfo.applicationVersion = 1;
    ici.applicationInfo.engineVersion = 1;
    ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

    XrInstanceCreateInfoAndroidKHR iciA{ XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR };
    iciA.applicationVM = (void*)javaVM;
    iciA.applicationActivity = (void*)androidContext;
    ici.next = &iciA;

    XrResult r = xrCreateInstance(&ici, &instance);
    if (XR_FAILED(r)) { LOGE("xrCreateInstance failed %d", (int)r); return false; }
    initOk = true;

    XrInstanceProperties ip{ XR_TYPE_INSTANCE_PROPERTIES };
    OXR(xrGetInstanceProperties(instance, &ip));
    snprintf(runtimeName, sizeof(runtimeName), "%s", ip.runtimeName);
    LOGI("OpenXR runtime: %s", runtimeName);

    XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = xrGetSystem(instance, &sgi, &systemId);
    if (XR_FAILED(r)) { LOGE("xrGetSystem failed %d (no HMD?)", (int)r); return false; }

    XrSystemProperties sp{ XR_TYPE_SYSTEM_PROPERTIES };
    OXR(xrGetSystemProperties(instance, systemId, &sp));
    LOGI("System: %s maxLayers=%u", sp.systemName, sp.graphicsProperties.maxLayerCount);

    for (uint32_t i = 0; i < 2; i++) viewCfg[i] = { XR_TYPE_VIEW_CONFIGURATION_VIEW };
    uint32_t nviews = 2;
    OXR(xrEnumerateViewConfigurationViews(instance, systemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &nviews, viewCfg));
    viewCount = nviews;
    LOGI("Views: %u  recommended %ux%u", viewCount,
         viewCfg[0].recommendedImageRectWidth, viewCfg[0].recommendedImageRectHeight);

    // refresh rate (best effort, prefer <=90 Hz)
    // PFN_xrSetDisplayRefreshRateFB is not shipped in the 1.0.34 core header:
    // declare it locally. Both FB functions take an XrSession (not instance/systemId).
    hasFbRefresh = false;
    {
        typedef XrResult (*PFN_xrSetDisplayRefreshRateFB_)(XrSession, float);
        PFN_xrEnumerateDisplayRefreshRatesFB er = nullptr;
        PFN_xrSetDisplayRefreshRateFB_ sr = nullptr;
        if (XR_SUCCEEDED(xrGetInstanceProcAddr(instance, "xrEnumerateDisplayRefreshRatesFB", (PFN_xrVoidFunction*)&er)) &&
            XR_SUCCEEDED(xrGetInstanceProcAddr(instance, "xrSetDisplayRefreshRateFB", (PFN_xrVoidFunction*)&sr)) &&
            er && sr && session != XR_NULL_HANDLE) {
            uint32_t n = 0;
            if (XR_SUCCEEDED(er(session, 0, &n, nullptr)) && n > 0) {
                std::vector<float> rates(n);
                OXR(er(session, n, &n, rates.data()));
                float best = 0.f;
                for (float f : rates) if (f <= 90.5f && f > best) best = f;
                if (best > 0.f) { OXR(sr(session, best)); refreshRate = best; }
                LOGI("Set display refresh %.0f Hz", refreshRate);
            }
        }
    }
    return true;
}

bool XrBackend::createSession() {
    if (session != XR_NULL_HANDLE) return true;

    // ---- actions
    {
        XrActionSetCreateInfo aci{ XR_TYPE_ACTION_SET_CREATE_INFO };
        strcpy(aci.actionSetName, "ledgevr");
        strcpy(aci.localizedActionSetName, "LEDGE VR");
        aci.priority = 0;
        OXR(xrCreateActionSet(instance, &aci, &actionSet));

        actPoseL = createPoseAction(actionSet, "pose_left", "Left Hand Pose");
        actPoseR = createPoseAction(actionSet, "pose_right", "Right Hand Pose");
        actGrabL = createFloatAction(actionSet, "grab_left", "Left Grab");
        actGrabR = createFloatAction(actionSet, "grab_right", "Right Grab");
        actTrigL = createFloatAction(actionSet, "trigger_left", "Left Trigger");
        actTrigR = createFloatAction(actionSet, "trigger_right", "Right Trigger");
        actStickL = createVec2Action(actionSet, "stick_left", "Left Thumbstick");
        actStickR = createVec2Action(actionSet, "stick_right", "Right Thumbstick");
        actMenu = createBoolAction(actionSet, "menu_left", "Open Menu");
        actX = createBoolAction(actionSet, "x_left", "Left X");
        actY = createBoolAction(actionSet, "y_left", "Left Y");
        actA = createBoolAction(actionSet, "a_right", "Right A");
        actB = createBoolAction(actionSet, "b_right", "Right B");
        actClickL = createBoolAction(actionSet, "click_left", "Left Stick Click");
        actClickR = createBoolAction(actionSet, "click_right", "Right Stick Click");
        {
            XrActionCreateInfo ci{ XR_TYPE_ACTION_CREATE_INFO };
            strcpy(ci.actionName, "haptic_left"); strcpy(ci.localizedActionName, "Left Vibration");
            ci.actionType = XR_ACTION_TYPE_VIBRATION_OUTPUT;
            OXR(xrCreateAction(actionSet, &ci, &actHapL));
            strcpy(ci.actionName, "haptic_right"); strcpy(ci.localizedActionName, "Right Vibration");
            OXR(xrCreateAction(actionSet, &ci, &actHapR));
        }

        OXR(xrStringToPath(instance, "/user/hand/left", &handPathL));
        OXR(xrStringToPath(instance, "/user/hand/right", &handPathR));

        // suggested bindings for both profiles.
        // NOTE: paths MUST exist in the profile topology — suggesting one bad path
        // (e.g. right-hand menu on Quest Touch) rejects the WHOLE xrSuggest call.
        const char* profilePaths[] = {
            "/interaction_profiles/oculus/touch_controller",
            "/interaction_profiles/khronos/simple_controller",
        };
        for (const char* profile : profilePaths) {
            std::vector<XrActionSuggestedBinding> sb;
            XrPath p = XR_NULL_PATH;
            if (XR_FAILED(xrStringToPath(instance, profile, &p))) continue;
            const bool touch = strstr(profile, "oculus/touch") != nullptr;
            auto add = [&](const char* in, XrAction a) {
                XrPath ip = XR_NULL_PATH;
                char buf[160];
                snprintf(buf, sizeof(buf), "%s%s", profile, in);
                if (XR_SUCCEEDED(xrStringToPath(instance, buf, &ip))) {
                    XrActionSuggestedBinding b;
                    b.action = a;
                    b.binding = ip;
                    sb.push_back(b);
                }
            };
            add("/user/hand/left/input/grip/pose", actPoseL);
            add("/user/hand/left/input/aim/pose", actPoseL);
            add("/user/hand/right/input/grip/pose", actPoseR);
            add("/user/hand/right/input/aim/pose", actPoseR);
            if (touch) {
                // Quest Touch: squeeze/trigger/stick/x/y/a/b. The HAMBURGER button
                // is the dedicated "menu" input on the LEFT controller only
                // (it is NOT the Y button); the right controller has no menu input.
                add("/user/hand/left/input/squeeze/click", actGrabL);
                add("/user/hand/right/input/squeeze/click", actGrabR);
                add("/user/hand/left/input/trigger/value", actTrigL);
                add("/user/hand/right/input/trigger/value", actTrigR);
                add("/user/hand/left/input/thumbstick", actStickL);
                add("/user/hand/right/input/thumbstick", actStickR);
                add("/user/hand/left/input/menu/click", actMenu);
                add("/user/hand/left/input/x/click", actX);
                add("/user/hand/left/input/y/click", actY);
                add("/user/hand/right/input/a/click", actA);
                add("/user/hand/right/input/b/click", actB);
                add("/user/hand/left/input/thumbstick/click", actClickL);
                add("/user/hand/right/input/thumbstick/click", actClickR);
            } else {
                // Khronos simple controller: select, menu, haptics only.
                add("/user/hand/left/input/select/click", actTrigL);
                add("/user/hand/right/input/select/click", actTrigR);
                add("/user/hand/left/input/squeeze/click", actGrabL);
                add("/user/hand/right/input/squeeze/click", actGrabR);
                add("/user/hand/left/input/menu/click", actMenu);
                add("/user/hand/right/input/menu/click", actMenu);
            }
            add("/user/hand/left/output/haptic", actHapL);
            add("/user/hand/right/output/haptic", actHapR);

            XrInteractionProfileSuggestedBinding sugg{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
            sugg.interactionProfile = p;
            sugg.countSuggestedBindings = (uint32_t)sb.size();
            sugg.suggestedBindings = sb.data();
            OXR(xrSuggestInteractionProfileBindings(instance, &sugg));
        }
    }

    // ---- session
    XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
    XrGraphicsBindingOpenGLESAndroidKHR gb{ XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR };
    gb.display = eglDisplay;
    gb.config = eglConfig;
    gb.context = eglContext;
    sci.next = &gb;
    sci.systemId = systemId;
    XrResult r = xrCreateSession(instance, &sci, &session);
    if (XR_FAILED(r)) { LOGE("xrCreateSession failed %d", (int)r); return false; }

    XrSessionActionSetsAttachInfo attach{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    attach.countActionSets = 1;
    attach.actionSets = &actionSet;
    OXR(xrAttachSessionActionSets(session, &attach));

    // spaces
    {
        XrActionSpaceCreateInfo asci{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
        asci.action = actPoseL;
        asci.subactionPath = handPathL;
        asci.poseInActionSpace = { {0,0,0,1}, {0,0,0} };
        OXR(xrCreateActionSpace(session, &asci, &gripSpaceL));
        asci.action = actPoseR;
        asci.subactionPath = handPathR;
        OXR(xrCreateActionSpace(session, &asci, &gripSpaceR));
    }
    {
        XrReferenceSpaceCreateInfo rci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
        rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
        rci.poseInReferenceSpace = { {0,0,0,1}, {0,0,0} };
        XrResult rr = xrCreateReferenceSpace(session, &rci, &localSpace);
        if (XR_FAILED(rr)) { LOGE("local space failed"); return false; }
        rci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
        if (XR_SUCCEEDED(xrCreateReferenceSpace(session, &rci, &stageSpace))) {
            viewSpace = stageSpace;
            usingStage = true;
        } else {
            viewSpace = localSpace;
            usingStage = false;
        }
    }

    // swapchains
    uint32_t w = viewCfg[0].recommendedImageRectWidth;
    uint32_t h = viewCfg[0].recommendedImageRectHeight;
    for (uint32_t i = 0; i < viewCount && i < 2; i++)
        createSwapchain(this, w, h, &eyes[i]);
    createSwapchain(this, 1536, 1536, &uiQuad);

    LOGI("Session ready. Eyes %ux%u. Stage=%d", w, h, (int)usingStage);
    return true;
}

// Pump OpenXR events; returns true when session began rendering.
bool XrBackend::pumpEvents() {
    bool any = false;
    char buf[sizeof(XrEventDataBuffer)] = {0};
    for (;;) {
        XrEventDataBuffer* ev = (XrEventDataBuffer*)buf;
        ev->type = XR_TYPE_EVENT_DATA_BUFFER;
        ev->next = nullptr;
        XrResult r = xrPollEvent(instance, ev);
        if (r != XR_SUCCESS) break;
        any = true;
        switch (ev->type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            auto* e = (XrEventDataSessionStateChanged*)ev;
            sessionRunning = (e->state >= XR_SESSION_STATE_READY);
            sessionFocused = (e->state == XR_SESSION_STATE_FOCUSED);
            if (e->state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo bi{ XR_TYPE_SESSION_BEGIN_INFO };
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                OXR(xrBeginSession(session, &bi));
                LOGI("Session began");
            }
            if (e->state == XR_SESSION_STATE_STOPPING) {
                LOGI("Session stopping");
            }
            if (e->state == XR_SESSION_STATE_EXITING || e->state == XR_SESSION_STATE_LOSS_PENDING) {
                LOGI("Session exiting/loss");
                shouldExit = true;
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            break;
        default:
            break;
        }
    }
    return any;
}

bool XrBackend::eyeAcquire(int i) {
    EyeRender& E = eyes[i];
    XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    OXR(xrAcquireSwapchainImage(E.swap, &ai, &E.index));
    XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = 100000000;
    if (XR_FAILED(xrWaitSwapchainImage(E.swap, &wi))) return false;
    E.acquired = true;
    glBindFramebuffer(GL_FRAMEBUFFER, E.fbo[E.index]);
    glViewport(0, 0, E.w, E.h);
    return true;
}

void XrBackend::eyeRelease(int i) {
    EyeRender& E = eyes[i];
    if (!E.acquired) return;
    XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    OXR(xrReleaseSwapchainImage(E.swap, &ri));
    E.acquired = false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

static void poseToMath(const XrPosef& p, quat& q, vec3& v) {
    q = quat(p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w);
    v = vec3(p.position.x, p.position.y, p.position.z);
}

void XrBackend::beginFrame() {
    if (session == XR_NULL_HANDLE) return;
    frameState = { XR_TYPE_FRAME_STATE };
    OXR(xrWaitFrame(session, nullptr, &frameState));
    pumpEvents();
    if (sessionRunning) OXR(xrBeginFrame(session, nullptr));
    layerCount = 0;

    // locate views
    XrViewLocateInfo vli{ XR_TYPE_VIEW_LOCATE_INFO };
    vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    vli.displayTime = frameState.predictedDisplayTime;
    vli.space = viewSpace;
    for (int i = 0; i < 2; i++) views[i] = { XR_TYPE_VIEW };
    viewState = { XR_TYPE_VIEW_STATE };
    uint32_t n = 0;
    OXR(xrLocateViews(session, &vli, &viewState, 2, &n, views));
    if ((viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) &&
        (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
        poseToMath(views[0].pose, headRot, headPos);
    }
}

// Build a view matrix (inverse of pose)
static mat4 poseToView(const XrPosef& p) {
    quat q(p.orientation.x, p.orientation.y, p.orientation.z, p.orientation.w);
    vec3 t(p.position.x, p.position.y, p.position.z);
    mat4 R = mat4::fromQuat(q.conj());
    mat4 T = mat4::translate(-t);
    // inverse(R*T) = T^-1 * R^-1 ; since R^-1 = fromQuat(q.conj()) and we want
    // view = inverse(pose) => view = fromQuat(q.conj()) translated by -R*q_pos
    vec3 rc = q.conj().rotate(t);
    mat4 V = R;
    V.m[12] = -rc.x; V.m[13] = -rc.y; V.m[14] = -rc.z;
    return V;
}

static void offCenterProj(float angleLeft, float angleRight, float angleUp, float angleDown,
                          float zn, float zf, float* out) {
    float l = tanf(angleLeft) * zn;
    float r = tanf(angleRight) * zn;
    float t = tanf(angleUp) * zn;
    float b = tanf(angleDown) * zn;
    memset(out, 0, sizeof(float) * 16);
    out[0] = 2.0f * zn / (r - l);
    out[5] = 2.0f * zn / (t - b);
    out[8] = (r + l) / (r - l);
    out[9] = (t + b) / (t - b);
    out[10] = -(zf + zn) / (zf - zn);
    out[11] = -1.0f;
    out[14] = -2.0f * zf * zn / (zf - zn);
}

// set from main: current UI quad world transform (local space) + size in meters
void XrBackend::setUiLayer(const quat& rot, const vec3& pos, float w, float h, bool visible) {
    uiVisible = visible;
    uiRot = rot; uiPos = pos; uiW = w; uiH = h;
}

void XrBackend::endFrame() {
    if (session == XR_NULL_HANDLE) return;

    for (int i = 0; i < 2; i++) eyeRelease(i);

    if (sessionRunning) {
        // assemble projection layer
        projLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
        projLayer.space = viewSpace;
        projLayer.viewCount = viewCount > 2 ? 2 : viewCount;
        static XrCompositionLayerProjectionView pviews[2];
        for (uint32_t i = 0; i < projLayer.viewCount; i++) {
            pviews[i] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            pviews[i].pose = views[i].pose;
            pviews[i].fov = views[i].fov;
            pviews[i].subImage.swapchain = eyes[i].swap;
            pviews[i].subImage.imageRect.offset = { 0, 0 };
            pviews[i].subImage.imageRect.extent = { eyes[i].w, eyes[i].h };
            pviews[i].subImage.imageArrayIndex = 0;
        }
        projLayer.views = pviews;
        layers[layerCount++] = (XrCompositionLayerBaseHeader*)&projLayer;

        if (uiVisible && uiQuadAcquired) {
            quadLayer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
            quadLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
            quadLayer.space = viewSpace;
            quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            quadLayer.subImage.swapchain = uiQuad.swap;
            quadLayer.subImage.imageRect.offset = { 0, 0 };
            quadLayer.subImage.imageRect.extent = { uiQuad.w, uiQuad.h };
            quadLayer.subImage.imageArrayIndex = 0;
            quadLayer.pose.orientation = { uiRot.x, uiRot.y, uiRot.z, uiRot.w };
            quadLayer.pose.position = { uiPos.x, uiPos.y, uiPos.z };
            quadLayer.size = { uiW, uiH };
            layers[layerCount++] = (XrCompositionLayerBaseHeader*)&quadLayer;
        }

        XrFrameEndInfo fei{ XR_TYPE_FRAME_END_INFO };
        fei.displayTime = frameState.predictedDisplayTime;
        fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
        fei.layers = layerCount > 0 ? layers : nullptr;
        fei.layerCount = layerCount;
        OXR(xrEndFrame(session, &fei));
    }
}

// per-hand action state readers (the queried action lives in XrActionStateGetInfo)
static float getStateFloat(XrSession s, XrAction a, XrPath hand) {
    XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO };
    gi.action = a;
    gi.subactionPath = hand;
    XrActionStateFloat st{ XR_TYPE_ACTION_STATE_FLOAT };
    if (XR_FAILED(xrGetActionStateFloat(s, &gi, &st))) return 0.f;
    return st.isActive ? st.currentState : 0.f;
}
static bool getStateBool(XrSession s, XrAction a, XrPath hand) {
    XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO };
    gi.action = a;
    gi.subactionPath = hand;
    XrActionStateBoolean st{ XR_TYPE_ACTION_STATE_BOOLEAN };
    if (XR_FAILED(xrGetActionStateBoolean(s, &gi, &st))) return false;
    return st.isActive && st.currentState;
}
static vec2 getStateVec2(XrSession s, XrAction a, XrPath hand) {
    XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO };
    gi.action = a;
    gi.subactionPath = hand;
    XrActionStateVector2f st{ XR_TYPE_ACTION_STATE_VECTOR2F };
    if (XR_FAILED(xrGetActionStateVector2f(s, &gi, &st))) return vec2();
    return st.isActive ? vec2(st.currentState.x, st.currentState.y) : vec2();
}
static bool getConnected(XrSession s, XrAction a, XrPath hand) {
    XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO };
    gi.action = a;
    gi.subactionPath = hand;
    XrActionStatePose st{ XR_TYPE_ACTION_STATE_POSE };
    if (XR_FAILED(xrGetActionStatePose(s, &gi, &st))) return false;
    return st.isActive != XR_FALSE;
}

void XrBackend::syncInput() {
    if (session == XR_NULL_HANDLE || !sessionRunning) return;
    XrActionsSyncInfo si{ XR_TYPE_ACTIONS_SYNC_INFO };
    si.countActiveActionSets = 1;
    XrActiveActionSet aas[1];
    aas[0].actionSet = actionSet;
    aas[0].subactionPath = XR_NULL_PATH;
    si.activeActionSets = aas;
    OXR(xrSyncActions(session, &si));

    inL.grab = getStateFloat(session, actGrabL, handPathL);
    inR.grab = getStateFloat(session, actGrabR, handPathR);
    inL.trigger = getStateFloat(session, actTrigL, handPathL);
    inR.trigger = getStateFloat(session, actTrigR, handPathR);
    inL.triggerClick = inL.trigger > 0.85f;
    inR.triggerClick = inR.trigger > 0.85f;
    inL.stick = getStateVec2(session, actStickL, handPathL);
    inR.stick = getStateVec2(session, actStickR, handPathR);
    inL.menuBtn = getStateBool(session, actMenu, handPathL);
    inL.xBtn = getStateBool(session, actX, handPathL);
    inL.yBtn = getStateBool(session, actY, handPathL);
    inR.aBtn = getStateBool(session, actA, handPathR);
    inR.bBtn = getStateBool(session, actB, handPathR);
    inL.stickClick = getStateBool(session, actClickL, handPathL);
    inR.stickClick = getStateBool(session, actClickR, handPathR);
    inL.connected = getConnected(session, actPoseL, handPathL);
    inR.connected = getConnected(session, actPoseR, handPathR);

    // locate grip spaces
    XrTime t = frameState.predictedDisplayTime;
    auto locate = [&](XrSpace sp, HandInput& in) {
        if (sp == XR_NULL_HANDLE) { in.connected = false; return; }
        XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
        if (XR_SUCCEEDED(xrLocateSpace(sp, viewSpace, t, &loc))) {
            if ((loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) &&
                (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)) {
                poseToMath(loc.pose, in.rot, in.pos);
                in.tracked = (loc.locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT) != 0;
            } else {
                in.connected = false;
            }
        } else {
            in.connected = false;
        }
    };
    locate(gripSpaceL, inL);
    locate(gripSpaceR, inR);

    // derive aim (pointing ray) from grip pose with the standard Touch tilt
    auto aimFrom = [&](HandInput& in) {
        in.aimRot = in.rot * quat::axisAngle(vec3(1, 0, 0), -0.55f);
        in.aimPos = in.pos + in.aimRot.rotate(vec3(0, 0, -0.06f));
    };
    aimFrom(inL);
    aimFrom(inR);
}

bool XrBackend::uiAcquire() {
    XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    OXR(xrAcquireSwapchainImage(uiQuad.swap, &ai, &uiQuadIndex));
    XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = 100000000;
    if (XR_FAILED(xrWaitSwapchainImage(uiQuad.swap, &wi))) return false;
    uiQuadAcquired = true;
    glBindFramebuffer(GL_FRAMEBUFFER, uiQuad.fbo[uiQuadIndex]);
    glViewport(0, 0, uiQuad.w, uiQuad.h);
    return true;
}

void XrBackend::uiRelease() {
    if (!uiQuadAcquired) return;
    XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    OXR(xrReleaseSwapchainImage(uiQuad.swap, &ri));
    uiQuadAcquired = false;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void XrBackend::haptic(int hand, float amplitude, float durationMs) {
    if (session == XR_NULL_HANDLE) return;
    XrHapticActionInfo hai{ XR_TYPE_HAPTIC_ACTION_INFO };
    hai.action = hand == 0 ? actHapL : actHapR;
    hai.subactionPath = hand == 0 ? handPathL : handPathR;
    XrHapticVibration vi{ XR_TYPE_HAPTIC_VIBRATION };
    vi.amplitude = amplitude;
    vi.duration = (int64_t)(durationMs * 1000000.0);
    vi.frequency = XR_FREQUENCY_UNSPECIFIED;
    xrApplyHapticFeedback(session, &hai, (const XrHapticBaseHeader*)&vi);
}

void XrBackend::recenter() {
    if (session == XR_NULL_HANDLE) return;
    float yaw = headRot.yaw();
    quat q = quat::axisAngle(vec3(0, 1, 0), yaw);
    XrReferenceSpaceCreateInfo rci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rci.referenceSpaceType = usingStage ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
    rci.poseInReferenceSpace.orientation = { q.x, q.y, q.z, q.w };
    rci.poseInReferenceSpace.position = { headPos.x, headPos.y, headPos.z };
    XrSpace ns = XR_NULL_HANDLE;
    if (XR_SUCCEEDED(xrCreateReferenceSpace(session, &rci, &ns))) {
        if (viewSpace != XR_NULL_HANDLE) xrDestroySpace(viewSpace);
        viewSpace = ns;
    }
}

void XrBackend::shutdown() {
    for (int i = 0; i < 2; i++) destroySwapchain(&eyes[i]);
    destroySwapchain(&uiQuad);
    if (localSpace != XR_NULL_HANDLE) xrDestroySpace(localSpace);
    if (stageSpace != XR_NULL_HANDLE) xrDestroySpace(stageSpace);
    if (gripSpaceL) xrDestroySpace(gripSpaceL);
    if (gripSpaceR) xrDestroySpace(gripSpaceR);
    if (session != XR_NULL_HANDLE) {
        xrEndSession(session);
        xrDestroySession(session);
        session = XR_NULL_HANDLE;
    }
    if (actionSet != XR_NULL_HANDLE) { xrDestroyActionSet(actionSet); actionSet = XR_NULL_HANDLE; }
    if (instance != XR_NULL_HANDLE) { xrDestroyInstance(instance); instance = XR_NULL_HANDLE; }
}

} // namespace lv
