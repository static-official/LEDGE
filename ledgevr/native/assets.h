// LEDGE VR — embedded assets: OpenXR action manifest + GLSL ES 3.2 shaders.
// All shaders are compiled at runtime (no offline SPIR-V pipeline).
#pragma once
#include <cstdint>

namespace lv {

// ---------------------------------------------------------------------------
// Action manifest — Quest Touch profile first, Khronos simple fallback.
// The "three lines" (hamburger) button on the LEFT Touch controller is the
// Y button -> action menuLeft, which opens the in-game menu.
// ---------------------------------------------------------------------------
static const char* ACTION_MANIFEST_JSON = R"JSON(
{
  "action_sets": [
    {
      "name": "ledgevr",
      "localized_name": "LEDGE VR Actions",
      "priority": 0
    }
  ],
  "actions": [
    { "name": "pose_left",    "localized_name": "Left Hand Pose",   "type": "pose" },
    { "name": "pose_right",   "localized_name": "Right Hand Pose",  "type": "pose" },
    { "name": "grab_left",    "localized_name": "Left Grab",        "type": "float" },
    { "name": "grab_right",   "localized_name": "Right Grab",       "type": "float" },
    { "name": "trigger_left", "localized_name": "Left Trigger",     "type": "float" },
    { "name": "trigger_right","localized_name": "Right Trigger",    "type": "float" },
    { "name": "stick_left",   "localized_name": "Left Thumbstick",  "type": "vector2f" },
    { "name": "stick_right",  "localized_name": "Right Thumbstick", "type": "vector2f" },
    { "name": "menu_left",    "localized_name": "Open Menu (three lines)", "type": "boolean" },
    { "name": "x_left",       "localized_name": "Left X Button",    "type": "boolean" },
    { "name": "y_left",       "localized_name": "Left Y Button",    "type": "boolean" },
    { "name": "a_right",      "localized_name": "Right A Button",   "type": "boolean" },
    { "name": "b_right",      "localized_name": "Right B Button",   "type": "boolean" },
    { "name": "click_left",   "localized_name": "Left Stick Click", "type": "boolean" },
    { "name": "click_right",  "localized_name": "Right Stick Click","type": "boolean" },
    { "name": "haptic_left",  "localized_name": "Left Vibration",   "type": "vibration" },
    { "name": "haptic_right", "localized_name": "Right Vibration",  "type": "vibration" }
  ],
  "default_bindings": [
    {
      "controller_type": "oculus_touch",
      "binding": "/interaction_profiles/oculus/touch_controller"
    }
  ],
  "bindings": [
    {
      "binding": "/interaction_profiles/oculus/touch_controller",
      "localized_name": "Meta Quest Touch Controllers",
      "inputs": [
        { "code": "/user/hand/left/input/grip/pose",       "path": "/user/hand/left/input/pose_left",     "inputs": [] },
        { "code": "/user/hand/left/input/aim/pose",        "path": "/user/hand/left/input/pose_left",     "inputs": [] },
        { "code": "/user/hand/right/input/grip/pose",      "path": "/user/hand/right/input/pose_right",   "inputs": [] },
        { "code": "/user/hand/right/input/aim/pose",       "path": "/user/hand/right/input/pose_right",   "inputs": [] },
        { "code": "/user/hand/left/input/squeeze/click",   "path": "/user/hand/left/input/grab_left" },
        { "code": "/user/hand/right/input/squeeze/click",  "path": "/user/hand/right/input/grab_right" },
        { "code": "/user/hand/left/input/trigger/value",   "path": "/user/hand/left/input/trigger_left" },
        { "code": "/user/hand/right/input/trigger/value",  "path": "/user/hand/right/input/trigger_right" },
        { "code": "/user/hand/left/input/thumbstick",      "path": "/user/hand/left/input/stick_left" },
        { "code": "/user/hand/right/input/thumbstick",     "path": "/user/hand/right/input/stick_right" },
        { "code": "/user/hand/left/input/menu/click",   "path": "/user/hand/left/input/menu_left" },
        { "code": "/user/hand/left/input/x/click",         "path": "/user/hand/left/input/x_left" },
        { "code": "/user/hand/left/input/y/click",         "path": "/user/hand/left/input/y_left" },
        { "code": "/user/hand/left/input/y/touch",         "path": "/user/hand/left/input/y_left" },
        { "code": "/user/hand/right/input/a/click",        "path": "/user/hand/right/input/a_right" },
        { "code": "/user/hand/right/input/b/click",        "path": "/user/hand/right/input/b_right" },
        { "code": "/user/hand/left/input/thumbstick/click","path": "/user/hand/left/input/click_left" },
        { "code": "/user/hand/right/input/thumbstick/click","path": "/user/hand/right/input/click_right" },
        { "code": "/user/hand/left/output/haptic",         "path": "/user/hand/left/output/haptic_left" },
        { "code": "/user/hand/right/output/haptic",        "path": "/user/hand/right/output/haptic_right" }
      ],
      "outputs": []
    },
    {
      "binding": "/interaction_profiles/khronos/simple_controller",
      "localized_name": "Khronos Simple Controller",
      "inputs": [
        { "code": "/user/hand/left/input/grip/pose",  "path": "/user/hand/left/input/pose_left" },
        { "code": "/user/hand/left/input/aim/pose",   "path": "/user/hand/left/input/pose_left" },
        { "code": "/user/hand/right/input/grip/pose", "path": "/user/hand/right/input/pose_right" },
        { "code": "/user/hand/right/input/aim/pose",  "path": "/user/hand/right/input/pose_right" },
        { "code": "/user/hand/left/input/select/click",  "path": "/user/hand/left/input/trigger_left" },
        { "code": "/user/hand/right/input/select/click", "path": "/user/hand/right/input/trigger_right" },
        { "code": "/user/hand/left/input/menu/click", "path": "/user/hand/left/input/menu_left" },
        { "code": "/user/hand/right/input/menu/click","path": "/user/hand/right/input/menu_left" },
        { "code": "/user/hand/left/output/haptic",    "path": "/user/hand/left/output/haptic_left" },
        { "code": "/user/hand/right/output/haptic",   "path": "/user/hand/right/output/haptic_right" }
      ],
      "outputs": []
    }
  ],
  "modes": [
    {
      "mode": "xr-core",
      "localized_name": "XR Core",
      "conflicts": []
    }
  ]
}
)JSON";

