// LEDGE UI (ledui) — compact immediate-mode UI library, styled after Dear
// ImGui's classic dark look (dark panels, blue accents, rounded frames,
// ProggyClean font). The game previously vendored Dear ImGui; this file
// replaces it with a hand-rolled implementation covering exactly the widget
// set the game uses, keeping API shape and vertex layout close so ported
// call sites and the GL upload code read the same.
//
//   ledger of differences vs Dear ImGui:
//   - no docking/tables/multi-font, single atlas, ASCII font range
//   - DrawIdx is uint16_t, DrawVert is {pos,uv,rgba} (20 bytes, as ImGui's)
//   - InputText renders + accepts chars fed via IO::AddInputCharactersUTF8
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdarg>

namespace ledui {

typedef void* TextureID;

struct Vec2 {
    float x, y;
    Vec2(float X = 0.0f, float Y = 0.0f) : x(X), y(Y) {}
};
struct Vec4 {
    float x, y, z, w;
    Vec4(float X = 0.0f, float Y = 0.0f, float Z = 0.0f, float W = 1.0f) : x(X), y(Y), z(Z), w(W) {}
};

enum Col_ {
    Col_Text = 0, Col_TextDisabled, Col_WindowBg, Col_ChildBg, Col_PopupBg, Col_Border,
    Col_FrameBg, Col_FrameBgHovered, Col_FrameBgActive,
    Col_TitleBg, Col_TitleBgActive,
    Col_Button, Col_ButtonHovered, Col_ButtonActive,
    Col_CheckMark, Col_SliderGrab, Col_SliderGrabActive,
    Col_Header, Col_HeaderHovered, Col_HeaderActive,
    Col_Separator, Col_TextSelectedBg, Col_Tab, Col_TabActive,
    Col_COUNT
};

namespace WinFlags {
enum : int { None = 0, NoDecoration = 1 << 0, NoMove = 1 << 1, NoSavedSettings = 1 << 2 };
}
namespace Corner {
enum : int { None = 0, TopLeft = 1, TopRight = 2, BottomLeft = 4, BottomRight = 8, All = 15 };
}

// ---- font / atlas (glyph layout mirrors Dear ImGui's ledui::Glyph) ----------
struct Glyph {
    float U0, V0, U1, V1;       // atlas uv
    float X0, Y0, X1, Y1;       // glyph quad offsets from cursor, baseline at Y1
    float XAdvance;             // 1 unit = font size
    float YAdvance;
    float XOffset;
    float YOffset;
    unsigned int Codepoint;
};

struct FontConfig {
    float SizePixels = 13.0f;   // raster size (ProggyClean scales softly past 13)
    float GlyphExtraSpacing = 0.0f;
};

struct Font {
    float FontSize = 13.0f;
    float Ascent = 0, Descent = 0;
    // lookup built after bake; linear scan over a compact codepoint range
    Glyph glyphs[96];           // ASCII 32..127
    int glyphCount = 0;
    const Glyph* FindGlyphNoFallback(unsigned int c) const;
    float CalcTextSizeA(float size, const char* text) const;
};

struct FontAtlas {
    TextureID TexID = nullptr;
    Font font;
    Font* Fonts[1] = { &font }; // io.Fonts->Fonts[0] keeps working
    void AddFontDefault(const FontConfig* cfg = nullptr);
    void GetTexDataAsRGBA32(unsigned char** pixels, int* w, int* h);
    FontAtlas();
    ~FontAtlas();
    // internal
    unsigned char* texPixelsRGBA = nullptr;
    int texW = 0, texH = 0;
};

// ---- IO --------------------------------------------------------------------
struct IO {
    Vec2 DisplaySize;           // pixels
    float DeltaTime = 1.0f / 60.0f;
    FontAtlas* Fonts = nullptr;
    float FontGlobalScale = 1.0f;
    bool WantCaptureMouse = false;

    // event queues (drained in NewFrame)
    void AddMousePosEvent(float x, float y);
    void AddMouseButtonEvent(int btn, bool down);   // btn 0=left,1=right
    void AddInputCharactersUTF8(const char* s);

    // internal state
    struct MouseEvt { float x, y; };
    struct ClickEvt { int btn; bool down; };
    MouseEvt mouseQ[64]; int mouseQn = 0;
    ClickEvt clickQ[64]; int clickQn = 0;
    char charQ[512]; int charQn = 0;

