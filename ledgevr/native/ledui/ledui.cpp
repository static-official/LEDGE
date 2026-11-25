// LEDGE UI (ledui) — implementation. See ledui.h for the design notes.
#include "ledui.h"
#include "ledui_font_data.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <string>

namespace ledui {

static Context* g_ctx = nullptr;

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static uint32_t packCol(const Vec4& c) {
    uint32_t r = (uint32_t)(c.x * 255.0f + 0.5f);
    uint32_t g = (uint32_t)(c.y * 255.0f + 0.5f);
    uint32_t b = (uint32_t)(c.z * 255.0f + 0.5f);
    uint32_t a = (uint32_t)(c.w * 255.0f + 0.5f);
    return (a << 24) | (b << 16) | (g << 8) | r;   // RGBA8, little-endian ABGR order
}

// ---------------------------------------------------------------------------
// context / style
// ---------------------------------------------------------------------------
Context::Context() {
    io.Fonts = &atlas;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    startTimeUs = (unsigned long long)ts.tv_sec * 1000000ULL + (unsigned long long)(ts.tv_nsec / 1000);
    StyleColorsDark();
}

bool CreateContext() {
    if (g_ctx) return true;
    g_ctx = new Context();
    return true;
}
void DestroyContext() { delete g_ctx; g_ctx = nullptr; }
Context* GetCurrentContext() { return g_ctx; }
IO& GetIO() { return g_ctx->io; }
Style& GetStyle() { return g_ctx->style; }
double GetTime() {
    if (!g_ctx) return 0;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long long now = (unsigned long long)ts.tv_sec * 1000000ULL + (unsigned long long)(ts.tv_nsec / 1000);
    return (double)(now - g_ctx->startTimeUs) / 1e6;
}

void StyleColorsDark() {
    if (!g_ctx) return;
    Vec4* c = g_ctx->style.Colors;
    c[Col_Text]            = Vec4(1.00f, 1.00f, 1.00f, 1.00f);
    c[Col_TextDisabled]    = Vec4(0.50f, 0.50f, 0.50f, 1.00f);
    c[Col_WindowBg]        = Vec4(0.00f, 0.00f, 0.00f, 0.94f);
    c[Col_ChildBg]         = Vec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[Col_PopupBg]         = Vec4(0.08f, 0.08f, 0.08f, 0.94f);
    c[Col_Border]          = Vec4(0.43f, 0.43f, 0.50f, 0.50f);
    c[Col_FrameBg]         = Vec4(0.16f, 0.29f, 0.48f, 0.54f);
    c[Col_FrameBgHovered]  = Vec4(0.26f, 0.59f, 0.98f, 0.46f);
    c[Col_FrameBgActive]   = Vec4(0.26f, 0.59f, 0.98f, 0.67f);
    c[Col_TitleBg]         = Vec4(0.06f, 0.06f, 0.09f, 0.82f);
    c[Col_TitleBgActive]   = Vec4(0.16f, 0.29f, 0.48f, 1.00f);
    c[Col_Button]          = Vec4(0.26f, 0.59f, 0.98f, 0.40f);
    c[Col_ButtonHovered]   = Vec4(0.26f, 0.59f, 0.98f, 0.67f);
    c[Col_ButtonActive]    = Vec4(0.06f, 0.53f, 0.98f, 1.00f);
    c[Col_CheckMark]       = Vec4(0.26f, 0.59f, 0.98f, 1.00f);
    c[Col_SliderGrab]      = Vec4(0.26f, 0.59f, 0.98f, 0.78f);
    c[Col_SliderGrabActive]= Vec4(0.26f, 0.59f, 0.98f, 1.00f);
    c[Col_Header]          = Vec4(0.26f, 0.59f, 0.98f, 0.31f);
    c[Col_HeaderHovered]   = Vec4(0.26f, 0.59f, 0.98f, 0.80f);
    c[Col_HeaderActive]    = Vec4(0.26f, 0.59f, 0.98f, 1.00f);
    c[Col_Separator]       = Vec4(0.61f, 0.61f, 0.61f, 1.00f);
    c[Col_TextSelectedBg]  = Vec4(0.26f, 0.59f, 0.98f, 0.35f);
    c[Col_Tab]             = Vec4(0.18f, 0.35f, 0.58f, 0.86f);
    c[Col_TabActive]       = Vec4(0.42f, 0.55f, 0.80f, 1.00f);
}

// ---------------------------------------------------------------------------
// IO event queues
// ---------------------------------------------------------------------------
void IO::AddMousePosEvent(float x, float y) { if (mouseQn < 64) mouseQ[mouseQn++] = { x, y }; }
void IO::AddMouseButtonEvent(int btn, bool down) { if (clickQn < 64) clickQ[clickQn++] = { btn, down }; }
void IO::AddInputCharactersUTF8(const char* s) {
    int n = (int)strlen(s);
    if (charQn + n < 512) { memcpy(charQ + charQn, s, n); charQn += n; }
}

// ---------------------------------------------------------------------------
// font atlas (stb_truetype bake of ProggyClean, ASCII 32..127, RGBA8)
// ---------------------------------------------------------------------------
FontAtlas::FontAtlas() {}
FontAtlas::~FontAtlas() { delete[] texPixelsRGBA; }

const Glyph* Font::FindGlyphNoFallback(unsigned int c) const {
    if (c < 32 || c > 127) return nullptr;
    return &glyphs[c - 32];
}

float Font::CalcTextSizeA(float size, const char* text) const {
    float w = 0;
    for (const char* p = text; *p; p++) {
        const Glyph* g = FindGlyphNoFallback((unsigned char)*p);
        if (g) w += g->XAdvance;
    }
    (void)size;
    return w;
}

void FontAtlas::AddFontDefault(const FontConfig* cfg) {
    float px = cfg ? cfg->SizePixels : 13.0f;
    stbtt_fontinfo f;
    stbtt_InitFont(&f, ledui_proggy, 0);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&f, &ascent, &descent, &lineGap);
    float sc = px / (float)(ascent - descent);
    font.FontSize = px;
    font.Ascent = ascent * sc;
    font.Descent = descent * sc;

