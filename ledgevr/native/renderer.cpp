// LEDGE VR — renderer implementation
#include "renderer.h"
#include "xr_backend.h"
#include "assets.h"
#include "log.h"
#include <imgui.h>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace lv {

// ---------------------------------------------------------------------------
void Mesh::upload() {
    if (uploaded || verts.empty()) return;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glGenBuffers(1, &ibo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);
    // layout matching toon/unlit: pos(0) normal(1) color(2) uv(3)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    glBindVertexArray(0);
    uploaded = true;
}

void Mesh::draw() {
    if (!uploaded) upload();
    if (!uploaded || indices.empty()) return;
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void Mesh::release() {
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ibo) glDeleteBuffers(1, &ibo);
    if (vao) glDeleteVertexArrays(1, &vao);
    vbo = ibo = vao = 0;
    uploaded = false;
}

// ---------------------------------------------------------------------------
bool Renderer::init() {
    GLuint vs = lvCompileShader(GL_VERTEX_SHADER, VS_TOON);
    GLuint fs = lvCompileShader(GL_FRAGMENT_SHADER, FS_TOON);
    toonProg = lvLinkProgram(vs, fs);
    vs = lvCompileShader(GL_VERTEX_SHADER, VS_UNLIT);
    fs = lvCompileShader(GL_FRAGMENT_SHADER, FS_UNLIT);
    unlitProg = lvLinkProgram(vs, fs);
    vs = lvCompileShader(GL_VERTEX_SHADER, VS_UI);
    fs = lvCompileShader(GL_FRAGMENT_SHADER, FS_UI);
    uiProg = lvLinkProgram(vs, fs);
    vs = lvCompileShader(GL_VERTEX_SHADER, VS_WTEXT);
    fs = lvCompileShader(GL_FRAGMENT_SHADER, FS_WTEXT);
    wtextProg = lvLinkProgram(vs, fs);
    wtext.program = wtextProg;
    wtext.init();
    LOGI("renderer init: toon=%u unlit=%u ui=%u wtext=%u", toonProg, unlitProg, uiProg, wtextProg);
    return toonProg && unlitProg && uiProg && wtextProg;
}

void Renderer::shutdown() {
    if (toonProg) glDeleteProgram(toonProg);
    if (unlitProg) glDeleteProgram(unlitProg);
    if (uiProg) glDeleteProgram(uiProg);
    if (wtextProg) glDeleteProgram(wtextProg);
}

void Renderer::setFog(const vec3& c, float d) { fogColor = c; fogDensity = d; }
void Renderer::setLightDir(const vec3& d) { lightDir = norm(d); }

void Renderer::beginFrame(const vec3& cp) {
    camPos = cp;
    wtext.camPos = cp;
    lineVerts.clear();
}

void Renderer::setViewProjEye(const float* p, const float* v, int eye) {
    (void)eye;
    memcpy(proj.m, p, sizeof(float) * 16);
    memcpy(view.m, v, sizeof(float) * 16);
}

void Renderer::drawMesh(const Mesh& m, const mat4& model, const vec4& tint) {
    drawMeshTex(m, model, tint, 0, vec2(1, 1));
}