    Vec2 mousePos = Vec2(-100, -100);
    bool mouseDown[3] = { false, false, false };
    bool mouseDownPrev[3] = { false, false, false };
    char inputChars[512]; int inputCharCount = 0;
};

// ---- style -----------------------------------------------------------------
struct Style {
    float WindowRounding = 0.0f;
    float FrameRounding = 0.0f;
    float GrabRounding = 0.0f;
    float TabRounding = 0.0f;
    float ScrollbarRounding = 0.0f;
    Vec2 WindowPadding = Vec2(10, 10);
    Vec2 FramePadding = Vec2(6, 4);
    Vec2 ItemSpacing = Vec2(8, 6);
    Vec2 ItemInnerSpacing = Vec2(4, 4);
    Vec4 Colors[Col_COUNT];
};

// ---- draw list (buffers expose Size/Data like Dear ImGui's) ----------------
template <class T>
struct Buf {
    T* Data = nullptr;
    int Size = 0, Cap = 0;
    void push(const T& v) {
        if (Size == Cap) { Cap = Cap ? Cap * 2 : 64; T* nd = new T[Cap];
            for (int i = 0; i < Size; i++) nd[i] = Data[i]; delete[] Data; Data = nd; }
        Data[Size++] = v;
    }
    void clear() { Size = 0; }
    T& operator[](int i) { return Data[i]; }
    const T& operator[](int i) const { return Data[i]; }
    ~Buf() { delete[] Data; }
};

typedef uint16_t DrawIdx;
struct DrawVert { Vec2 pos, uv; uint32_t col; };   // 20 bytes, GL_UNSIGNED_BYTE normalized col

struct DrawCmd {
    Vec4 ClipRect;
    uint32_t ElemCount = 0;
    uintptr_t IdxOffset = 0;
    TextureID TexID = nullptr;
    void* UserCallback = nullptr;   // never set; backend skips if non-null
    TextureID GetTexID() const { return TexID; }
};

struct DrawList {
    Buf<DrawCmd> CmdBuffer;
    Buf<DrawIdx> IdxBuffer;
    Buf<DrawVert> VtxBuffer;
    Vec4 ClipRect;                  // current clip

    void Clear();
    void PushClipRect(const Vec4& r);
    void PopClipRect();
    void AddRectFilled(const Vec2& pmin, const Vec2& pmax, uint32_t col, float rounding = 0.0f, int corners = Corner::All);
    void AddRect(const Vec2& pmin, const Vec2& pmax, uint32_t col, float rounding = 0.0f, int corners = Corner::All, float thickness = 1.0f);
    void AddLine(const Vec2& a, const Vec2& b, uint32_t col, float thickness = 1.0f);
    void AddCircleFilled(const Vec2& c, float r, uint32_t col, int segs = 12);
    void AddText(const Vec2& pos, uint32_t col, const char* text, const char* end = nullptr);
    void AddQuadFilled(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, uint32_t col);
    // internal
    void primReserve(int idxCount, int vtxCount);
    uint32_t packCol(const Vec4& c) const;
    void pushQuad(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, uint32_t colABCD);
    void pushTriangle(const Vec2& a, const Vec2& b, const Vec2& c, uint32_t col);
    struct ClipStack { Vec4 rects[16]; int n = 0; } clipStack;
};

struct DrawData {
    int CmdListsCount = 0;
    DrawList** CmdLists = nullptr;
    Vec2 DisplaySize;
    bool Valid = false;
};

// ---- context ---------------------------------------------------------------
struct Context {
    IO io;
    Style style;
    FontAtlas atlas;
    DrawList draw;                 // rebuilt each frame
    DrawList* cmdListPtr = nullptr;
    DrawData drawData;
    unsigned long long startTimeUs = 0;

    // window state
    struct Window { const char* name; Vec2 pos, size; int flags; bool open; };
    Window win = { nullptr, Vec2(60, 60), Vec2(400, 300), 0, true };
    Vec2 winCursor;                // content cursor (screen coords)
    float lineHeight = 26.0f;      // font size + frame padding
    float lastItemW = 0;
    Vec2 lastItemMin, lastItemMax; // for IsItemEdited/hover
    bool windowVisible = false;

    // interaction state
    Vec2 setNextPos; bool hasNextPos = false;
    Vec2 setNextSize; bool hasNextSize = false;
    int itemId = 0;                // sequential per window
    int activeId = -1;             // pressed-and-held widget
    int hoveredId = -1;
    float activeGrabT = -1;        // slider drag state
    bool itemEdited = false;       // last item changed value

    Vec4 colorStack[8]; int colorStackN = 0;
    Col_ colorStackCol[8];

    Context();
};

// ---- API -------------------------------------------------------------------
bool CreateContext();
void DestroyContext();
Context* GetCurrentContext();
void NewFrame();
void Render();
DrawData* GetDrawData();
double GetTime();

IO& GetIO();
Style& GetStyle();

void StyleColorsDark();

void SetNextWindowPos(const Vec2& pos);
void SetNextWindowSize(const Vec2& size);
bool Begin(const char* name, bool* open = nullptr, int flags = WinFlags::None);
void End();

void Text(const char* fmt, ...);
void TextWrapped(const char* fmt, ...);
void TextColored(const Vec4& col, const char* fmt, ...);
void BulletText(const char* fmt, ...);
void SameLine(float offsetFromStartX = 0.0f, float spacing = -1.0f);
void NewLine();
void Separator();
Vec2 GetContentRegionAvail();

bool Button(const char* label, const Vec2& size = Vec2(0, 0));
bool SmallButton(const char* label);
bool Checkbox(const char* label, bool* v);
bool RadioButton(const char* label, bool active);
bool SliderFloat(const char* label, float* v, float min, float max, const char* fmt = "%.3f");
bool InputText(const char* label, char* buf, size_t bufSize, int flags = 0);

bool IsItemEdited();
void PushStyleColor(Col_ idx, const Vec4& col);
void PopStyleColor(int count = 1);

} // namespace ledui