    // row packer into a fixed 512x256 atlas (96 glyphs of one size fit easily)
    const int AW = 512, AH = 256;
    unsigned char* buf = new unsigned char[AW * AH]();
    int cx = 0, cy = 0, rowH = 0;
    font.glyphCount = 0;
    float extra = cfg ? cfg->GlyphExtraSpacing : 0.0f;

    for (int cp = 32; cp <= 127; cp++) {
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f, cp, &adv, &lsb);
        int bw = 0, bh = 0, ox = 0, oy = 0;
        unsigned char* bm = stbtt_GetCodepointBitmap(&f, sc, sc, cp, &bw, &bh, &ox, &oy);
        if (cx + bw + 1 >= AW) { cx = 0; cy += rowH + 1; rowH = 0; }
        if (bm) {
            for (int y = 0; y < bh; y++)
                memcpy(buf + (cy + y) * AW + cx, bm + y * bw, (size_t)bw);
            stbtt_FreeBitmap(bm, nullptr);
        }
        Glyph& g = font.glyphs[cp - 32];
        g.Codepoint = cp;
        g.U0 = (float)cx / AW;         g.V0 = (float)cy / AH;
        g.U1 = (float)(cx + bw) / AW;  g.V1 = (float)(cy + bh) / AH;
        // ledui text positions are the TOP of the text box; baseline sits at
        // font.Ascent below it. stbtt iyoff is measured from the baseline.
        g.X0 = (float)ox;
        g.Y0 = font.Ascent + (float)oy;
        g.X1 = g.X0 + (float)bw;
        g.Y1 = g.Y0 + (float)bh;
        g.XAdvance = adv * sc + extra;
        g.YAdvance = 0;
        g.XOffset = 0; g.YOffset = 0;
        cx += bw + 1;
        if (bh > rowH) rowH = bh;
        font.glyphCount++;
    }

    // expand to RGBA (white glyph, alpha = coverage) — same scheme as ImGui
    texW = AW; texH = AH;
    texPixelsRGBA = new unsigned char[AW * AH * 4];
    for (int i = 0; i < AW * AH; i++) {
        texPixelsRGBA[i * 4 + 0] = 255;
        texPixelsRGBA[i * 4 + 1] = 255;
        texPixelsRGBA[i * 4 + 2] = 255;
        texPixelsRGBA[i * 4 + 3] = buf[i];
    }
    delete[] buf;
}

void FontAtlas::GetTexDataAsRGBA32(unsigned char** pixels, int* w, int* h) {
    *pixels = texPixelsRGBA; *w = texW; *h = texH;
}

// ---------------------------------------------------------------------------
// draw list
// ---------------------------------------------------------------------------
void DrawList::Clear() { CmdBuffer.clear(); IdxBuffer.clear(); VtxBuffer.clear(); clipStack.n = 0; ClipRect = Vec4(0, 0, 0, 0); }

void DrawList::PushClipRect(const Vec4& r) {
    Vec4 out = r;
    if (clipStack.n > 0) {   // intersect with previous
        const Vec4& p = clipStack.rects[clipStack.n - 1];
        out.x = r.x > p.x ? r.x : p.x;
        out.y = r.y > p.y ? r.y : p.y;
        out.z = r.z < p.z ? r.z : p.z;
        out.w = r.w < p.w ? r.w : p.w;
    }
    if (out.z < out.x) out.z = out.x;
    if (out.w < out.y) out.w = out.y;
    clipStack.rects[clipStack.n++] = out;
    ClipRect = out;
}
void DrawList::PopClipRect() {
    if (clipStack.n > 0) clipStack.n--;
    ClipRect = clipStack.n > 0 ? clipStack.rects[clipStack.n - 1] : Vec4(0, 0, 0, 0);
}

void DrawList::primReserve(int idxCount, int vtxCount) {
    (void)idxCount; (void)vtxCount;   // Buf grows on push; sizes tracked after
}