void Renderer::drawMeshTex(const Mesh& m, const mat4& model, const vec4& tint,
                           GLuint tex, const vec2& tiling) {
    if (m.verts.empty()) return;
    glUseProgram(toonProg);
    glUniformMatrix4fv(glGetUniformLocation(toonProg, "uProj"), 1, GL_FALSE, proj.m);
    glUniformMatrix4fv(glGetUniformLocation(toonProg, "uView"), 1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(toonProg, "uModel"), 1, GL_FALSE, model.m);
    glUniform3f(glGetUniformLocation(toonProg, "uLightDir"), lightDir.x, lightDir.y, lightDir.z);
    glUniform3f(glGetUniformLocation(toonProg, "uCamPos"), camPos.x, camPos.y, camPos.z);
    glUniform3f(glGetUniformLocation(toonProg, "uFogColor"), fogColor.x, fogColor.y, fogColor.z);
    glUniform1f(glGetUniformLocation(toonProg, "uFogDensity"), fogDensity);
    glUniform4f(glGetUniformLocation(toonProg, "uTint"), tint.x, tint.y, tint.z, tint.w);
    glUniform1f(glGetUniformLocation(toonProg, "uUseTex"), tex ? 1.0f : 0.0f);
    glUniform2f(glGetUniformLocation(toonProg, "uUvTiling"), tiling.x, tiling.y);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glUniform1i(glGetUniformLocation(toonProg, "uTex"), 0);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glCullFace(GL_BACK);
    const_cast<Mesh&>(m).draw();
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::drawBox(const vec3& center, const vec3& he, float rotY, const vec4& color) {
    static Mesh box;
    static vec3 lastHe(0, 0, 0);
    if (lastHe.x != he.x || lastHe.y != he.y || lastHe.z != he.z) {
        buildBoxMesh(box, he, vec4(1, 1, 1, 1));
        box.release();
        box.upload();
        lastHe = he;
    }
    mat4 model = mat4::translate(center) * mat4::rotY(rotY);
    drawMesh(box, model, color);
}

void Renderer::drawLine(const vec3& a, const vec3& b, const vec4& color) {
    vec3 n(0, 1, 0);
    lineVerts.push_back({a, n, color});
    lineVerts.push_back({b, n, color});
    lineDirty = true;
}

void Renderer::drawSky() {
    // big inverted box with pastel gradient (vertex colors), drawn first, depth write off
    static Mesh sky;
    static bool built = false;
    if (!built) {
        float R = 300.0f;
        buildBoxMesh(sky, vec3(R, R, R), vec4(1, 1, 1, 1));
        // vertical gradient: sky blue top, pale horizon
        for (auto& v : sky.verts) {
            float t = (v.pos.y / R) * 0.5f + 0.5f;
            vec3 top(0.55f, 0.78f, 0.90f), bot(0.86f, 0.94f, 0.97f);
            v.color = vec4(bot + (top - bot) * t, 1.0f);
            v.normal = -v.normal; // inside faces
        }
        for (size_t i = 0; i + 2 < sky.indices.size(); i += 3)
            std::swap(sky.indices[i + 1], sky.indices[i + 2]);
        sky.upload();
        built = true;
    }
    mat4 model = mat4::translate(camPos);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glUseProgram(toonProg);
    glUniformMatrix4fv(glGetUniformLocation(toonProg, "uProj"), 1, GL_FALSE, proj.m);
    glUniformMatrix4fv(glGetUniformLocation(toonProg, "uView"), 1, GL_FALSE, view.m);
    glUniformMatrix4fv(glGetUniformLocation(toonProg, "uModel"), 1, GL_FALSE, model.m);
    glUniform3f(glGetUniformLocation(toonProg, "uLightDir"), 0, 1, 0);
    glUniform3f(glGetUniformLocation(toonProg, "uCamPos"), camPos.x, camPos.y, camPos.z);
    glUniform3f(glGetUniformLocation(toonProg, "uFogColor"), fogColor.x, fogColor.y, fogColor.z);
    glUniform1f(glGetUniformLocation(toonProg, "uFogDensity"), 0.0f);
    glUniform4f(glGetUniformLocation(toonProg, "uTint"), 1, 1, 1, 1);
    sky.draw();
    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
}

void Renderer::drawGrid(float size, float step, float y, const vec4& color_) {
    (void)color_;
    vec4 c(1, 1, 1, 0.35f);
    for (float x = -size; x <= size + 0.001f; x += step) {
        drawLine(vec3(x, y, -size), vec3(x, y, size), c);
        drawLine(vec3(-size, y, x), vec3(size, y, x), c);
    }
}

void Renderer::setViewProj(const float* p, const float* v) {
    memcpy(proj.m, p, sizeof(float) * 16);
    memcpy(view.m, v, sizeof(float) * 16);
}

// flush accumulated lines (call once per eye after opaque draws)
void rendererFlushLines(Renderer& R) {
    if (R.lineVerts.empty()) return;
    static Mesh lm;
    lm.release();
    lm.verts = R.lineVerts;
    lm.indices.clear();
    for (uint32_t i = 0; i < R.lineVerts.size(); i++) lm.indices.push_back(i);
    lm.upload();
    glUseProgram(R.unlitProg);
    glUniformMatrix4fv(glGetUniformLocation(R.unlitProg, "uProj"), 1, GL_FALSE, R.proj.m);
    glUniformMatrix4fv(glGetUniformLocation(R.unlitProg, "uView"), 1, GL_FALSE, R.view.m);
    glUniformMatrix4fv(glGetUniformLocation(R.unlitProg, "uModel"), 1, GL_FALSE, mat4::identity().m);
    glUniform4f(glGetUniformLocation(R.unlitProg, "uTint"), 1, 1, 1, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glLineWidth(2.0f);
    glDepthMask(GL_FALSE);
    lm.draw();
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    R.lineVerts.clear();
}

// ---------------------------------------------------------------------------
// geometry builders
// ---------------------------------------------------------------------------
void buildBoxMesh(Mesh& m, const vec3& he, const vec4& color) {
    m.verts.clear(); m.indices.clear();
    vec3 corners[8] = {
        {-he.x, -he.y, -he.z}, {he.x, -he.y, -he.z}, {he.x, he.y, -he.z}, {-he.x, he.y, -he.z},
        {-he.x, -he.y, he.z}, {he.x, -he.y, he.z}, {he.x, he.y, he.z}, {-he.x, he.y, he.z}};
    vec3 normals[6] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}};
    int faces[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7}, {1, 5, 6, 2}, {4, 5, 1, 0}, {3, 2, 6, 7}};
    for (int f = 0; f < 6; f++) {
        uint32_t base = (uint32_t)m.verts.size();
        for (int i = 0; i < 4; i++)
            m.verts.push_back({corners[faces[f][i]], normals[f], color});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

void buildSphereMesh(Mesh& m, float radius, int segs, const vec4& color) {
    m.verts.clear(); m.indices.clear();
    for (int y = 0; y <= segs; y++) {
        float v = (float)y / segs;
        float phi = v * 3.14159265f;
        for (int x = 0; x <= segs; x++) {
            float u = (float)x / segs;
            float theta = u * 6.2831853f;
            vec3 n(sinf(phi) * cosf(theta), cosf(phi), sinf(phi) * sinf(theta));
            m.verts.push_back({n * radius, n, color, vec2(u, v)});
        }
    }
    for (int y = 0; y < segs; y++)
        for (int x = 0; x < segs; x++) {
            uint32_t a = y * (segs + 1) + x, b = a + segs + 1;
            m.indices.insert(m.indices.end(), {a, b, a + 1, a + 1, b, b + 1});
        }
}

void buildRoundedBoxMesh(Mesh& m, const vec3& he, const vec4& color) {
    // rounded box: 6 face grids projected onto the rounded-box surface
    // (clamp interior + sphere cap of radius r). Per-meter UVs per face.
    m.verts.clear(); m.indices.clear();
    float r = std::min(std::min(he.x, std::min(he.y, he.z)) * 0.22f, 0.16f);
    if (r < 0.02f) r = 0.02f;
    vec3 inner = he - vec3(r, r, r);
    const int N = 6;  // grid subdivision per face edge
    // face data: normal, tangent(u), bitangent(v) chosen so tang x bitang = normal,
    // plus the u/v half extents and the offset along the normal.
    struct Face { vec3 n, t, b; float hu, hv, off; };
    const Face faces[6] = {
        { vec3(0, 0, -1), vec3(0, 1, 0), vec3(1, 0, 0), he.y, he.x, he.z },
        { vec3(0, 0,  1), vec3(1, 0, 0), vec3(0, 1, 0), he.x, he.y, he.z },
        { vec3(-1, 0, 0), vec3(0, 0, 1), vec3(0, 1, 0), he.z, he.y, he.x },
        { vec3( 1, 0, 0), vec3(0, 1, 0), vec3(0, 0, 1), he.y, he.z, he.x },
        { vec3(0,-1,  0), vec3(1, 0, 0), vec3(0, 0, 1), he.x, he.z, he.y },
        { vec3(0, 1,  0), vec3(0, 0, 1), vec3(1, 0, 0), he.z, he.x, he.y },
    };
    for (int f = 0; f < 6; f++) {
        const Face& F = faces[f];
        uint32_t base = (uint32_t)m.verts.size();
        for (int gy = 0; gy <= N; gy++) {
            for (int gx = 0; gx <= N; gx++) {
                float su = (gx / (float)N * 2 - 1) * F.hu;
                float sv = (gy / (float)N * 2 - 1) * F.hv;
                vec3 p = F.t * su + F.b * sv + F.n * F.off;   // on the face plane
                vec3 q(std::max(-inner.x, std::min(inner.x, p.x)),
                       std::max(-inner.y, std::min(inner.y, p.y)),
                       std::max(-inner.z, std::min(inner.z, p.z)));
                vec3 dir = p - q;
                float dl = len(dir);
                if (dl > 1e-6f) dir = dir * (1.0f / dl);
                else dir = F.n;
                vec3 sp = q + dir * r;
                m.verts.push_back({sp, dir, color, vec2(su, sv)});
            }
        }
        for (int gy = 0; gy < N; gy++)
            for (int gx = 0; gx < N; gx++) {
                uint32_t a = base + gy * (N + 1) + gx, b = a + (N + 1);
                m.indices.insert(m.indices.end(), {a, a + 1, b, a + 1, b + 1, b});
            }
    }
}

void buildCylinderMesh(Mesh& m, float radius, float height, int segs, const vec4& color) {
    m.verts.clear(); m.indices.clear();
    float h = height * 0.5f;
    for (int s = 0; s <= segs; s++) {
        float a = (float)s / segs * 6.2831853f;
        float u = (float)s / segs;
        vec3 n(cosf(a), 0, sinf(a));
        m.verts.push_back({vec3(n.x * radius, -h, n.z * radius), n, color, vec2(u, 0)});
        m.verts.push_back({vec3(n.x * radius, h, n.z * radius), n, color, vec2(u, 1)});
    }
    for (int s = 0; s < segs; s++) {
        uint32_t a = s * 2;
        m.indices.insert(m.indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
    }
    // caps
    uint32_t base = (uint32_t)m.verts.size();
    m.verts.push_back({vec3(0, h, 0), vec3(0, 1, 0), color, vec2(0.5f, 0.5f)});
    m.verts.push_back({vec3(0, -h, 0), vec3(0, -1, 0), color, vec2(0.5f, 0.5f)});
    for (int s = 0; s < segs; s++) {
        float a1 = (float)s / segs * 6.2831853f, a2 = (float)(s + 1) / segs * 6.2831853f;
        float u1 = (float)s / segs, u2 = (float)(s + 1) / segs;
        m.verts.push_back({vec3(cosf(a1) * radius, h, sinf(a1) * radius), vec3(0, 1, 0), color, vec2(u1, 1)});
        m.verts.push_back({vec3(cosf(a2) * radius, h, sinf(a2) * radius), vec3(0, 1, 0), color, vec2(u2, 1)});
        m.verts.push_back({vec3(cosf(a1) * radius, -h, sinf(a1) * radius), vec3(0, -1, 0), color, vec2(u1, 0)});
        m.verts.push_back({vec3(cosf(a2) * radius, -h, sinf(a2) * radius), vec3(0, -1, 0), color, vec2(u2, 0)});
    }
    for (int s = 0; s < segs; s++) {
        uint32_t b = base + 2 + s * 4;
        m.indices.insert(m.indices.end(), {base, b + 1, b, base + 1, b + 2, b + 3});
    }
}

void buildConeMesh(Mesh& m, float radius, float height, int segs, const vec4& color) {
    // cone pointing +Y, base at -h
    m.verts.clear(); m.indices.clear();
    float h = height * 0.5f;
    for (int s = 0; s <= segs; s++) {
        float a = (float)s / segs * 6.2831853f;
        float u = (float)s / segs;
        // slant normal
        vec3 n(cosf(a), 0.45f, sinf(a));
        n = norm(n);
        m.verts.push_back({vec3(cosf(a) * radius, -h, sinf(a) * radius), n, color, vec2(u, 0)});
        m.verts.push_back({vec3(0, h, 0), n, color, vec2(u, 1)});
    }
    for (int s = 0; s < segs; s++) {
        uint32_t a = s * 2;
        m.indices.insert(m.indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
    }
    // base cap
    uint32_t base = (uint32_t)m.verts.size();
    m.verts.push_back({vec3(0, -h, 0), vec3(0, -1, 0), color, vec2(0.5f, 0.5f)});
    for (int s = 0; s < segs; s++) {
        float a1 = (float)s / segs * 6.2831853f, a2 = (float)(s + 1) / segs * 6.2831853f;
        m.verts.push_back({vec3(cosf(a1) * radius, -h, sinf(a1) * radius), vec3(0, -1, 0), color,
                           vec2((float)s / segs, 0)});
        m.verts.push_back({vec3(cosf(a2) * radius, -h, sinf(a2) * radius), vec3(0, -1, 0), color,
                           vec2((float)(s + 1) / segs, 0)});
    }
    for (int s = 0; s < segs; s++) {
        uint32_t b = base + 1 + s * 2;
        m.indices.insert(m.indices.end(), {base, b, b + 1});
    }
}

void buildCapsuleMesh(Mesh& m, float radius, float height, const vec4& color) {
    // body = cylinder; caps handled by sphere overlay at render time
    buildCylinderMesh(m, radius, height, 12, color);
}

void buildPlaneMesh(Mesh& m, float w, float h, const vec4& color) {
    m.verts.clear(); m.indices.clear();
    float hw = w * 0.5f, hh = h * 0.5f;
    m.verts.push_back({vec3(-hw, 0, -hh), vec3(0, 1, 0), color, vec2(0, 0)});
    m.verts.push_back({vec3(hw, 0, -hh), vec3(0, 1, 0), color, vec2(w, 0)});
    m.verts.push_back({vec3(hw, 0, hh), vec3(0, 1, 0), color, vec2(w, h)});
    m.verts.push_back({vec3(-hw, 0, hh), vec3(0, 1, 0), color, vec2(0, h)});
    m.indices = {0, 1, 2, 0, 2, 3};
}

void buildQuad2DMesh(Mesh& m, float w, float h) {
    m.verts.clear(); m.indices.clear();
    float hw = w * 0.5f, hh = h * 0.5f;
    m.verts.push_back({vec3(-hw, -hh, 0), vec3(0, 0, 1), vec4(1, 1, 1, 1)});
    m.verts.push_back({vec3(hw, -hh, 0), vec3(0, 0, 1), vec4(1, 1, 1, 1)});
    m.verts.push_back({vec3(hw, hh, 0), vec3(0, 0, 1), vec4(1, 1, 1, 1)});
    m.verts.push_back({vec3(-hw, hh, 0), vec3(0, 0, 1), vec4(1, 1, 1, 1)});
    m.indices = {0, 1, 2, 0, 2, 3};
}

// ---------------------------------------------------------------------------
// world text (name tags) using the ImGui font atlas
// ---------------------------------------------------------------------------
bool WorldTextRenderer::init() {
    // program compiled by Renderer::init; glyphs filled after ImGui init
    return true;
}

void WorldTextRenderer::setAtlas(GLuint tex, float w, float h) {
    fontTex = tex; texW = w; texH = h;
    glyphs.clear();
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    if (!atlas) return;
    ImFont* font = atlas->Fonts[0];
    for (int cp = 32; cp < 0x2FFF; cp++) {
        const ImWchar c = (ImWchar)cp;
        const ImFontGlyph* g = font->FindGlyphNoFallback(c);
        if (!g || g->Visible == 0) continue;
        Glyph gl;
        gl.cp = (uint32_t)cp;
        gl.u0 = g->U0; gl.v0 = g->V0; gl.u1 = g->U1; gl.v1 = g->V1;
        gl.x0 = g->X0; gl.y0 = g->Y0; gl.x1 = g->X1; gl.y1 = g->Y1;
        gl.xadv = g->AdvanceX;
        glyphs.push_back(gl);
    }
}

struct WTextCmd { std::string text; vec3 pos; float height; vec4 color; bool billboard; };
static std::vector<WTextCmd> g_wtextCmds;

void WorldTextRenderer::begin() { g_wtextCmds.clear(); }

void WorldTextRenderer::addText(const std::string& text, const vec3& pos, float height,
                                const vec4& color, bool billboard) {
    g_wtextCmds.push_back({text, pos, height, color, billboard});
}

void WorldTextRenderer::render(const float* proj, const float* view) {
    if (g_wtextCmds.empty() || glyphs.empty() || !fontTex) return;
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    float fontPx = atlas->Fonts[0]->FontSize;

    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "uProj"), 1, GL_FALSE, proj);
    glUniformMatrix4fv(glGetUniformLocation(program, "uView"), 1, GL_FALSE, view);
    glUniform1i(glGetUniformLocation(program, "uTex"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTex);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    for (auto& cmd : g_wtextCmds) {
        float scale = cmd.height / fontPx;
        // build transform: billboard facing camera or fixed yaw
        float width = 0;
        for (char c : cmd.text) {
            uint8_t uc = (uint8_t)c;
            for (auto& g : glyphs) if (g.cp == uc) { width += g.xadv * scale; break; }
        }
        mat4 model;
        if (cmd.billboard) {
            // face camera: yaw only
            vec3 dir = norm(vec3(cmd.pos.x - camPos.x, 0, cmd.pos.z - camPos.z));
            float yaw = atan2f(dir.x, dir.z);
            model = mat4::translate(cmd.pos) * mat4::rotY(yaw) * mat4::scale(vec3(scale, scale, scale));
            // shift left by half width (in font units / scale)
            model = model * mat4::translate(vec3(-width / scale * 0.5f, 0, 0));
        } else {
            model = mat4::translate(cmd.pos) * mat4::scale(vec3(scale, scale, scale));
        }
        glUniformMatrix4fv(glGetUniformLocation(program, "uModel"), 1, GL_FALSE, model.m);

        static GLuint vao = 0, vbo = 0;
        if (!vao) {
            glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 8, (void*)0);
            glEnableVertexAttribArray(1); glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 8, (void*)(sizeof(float) * 2));
            glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 8, (void*)(sizeof(float) * 4));
        }
        std::vector<float> data;
        float cx = 0;
        for (char c : cmd.text) {
            uint8_t uc = (uint8_t)c;
            for (auto& g : glyphs) {
                if (g.cp != uc) continue;
                float x0 = cx + g.x0, y0 = -g.y0, x1 = cx + g.x1, y1 = -g.y1;
                data.insert(data.end(), {
                    x0, y0, g.u0, g.v0, cmd.color.x, cmd.color.y, cmd.color.z, cmd.color.w,
                    x1, y0, g.u1, g.v0, cmd.color.x, cmd.color.y, cmd.color.z, cmd.color.w,
                    x1, y1, g.u1, g.v1, cmd.color.x, cmd.color.y, cmd.color.z, cmd.color.w,
                    x0, y0, g.u0, g.v0, cmd.color.x, cmd.color.y, cmd.color.z, cmd.color.w,
                    x1, y1, g.u1, g.v1, cmd.color.x, cmd.color.y, cmd.color.z, cmd.color.w,
                    x0, y1, g.u0, g.v1, cmd.color.x, cmd.color.y, cmd.color.z, cmd.color.w});
                cx += g.xadv;
                break;
            }
        }
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(data.size() / 8));
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}

} // namespace lv
