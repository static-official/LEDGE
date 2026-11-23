// LEDGE VR — renderer: toon pipeline, geometry, sky, loading screen, name tags
#pragma once
#include "mathlib.h"
#include <GLES3/gl32.h>
#include <string>
#include <vector>
#include <functional>

namespace lv {

struct Vertex { vec3 pos; vec3 normal; vec4 color; vec2 uv{0.5f, 0.5f}; };

struct Mesh {
    std::vector<Vertex> verts;
    std::vector<uint32_t> indices;
    GLuint vbo = 0, ibo = 0, vao = 0;
    bool uploaded = false;
    void upload();
    void draw();
    void release();
};

struct WorldTextRenderer {
    // uses ImGui's font atlas as the glyph source
    bool init();
    void setAtlas(GLuint tex, float texW, float texH);
    void begin();   // clear command list
    // draw text at world position, facing camera (billboard), height in meters
    void addText(const std::string& text, const vec3& pos, float height,
                 const vec4& color, bool billboard = true);
    void render(const float* proj, const float* view);
    GLuint program = 0, fontTex = 0;
    float texW = 0, texH = 0;
    vec3 camPos;                    // updated by Renderer::beginFrame (billboarding)
    // simple glyph cache: codepoint -> uv rect (filled from ImGui atlas)
    struct Glyph { uint32_t cp; float u0, v0, u1, v1; float x0, y0, x1, y1; float xadv; };
    std::vector<Glyph> glyphs;
};

struct Renderer {
    bool init();            // compile all shader programs
    void shutdown();

    // toon pipeline
    GLuint toonProg = 0, unlitProg = 0, uiProg = 0, wtextProg = 0;

    void beginFrame(const vec3& camPos);   // clears per-frame state
    // draw a mesh with a model transform + tint
    void drawMesh(const Mesh& m, const mat4& model, const vec4& tint);
    // textured variant: binds `tex` (0 = white fallback), uv * tiling
    void drawMeshTex(const Mesh& m, const mat4& model, const vec4& tint,
                     GLuint tex, const vec2& tiling = vec2(1, 1));
    void drawBox(const vec3& center, const vec3& halfExtents, float rotY,
                 const vec4& color);
    void drawLine(const vec3& a, const vec3& b, const vec4& color);
    void drawGrid(float size, float step, float y, const vec4& color);
    void drawSky();                       // fullscreen gradient via big box
    void setFog(const vec3& color, float density);
    void setLightDir(const vec3& dir);
    void setViewProj(const float* proj, const float* view);
    void setViewProjEye(const float* proj, const float* view, int eye);

    WorldTextRenderer wtext;

    // line batch
    std::vector<Vertex> lineVerts;
    Mesh lineMesh;
    bool lineDirty = false;

    vec3 camPos;
    vec3 lightDir = norm(vec3(0.35f, 1.0f, 0.25f));
    vec3 fogColor = vec3(0.72f, 0.87f, 0.91f);
    float fogDensity = 0.012f;
    mat4 proj, view;
};

// geometry builders
void buildBoxMesh(Mesh& m, const vec3& half, const vec4& color);
// rounded-edge box (bevelled corners, per-face meter UVs) — the game-block look
void buildRoundedBoxMesh(Mesh& m, const vec3& half, const vec4& color);
void buildSphereMesh(Mesh& m, float radius, int segs, const vec4& color);
void buildCapsuleMesh(Mesh& m, float radius, float height, const vec4& color); // avatar body
void buildCylinderMesh(Mesh& m, float radius, float height, int segs, const vec4& color);
void buildConeMesh(Mesh& m, float radius, float height, int segs, const vec4& color);
void buildPlaneMesh(Mesh& m, float w, float h, const vec4& color);
void buildQuad2DMesh(Mesh& m, float w, float h); // XY plane, z=0, for UI quads

// flush accumulated debug/aim lines (call once per eye after opaque draws)
void rendererFlushLines(Renderer& R);

extern const char* VS_TOON_E;
extern const char* FS_TOON_E;
extern const char* VS_UNLIT_E;
extern const char* FS_UNLIT_E;

} // namespace lv