void DrawList::pushTriangle(const Vec2& a, const Vec2& b, const Vec2& c, uint32_t col) {
    int base = VtxBuffer.Size;
    Vec2 uv0(0, 0);   // solid color: uv (0,0) samples a white pixel baked at atlas (0,0)
    DrawVert va = { a, uv0, col }, vb = { b, uv0, col }, vc = { c, uv0, col };
    VtxBuffer.push(va); VtxBuffer.push(vb); VtxBuffer.push(vc);
    DrawIdx i0 = (DrawIdx)base, i1 = (DrawIdx)(base + 1), i2 = (DrawIdx)(base + 2);
    IdxBuffer.push(i0); IdxBuffer.push(i1); IdxBuffer.push(i2);
}

void DrawList::AddQuadFilled(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, uint32_t col) {
    pushTriangle(a, b, c, col);
    pushTriangle(a, c, d, col);
}

void DrawList::AddRectFilled(const Vec2& pmin, const Vec2& pmax, uint32_t col, float rounding, int corners) {
    if (rounding <= 0.0f) {
        AddQuadFilled(pmin, Vec2(pmax.x, pmin.y), pmax, Vec2(pmin.x, pmax.y), col);
        return;
    }
    float r = rounding;
    float maxR = (pmax.x - pmin.x) * 0.5f < (pmax.y - pmin.y) * 0.5f ? (pmax.x - pmin.x) * 0.5f : (pmax.y - pmin.y) * 0.5f;
    if (r > maxR) r = maxR;
    // convex polygon: straight edges + quarter arcs (3 segments per active corner)
    Vec2 pts[64];
    int n = 0;
    auto arc = [&](int cornerFlag, Vec2 c, float a0) {
        if (!(corners & cornerFlag)) {
            return;
        }
        for (int i = 0; i <= 3; i++) {
            float a = a0 + (float)M_PI * 0.5f * (float)i / 3.0f;
            pts[n++] = Vec2(c.x + cosf(a) * r, c.y + sinf(a) * r);
        }
    };
    auto pt = [&](Vec2 p) { pts[n++] = p; };
    arc(Corner::TopLeft, Vec2(pmin.x + r, pmin.y + r), (float)M_PI);
    pt(Vec2(pmax.x - r, pmin.y));
    arc(Corner::TopRight, Vec2(pmax.x - r, pmin.y + r), -(float)M_PI * 0.5f);
    pt(Vec2(pmax.x, pmax.y - r));
    arc(Corner::BottomRight, Vec2(pmax.x - r, pmax.y - r), 0.0f);
    pt(Vec2(pmin.x + r, pmax.y));
    arc(Corner::BottomLeft, Vec2(pmin.x + r, pmax.y - r), (float)M_PI * 0.5f);
    pt(Vec2(pmin.x, pmin.y + r));
    // fan from centroid
    Vec2 ctr((pmin.x + pmax.x) * 0.5f, (pmin.y + pmax.y) * 0.5f);
    for (int i = 0; i < n; i++)
        pushTriangle(ctr, pts[i], pts[(i + 1) % n], col);
}

void DrawList::AddRect(const Vec2& pmin, const Vec2& pmax, uint32_t col, float rounding, int corners, float thickness) {
    (void)rounding; (void)corners;
    AddLine(pmin, Vec2(pmax.x, pmin.y), col, thickness);
    AddLine(Vec2(pmax.x, pmin.y), pmax, col, thickness);
    AddLine(pmax, Vec2(pmin.x, pmax.y), col, thickness);
    AddLine(Vec2(pmin.x, pmax.y), pmin, col, thickness);
}

void DrawList::AddLine(const Vec2& a, const Vec2& b, uint32_t col, float thickness) {
    float dx = b.x - a.x, dy = b.y - a.y;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    float nx = -dy / len * thickness * 0.5f, ny = dx / len * thickness * 0.5f;
    AddQuadFilled(Vec2(a.x + nx, a.y + ny), Vec2(b.x + nx, b.y + ny), Vec2(b.x - nx, b.y - ny), Vec2(a.x - nx, a.y - ny), col);
}

void DrawList::AddCircleFilled(const Vec2& c, float r, uint32_t col, int segs) {
    Vec2 prev(c.x + r, c.y);
    for (int i = 1; i <= segs; i++) {
        float a = (float)M_PI * 2.0f * (float)i / (float)segs;
        Vec2 p(c.x + cosf(a) * r, c.y + sinf(a) * r);
        pushTriangle(c, prev, p, col);
        prev = p;
    }
}