// ---------------------------------------------------------------------------
// Shaders — GLSL ES 3.2, compiled at runtime.
// ---------------------------------------------------------------------------

// Toon shader: flat quantized lighting + rim light + distance fog. Pastel look.
// 1.1: optional material texture (uUseTex) with per-meter tiling (uUvTiling).
static const char* VS_TOON = R"GLSL(#version 320 es
precision highp float;
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aColor;
layout(location=3) in vec2 aUV;
uniform mat4 uProj, uView, uModel;
uniform vec2 uUvTiling;
out vec3 vNormal;
out vec4 vColor;
out vec2 vUV;
out vec3 vWorldPos;
void main(){
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    mat3 nm = mat3(uModel);
    vNormal = normalize(nm * aNormal);
    vColor = aColor;
    vUV = aUV * uUvTiling;
    gl_Position = uProj * uView * world;
}
)GLSL";

static const char* FS_TOON = R"GLSL(#version 320 es
precision highp float;
in vec3 vNormal;
in vec4 vColor;
in vec2 vUV;
in vec3 vWorldPos;
uniform vec3 uLightDir;   // normalized, world space
uniform vec3 uCamPos;
uniform vec3 uFogColor;
uniform float uFogDensity;
uniform vec4 uTint;       // multiplies vColor
uniform float uUseTex;    // >0.5: sample uTex
uniform vec2 uUvTiling;
uniform sampler2D uTex;
out vec4 fragColor;
void main(){
    vec3 n = normalize(vNormal);
    float ndl = dot(n, normalize(uLightDir));
    // 3-band toon ramp
    float t = ndl > 0.55 ? 1.0 : (ndl > 0.15 ? 0.72 : 0.5);
    vec3 base = vColor.rgb * uTint.rgb;
    if (uUseTex > 0.5) base *= texture(uTex, vUV).rgb;
    vec3 col = base * t;
    // rim light
    vec3 v = normalize(uCamPos - vWorldPos);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0) * 0.35;
    col += vec3(0.95, 0.93, 0.9) * rim;
    // exp2 fog
    float dist = length(uCamPos - vWorldPos);
    float fog = 1.0 - exp(-uFogDensity * uFogDensity * dist * dist);
    col = mix(col, uFogColor, clamp(fog, 0.0, 1.0));
    fragColor = vec4(col, vColor.a * uTint.a);
}
)GLSL";

// Unlit shader: solid color (loading screen, lines, gizmos, sky quads).
static const char* VS_UNLIT = R"GLSL(#version 320 es
precision highp float;
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec4 aColor;
uniform mat4 uProj, uView, uModel;
out vec4 vColor;
void main(){
    vColor = aColor;
    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);
}
)GLSL";

static const char* FS_UNLIT = R"GLSL(#version 320 es
precision highp float;
in vec4 vColor;
uniform vec4 uTint;
out vec4 fragColor;
void main(){
    fragColor = vColor * uTint;
}
)GLSL";

// ImGui-style UI shader (renders into the menu texture and world text).
static const char* VS_UI = R"GLSL(#version 320 es
precision highp float;
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
uniform mat4 uProj;
out vec2 vUV;
out vec4 vColor;
void main(){
    vUV = aUV;
    vColor = aColor;
    gl_Position = uProj * vec4(aPos, 0.0, 1.0);
}
)GLSL";

static const char* FS_UI = R"GLSL(#version 320 es
precision highp float;
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 fragColor;
void main(){
    vec4 t = texture(uTex, vUV);
    fragColor = vColor * t;
    if (fragColor.a < 0.003) discard;
}
)GLSL";

// In-world toon shader for text/name tags: unlit, samples atlas, keeps alpha.
// uModel places/scales the glyph quads (billboard for world text, pixel-space for HUD).
static const char* VS_WTEXT = R"GLSL(#version 320 es
precision highp float;
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
uniform mat4 uProj, uView, uModel;
out vec2 vUV;
out vec4 vColor;
void main(){
    vUV = aUV;
    vColor = aColor;
    gl_Position = uProj * uView * uModel * vec4(aPos, 0.0, 1.0);
}
)GLSL";

static const char* FS_WTEXT = R"GLSL(#version 320 es
precision highp float;
in vec2 vUV;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 fragColor;
void main(){
    float a = texture(uTex, vUV).a;
    fragColor = vec4(vColor.rgb, vColor.a * a);
    if (fragColor.a < 0.01) discard;
}
)GLSL";

} // namespace lv