void DrawList::AddText(const Vec2& pos, uint32_t col, const char* text, const char* end) {
    if (!g_ctx) return;
    const Font& f = g_ctx->atlas.font;
    float x = pos.x, y = pos.y;
    for (const char* p = text; (end ? p < end : *p) && *p; p++) {
        const Glyph* g = f.FindGlyphNoFallback((unsigned char)*p);
        if (!g) continue;
        if (g->U1 > g->U0) {
            Vec2 a(x + g->X0, y + g->Y0), b(x + g->X1, y + g->Y0), c(x + g->X1, y + g->Y1), d(x + g->X0, y + g->Y1);
            int base = VtxBuffer.Size;
            DrawVert va = { a, Vec2(g->U0, g->V0), col }, vb = { b, Vec2(g->U1, g->V0), col },
                     vc = { c, Vec2(g->U1, g->V1), col }, vd = { d, Vec2(g->U0, g->V1), col };
            VtxBuffer.push(va); VtxBuffer.push(vb); VtxBuffer.push(vc); VtxBuffer.push(vd);
            DrawIdx i0 = (DrawIdx)base, i1 = (DrawIdx)(base + 1), i2 = (DrawIdx)(base + 2), i3 = (DrawIdx)(base + 3);
            IdxBuffer.push(i0); IdxBuffer.push(i1); IdxBuffer.push(i2);
            IdxBuffer.push(i0); IdxBuffer.push(i2); IdxBuffer.push(i3);
        }
        x += g->XAdvance;
    }
}

// ---------------------------------------------------------------------------
// frame flow
// ---------------------------------------------------------------------------
void NewFrame() {
    if (!g_ctx) return;
    Context& C = *g_ctx;
    IO& io = C.io;
    // drain queues
    for (int i = 0; i < io.mouseQn; i++) io.mousePos = Vec2(io.mouseQ[i].x, io.mouseQ[i].y);
    io.mouseQn = 0;
    io.mouseDownPrev[0] = io.mouseDown[0];
    io.mouseDownPrev[1] = io.mouseDown[1];
    for (int i = 0; i < io.clickQn; i++) {
        if (io.clickQ[i].btn >= 0 && io.clickQ[i].btn < 3) io.mouseDown[io.clickQ[i].btn] = io.clickQ[i].down;
    }
    io.clickQn = 0;
    io.inputCharCount = io.charQn;
    memcpy(io.inputChars, io.charQ, (size_t)io.charQn);
    io.charQn = 0;
    io.WantCaptureMouse = io.mousePos.x >= 0 && io.mousePos.y >= 0 &&
                          io.mousePos.x < io.DisplaySize.x && io.mousePos.y < io.DisplaySize.y;

    C.draw.Clear();
    if (io.DisplaySize.x > 0 && io.DisplaySize.y > 0)
        C.draw.PushClipRect(Vec4(0, 0, io.DisplaySize.x, io.DisplaySize.y));
    C.itemEdited = false;
    C.hoveredId = -1;
    C.lastItemW = 0;
    C.windowVisible = false;
}

void Render() {
    if (!g_ctx) return;
    Context& C = *g_ctx;
    // single draw list, single command (atlas texture); add one if we drew
    C.drawData.CmdListsCount = C.draw.VtxBuffer.Size > 0 ? 1 : 0;
    C.cmdListPtr = &C.draw;
    C.drawData.CmdLists = &C.cmdListPtr;
    C.drawData.DisplaySize = C.io.DisplaySize;
    C.drawData.Valid = C.drawData.CmdListsCount > 0;
}

DrawData* GetDrawData() { return g_ctx ? &g_ctx->drawData : nullptr; }

// solid quads sample uv (0,0) — the atlas keeps pixel (0,0) pure white for that
static void ensureSolidPixel() {
    if (g_ctx && g_ctx->atlas.texPixelsRGBA) {
        g_ctx->atlas.texPixelsRGBA[0] = 255;
        g_ctx->atlas.texPixelsRGBA[1] = 255;
        g_ctx->atlas.texPixelsRGBA[2] = 255;
        g_ctx->atlas.texPixelsRGBA[3] = 255;
    }
}

// ---------------------------------------------------------------------------
// windows
// ---------------------------------------------------------------------------
static Vec4 winClip(Context& C) {
    return Vec4(C.win.pos.x, C.win.pos.y, C.win.pos.x + C.win.size.x, C.win.pos.y + C.win.size.y);
}

bool Begin(const char* name, bool* open, int flags) {
    if (!g_ctx) return false;
    Context& C = *g_ctx;
    const IO& io = C.io;
    C.win.name = name;
    C.win.flags = flags;
    C.itemId = 0;
    if (C.hasNextPos) { C.win.pos = C.setNextPos; C.hasNextPos = false; }
    if (C.hasNextSize) { C.win.size = C.setNextSize; C.hasNextSize = false; }
    ensureSolidPixel();

    const Style& st = C.style;
    float fs = C.atlas.font.FontSize;
    C.lineHeight = fs + st.FramePadding.y * 2.0f;
    bool decorated = (flags & WinFlags::NoDecoration) == 0;

    // draw window chrome
    float rounding = st.WindowRounding;
    C.draw.AddRectFilled(C.win.pos, Vec2(C.win.pos.x + C.win.size.x, C.win.pos.y + C.win.size.y),
                         packCol(st.Colors[Col_WindowBg]), rounding);
    float titleH = 0;
    if (decorated) {
        titleH = C.lineHeight + 4;
        C.draw.AddRectFilled(C.win.pos, Vec2(C.win.pos.x + C.win.size.x, C.win.pos.y + titleH),
                             packCol(st.Colors[Col_TitleBgActive]), rounding, Corner::TopLeft | Corner::TopRight);
        C.draw.AddText(Vec2(C.win.pos.x + st.WindowPadding.x, C.win.pos.y + (titleH - fs) * 0.5f),
                       packCol(st.Colors[Col_Text]), name);
        // collapse/close box
        if (open) {
            Vec2 bx(C.win.pos.x + C.win.size.x - titleH - 4, C.win.pos.y + 2);
            C.draw.AddRectFilled(bx, Vec2(bx.x + titleH - 4, bx.y + titleH - 4), packCol(st.Colors[Col_Button]), 4.0f);
            bool hov = io.mousePos.x >= bx.x && io.mousePos.y >= bx.y &&
                       io.mousePos.x <= bx.x + titleH - 4 && io.mousePos.y <= bx.y + titleH - 4;
            bool clicked = hov && io.mouseDown[0] && !io.mouseDownPrev[0];
            if (clicked) *open = false;
        }
    }
    C.winCursor = Vec2(C.win.pos.x + st.WindowPadding.x, C.win.pos.y + titleH + st.WindowPadding.y);
    C.draw.PushClipRect(winClip(C));
    C.windowVisible = true;
    return true;
}

void End() {
    if (!g_ctx) return;
    Context& C = *g_ctx;
    C.draw.PopClipRect();
    C.windowVisible = false;
}

void SetNextWindowPos(const Vec2& pos) { if (g_ctx) { g_ctx->setNextPos = pos; g_ctx->hasNextPos = true; } }
void SetNextWindowSize(const Vec2& size) { if (g_ctx) { g_ctx->setNextSize = size; g_ctx->hasNextSize = true; } }

Vec2 GetContentRegionAvail() {
    Context& C = *g_ctx;
    return Vec2(C.win.pos.x + C.win.size.x - C.style.WindowPadding.x - C.winCursor.x,
                C.win.pos.y + C.win.size.y - C.style.WindowPadding.y - C.winCursor.y);
}

// ---------------------------------------------------------------------------
// widget plumbing
// ---------------------------------------------------------------------------
static bool hovered(Context& C, const Vec2& mn, const Vec2& mx) {
    const IO& io = C.io;
    return io.mousePos.x >= mn.x && io.mousePos.y >= mn.y && io.mousePos.x <= mx.x && io.mousePos.y <= mx.y;
}
static bool pressedNow(Context& C, int id, const Vec2& mn, const Vec2& mx) {
    const IO& io = C.io;
    bool hov = hovered(C, mn, mx);
    if (hov) C.hoveredId = id;
    if (hov && io.mouseDown[0] && !io.mouseDownPrev[0]) C.activeId = id;
    return hov && C.activeId == id && !io.mouseDown[0] && io.mouseDownPrev[0] && C.itemId == id;
}

static const Vec4& colFor(Context& C, Col_ base) {
    const Style& st = C.style;
    if (C.colorStackN > 0 && C.colorStackCol[C.colorStackN - 1] == base)
        return C.colorStack[C.colorStackN - 1];
    const IO& io = C.io;
    if (base == Col_Button && C.activeId == C.itemId && io.mouseDown[0]) return st.Colors[Col_ButtonActive];
    if (base == Col_Button && hovered(C, C.lastItemMin, C.lastItemMax)) return st.Colors[Col_ButtonHovered];
    return st.Colors[base];
}

static void endItem(Context& C, Vec2 mn, Vec2 mx, float advanceW = -1) {
    C.lastItemMin = mn; C.lastItemMax = mx;
    C.lastItemW = advanceW >= 0 ? advanceW : (mx.x - mn.x);
    C.itemId++;
}

static bool consumeTypedChars(Context& C, char* buf, size_t bufSize) {
    bool edited = false;
    IO& io = C.io;
    for (int i = 0; i < io.inputCharCount; i++) {
        char ch = io.inputChars[i];
        size_t len = strlen(buf);
        if (ch == '\b') {
            if (len > 0) { buf[len - 1] = 0; edited = true; }
        } else if ((unsigned char)ch >= 32 && len + 1 < bufSize) {
            buf[len] = ch; buf[len + 1] = 0; edited = true;
        }
    }
    io.inputCharCount = 0;
    return edited;
}

// ---------------------------------------------------------------------------
// widgets
// ---------------------------------------------------------------------------
bool Button(const char* label, const Vec2& size) {
    Context& C = *g_ctx;
    const Style& st = C.style;
    const Font& f = C.atlas.font;
    Vec2 ts(f.CalcTextSizeA(f.FontSize, label), f.FontSize);
    Vec2 sz(size.x > 0 ? size.x : ts.x + st.FramePadding.x * 2,
            size.y > 0 ? size.y : f.FontSize + st.FramePadding.y * 2);
    Vec2 mn(C.winCursor), mx(C.winCursor.x + sz.x, C.winCursor.y + sz.y);
    int id = C.itemId;
    C.lastItemMin = mn; C.lastItemMax = mx;   // for colFor hover on this item
    C.draw.AddRectFilled(mn, mx, packCol(colFor(C, Col_Button)), st.FrameRounding);
    // border
    C.draw.AddRect(mn, mx, packCol(st.Colors[Col_Border]), st.FrameRounding, Corner::All, 1.0f);
    C.draw.AddText(Vec2(mn.x + (sz.x - ts.x) * 0.5f, mn.y + (sz.y - ts.y) * 0.5f),
                   packCol(st.Colors[Col_Text]), label);
    C.winCursor.x += sz.x;
    endItem(C, mn, mx, sz.x);
    return pressedNow(C, id, mn, mx);
}

bool SmallButton(const char* label) {
    Context& C = *g_ctx;
    Vec2 save = C.style.FramePadding;
    C.style.FramePadding = Vec2(3, 2);
    bool r = Button(label);
    C.style.FramePadding = save;
    return r;
}

void Text(const char* fmt, ...) {
    Context& C = *g_ctx;
    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    const Font& f = C.atlas.font;
    C.draw.AddText(C.winCursor, packCol(colFor(C, Col_Text)), buf);
    Vec2 mn(C.winCursor), mx(C.winCursor.x + f.CalcTextSizeA(f.FontSize, buf), C.winCursor.y + C.lineHeight);
    C.winCursor.y += C.lineHeight;
    endItem(C, mn, mx);
}

void TextWrapped(const char* fmt, ...) {
    Context& C = *g_ctx;
    char buf[1024];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    const Font& f = C.atlas.font;
    Vec2 avail = GetContentRegionAvail();
    float maxW = avail.x > 0 ? avail.x : C.win.size.x - 20;
    // wrap into lines
    float y = C.winCursor.y;
    float x = C.winCursor.x;
    const char* p = buf;
    const char* lineStart = buf;
    Vec2 mn = C.winCursor, mx = C.winCursor;
    while (*p) {
        const Glyph* g = f.FindGlyphNoFallback((unsigned char)*p);
        float adv = g ? g->XAdvance : 0;
        if (*p == '\n' || (x + adv > C.winCursor.x + maxW && lineStart != p)) {
            // flush line [lineStart, p)
            C.draw.AddText(Vec2(C.winCursor.x, y), packCol(colFor(C, Col_Text)), lineStart, p);
            y += C.lineHeight;
            x = C.winCursor.x;
            lineStart = p + (p[0] == '\n' ? 1 : 0);
            p = lineStart;
            continue;
        }
        x += adv;
        p++;
    }
    C.draw.AddText(Vec2(C.winCursor.x, y), packCol(colFor(C, Col_Text)), lineStart, p);
    y += C.lineHeight;
    mx = Vec2(C.winCursor.x + maxW, y);
    C.winCursor.y = y;
    endItem(C, mn, mx, maxW);
}

void TextColored(const Vec4& col, const char* fmt, ...) {
    Context& C = *g_ctx;
    PushStyleColor(Col_Text, col);
    va_list ap; va_start(ap, fmt);
    // reuse Text with forwarded args via vsnprintf
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    const Font& f = C.atlas.font;
    C.draw.AddText(C.winCursor, packCol(C.style.Colors[Col_Text]), buf);
    Vec2 mn(C.winCursor), mx(C.winCursor.x + f.CalcTextSizeA(f.FontSize, buf), C.winCursor.y + C.lineHeight);
    C.winCursor.y += C.lineHeight;
    endItem(C, mn, mx);
    PopStyleColor();
}

void BulletText(const char* fmt, ...) {
    Context& C = *g_ctx;
    PushStyleColor(Col_Text, colFor(C, Col_Text));
    va_list ap; va_start(ap, fmt);
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    const Font& f = C.atlas.font;
    C.draw.AddCircleFilled(Vec2(C.winCursor.x + 5, C.winCursor.y + C.lineHeight * 0.5f), 4.0f, packCol(C.style.Colors[Col_Text]), 10);
    C.draw.AddText(Vec2(C.winCursor.x + 16, C.winCursor.y), packCol(C.style.Colors[Col_Text]), buf);
    Vec2 mn(C.winCursor), mx(C.winCursor.x + 16 + f.CalcTextSizeA(f.FontSize, buf), C.winCursor.y + C.lineHeight);
    C.winCursor.y += C.lineHeight;
    endItem(C, mn, mx);
    PopStyleColor();
}

void SameLine(float offsetFromStartX, float spacing) {
    Context& C = *g_ctx;
    (void)spacing;
    C.winCursor.y -= C.lineHeight;   // undo NewLine of previous item
    if (offsetFromStartX > 0)
        C.winCursor.x = C.win.pos.x + C.style.WindowPadding.x + offsetFromStartX;
    else
        C.winCursor.x += C.style.ItemSpacing.x;
}

void NewLine() {
    Context& C = *g_ctx;
    C.winCursor.x = C.win.pos.x + C.style.WindowPadding.x;
    C.winCursor.y += C.lineHeight;
}

void Separator() {
    Context& C = *g_ctx;
    const Style& st = C.style;
    float w = C.win.size.x - st.WindowPadding.x * 2;
    float sy = C.winCursor.y + C.lineHeight * 0.5f;
    C.draw.AddRectFilled(Vec2(C.winCursor.x, sy), Vec2(C.winCursor.x + w, sy + 2), packCol(st.Colors[Col_Separator]));
    Vec2 mn(C.winCursor), mx(C.winCursor.x + w, C.winCursor.y + C.lineHeight);
    C.winCursor.y += C.lineHeight + st.ItemSpacing.y;
    endItem(C, mn, mx, w);
}

bool Checkbox(const char* label, bool* v) {
    Context& C = *g_ctx;
    const Style& st = C.style;
    const Font& f = C.atlas.font;
    int id = C.itemId;
    float side = C.lineHeight;
    Vec2 mn(C.winCursor), mx(C.winCursor.x + side, C.winCursor.y + side);
    bool hov = hovered(C, mn, Vec2(mx.x + st.ItemInnerSpacing.x + f.CalcTextSizeA(f.FontSize, label), mx.y));
    if (hov) C.hoveredId = id;
    if (hov && C.io.mouseDown[0] && !C.io.mouseDownPrev[0]) C.activeId = id;
    bool toggled = hov && C.activeId == id && !C.io.mouseDown[0] && C.io.mouseDownPrev[0];
    bool changed = false;
    if (toggled) { *v = !*v; changed = true; C.itemEdited = true; }
    const Vec4& fc = C.io.mouseDown[0] && hov ? st.Colors[Col_FrameBgActive]
                    : hov ? st.Colors[Col_FrameBgHovered] : st.Colors[Col_FrameBg];
    C.draw.AddRectFilled(mn, mx, packCol(fc), st.FrameRounding);
    if (*v) {   // check mark: two thick lines
        Vec2 a(mn.x + side * 0.22f, mn.y + side * 0.55f), b(mn.x + side * 0.45f, mn.y + side * 0.78f),
             c(mn.x + side * 0.80f, mn.y + side * 0.25f);
        C.draw.AddLine(a, b, packCol(st.Colors[Col_CheckMark]), 3.0f);
        C.draw.AddLine(b, c, packCol(st.Colors[Col_CheckMark]), 3.0f);
    }
    C.draw.AddRect(mn, mx, packCol(st.Colors[Col_Border]), st.FrameRounding, Corner::All, 1.0f);
    C.draw.AddText(Vec2(mx.x + st.ItemInnerSpacing.x, mn.y + (side - f.FontSize) * 0.5f),
                   packCol(st.Colors[Col_Text]), label);
    Vec2 lmx(mx.x + st.ItemInnerSpacing.x + f.CalcTextSizeA(f.FontSize, label), mx.y);
    C.winCursor.x += side + st.ItemInnerSpacing.x + f.CalcTextSizeA(f.FontSize, label) + st.ItemSpacing.x;
    endItem(C, mn, lmx);
    return changed;
}

bool RadioButton(const char* label, bool active) {
    Context& C = *g_ctx;
    const Style& st = C.style;
    const Font& f = C.atlas.font;
    int id = C.itemId;
    float r = C.lineHeight * 0.4f;
    Vec2 center(C.winCursor.x + r + 2, C.winCursor.y + C.lineHeight * 0.5f);
    Vec2 mn(C.winCursor.x, center.y - C.lineHeight * 0.5f);
    float labelW = f.CalcTextSizeA(f.FontSize, label);
    Vec2 mx(center.x + r + st.ItemInnerSpacing.x + labelW, center.y + C.lineHeight * 0.5f);
    bool hov = hovered(C, mn, mx);
    if (hov) C.hoveredId = id;
    if (hov && C.io.mouseDown[0] && !C.io.mouseDownPrev[0]) C.activeId = id;
    bool clicked = hov && C.activeId == id && !C.io.mouseDown[0] && C.io.mouseDownPrev[0];
    C.draw.AddCircleFilled(center, r, packCol(active ? st.Colors[Col_ButtonActive] : st.Colors[Col_FrameBg]), 16);
    C.draw.AddCircleFilled(center, r * 0.55f, packCol(st.Colors[Col_CheckMark]), 12);
    if (hov) C.draw.AddCircleFilled(center, r + 2, packCol(st.Colors[Col_Border]), 16);
    C.draw.AddText(Vec2(center.x + r + st.ItemInnerSpacing.x, mn.y + (C.lineHeight - f.FontSize) * 0.5f),
                   packCol(st.Colors[Col_Text]), label);
    C.winCursor.x += r * 2 + 4 + st.ItemInnerSpacing.x + labelW + st.ItemSpacing.x;
    endItem(C, mn, mx);
    return clicked;
}

bool SliderFloat(const char* label, float* v, float min, float max, const char* fmt) {
    Context& C = *g_ctx;
    const Style& st = C.style;
    const Font& f = C.atlas.font;
    int id = C.itemId;
    Vec2 avail = GetContentRegionAvail();
    float w = avail.x > 40 ? avail.x : 200.0f;
    float h = C.lineHeight;
    Vec2 mn(C.winCursor), mx(C.winCursor.x + w, C.winCursor.y + h);
    // label drawn above the frame (ImGui draws it right-aligned inside; ours: text above)
    C.draw.AddText(Vec2(mn.x, mn.y - f.FontSize - 2), packCol(st.Colors[Col_Text]), label);
    Vec2 frame(mn.x, mn.y - 4);
    bool hov = hovered(C, frame, mx);
    if (hov) C.hoveredId = id;
    if (hov && C.io.mouseDown[0] && !C.io.mouseDownPrev[0]) { C.activeId = id; C.activeGrabT = -1; }
    bool changed = false;
    if (C.activeId == id && C.io.mouseDown[0]) {
        float t = (C.io.mousePos.x - frame.x) / (mx.x - frame.x);
        if (t < 0) t = 0; if (t > 1) t = 1;
        float nv = min + (max - min) * t;
        if (nv != *v) { *v = nv; changed = true; C.itemEdited = true; }
    } else if (C.activeId == id && !C.io.mouseDown[0]) {
        C.activeId = -1;
    }
    // frame
    const Vec4& fc = (hov && C.activeId == id) ? st.Colors[Col_FrameBgActive]
                    : hov ? st.Colors[Col_FrameBgHovered] : st.Colors[Col_FrameBg];
    C.draw.AddRectFilled(frame, mx, packCol(fc), st.FrameRounding);
    float t = (max > min) ? (*v - min) / (max - min) : 0;
    if (t < 0) t = 0; if (t > 1) t = 1;
    float grabW = 10.0f;
    float gx = frame.x + (mx.x - frame.x - grabW) * t;
    const Vec4& gc = (C.activeId == id && C.io.mouseDown[0]) ? st.Colors[Col_SliderGrabActive] : st.Colors[Col_SliderGrab];
    C.draw.AddRectFilled(Vec2(gx, frame.y + 2), Vec2(gx + grabW, mx.y - 2), packCol(gc), st.GrabRounding);
    // value text on grab
    char vbuf[64];
    snprintf(vbuf, sizeof(vbuf), fmt ? fmt : "%.3f", *v);
    float vw = f.CalcTextSizeA(f.FontSize, vbuf);
    C.draw.AddText(Vec2(frame.x + (mx.x - frame.x - vw) * 0.5f, frame.y + (h - f.FontSize) * 0.5f - 1),
                   packCol(st.Colors[Col_Text]), vbuf);
    C.winCursor.y += h + 4 + st.ItemSpacing.y;
    endItem(C, frame, mx, w);
    return changed;
}

bool InputText(const char* label, char* buf, size_t bufSize, int flags) {
    Context& C = *g_ctx;
    (void)flags;
    const Style& st = C.style;
    const Font& f = C.atlas.font;
    int id = C.itemId;
    Vec2 avail = GetContentRegionAvail();
    float w = avail.x > 40 ? avail.x : 200.0f;
    float h = C.lineHeight + 6;
    Vec2 mn(C.winCursor), mx(C.winCursor.x + w, C.winCursor.y + h);
    if (label[0] != '#')   // hidden labels ("##pn") skip the caption
        C.draw.AddText(Vec2(mn.x, mn.y - f.FontSize - 2), packCol(st.Colors[Col_Text]), label);
    Vec2 frame(mn.x, mn.y);
    bool hov = hovered(C, frame, mx);
    if (hov) C.hoveredId = id;
    bool edited = consumeTypedChars(C, buf, bufSize);
    if (edited) C.itemEdited = true;
    const Vec4& fc = hov ? st.Colors[Col_FrameBgHovered] : st.Colors[Col_FrameBg];
    C.draw.AddRectFilled(frame, mx, packCol(fc), st.FrameRounding);
    C.draw.AddRect(frame, mx, packCol(st.Colors[Col_Border]), st.FrameRounding, Corner::All, 1.0f);
    C.draw.AddText(Vec2(frame.x + st.FramePadding.x, frame.y + (h - f.FontSize) * 0.5f),
                   packCol(st.Colors[Col_Text]), buf);
    // blinking cursor (0.6s period)
    if ((GetTime() - (int)(GetTime() / 0.6) * 0.6) < 0.3)
        C.draw.AddRectFilled(Vec2(frame.x + st.FramePadding.x + f.CalcTextSizeA(f.FontSize, buf) + 1, frame.y + 4),
                             Vec2(frame.x + st.FramePadding.x + f.CalcTextSizeA(f.FontSize, buf) + 3, mx.y - 4),
                             packCol(st.Colors[Col_Text]));
    C.winCursor.y += h + st.ItemSpacing.y;
    endItem(C, frame, mx, w);
    return edited;
}

bool IsItemEdited() { return g_ctx && g_ctx->itemEdited; }

void PushStyleColor(Col_ idx, const Vec4& col) {
    if (!g_ctx) return;
    Context& C = *g_ctx;
    if (C.colorStackN < 8) {
        C.colorStackCol[C.colorStackN] = idx;
        C.colorStack[C.colorStackN++] = col;
    }
}
void PopStyleColor(int count) {
    if (!g_ctx) return;
    Context& C = *g_ctx;
    C.colorStackN -= count;
    if (C.colorStackN < 0) C.colorStackN = 0;
}

} // namespace ledui
