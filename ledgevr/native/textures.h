// LEDGE 1.1 — material textures: asset PNG loading + GL uploads.
// Minimal PNG decoder (8-bit RGB/RGBA, non-interlaced, zlib inflate) so the
// material pack needs no external image lib. Shared by VR + mobile.
#pragma once
#include <GLES3/gl32.h>
#include <string>
#include <vector>
#include <android/asset_manager.h>

namespace lv {

// material ids — order matches the tex/ asset files and palette colors
enum TexId : int {
    TEX_WHITE = 0,     // fallback (generated)
    TEX_WOOD,          // wood_pastel
    TEX_GRASS,         // grass_mint
    TEX_STONE,         // stone_sky
    TEX_CANDY,         // candy_pink
    TEX_METAL,         // metal_silver
    TEX_SLATE,         // slate_dark
    TEX_SAND,          // sand_gold
    TEX_LEAF,          // leaf_green
    TEX_CLOUD,         // cloud_soft
    TEX_NEON,          // neon_grid
    TEX_RUBBER,        // rubber_blue
    TEX_MARBLE,        // marble_white
    TEX_COUNT
};

class TexLib {
public:
    // am = AAssetManager* on Android; null = use bundleDir_ (iOS plain files)
    bool init(AAssetManager* am);
    // iOS: load from a plain directory instead of AAssetManager
    void setBundleDir(const std::string& dir) { bundleDir_ = dir; }
    bool loadAll();                 // decode + upload all material PNGs
    void shutdown();
    GLuint tex(int id) const { return id >= 0 && id < TEX_COUNT ? ids_[id] : 0; }
    // material for a palette color index (kBlockColors idx -> TEX_*)
    static int materialForPalette(int colorIdx);
private:
    GLuint ids_[TEX_COUNT] = {};
    bool loaded_[TEX_COUNT] = {};
    AAssetManager* am_ = nullptr;
    std::string bundleDir_;
};

// decode a PNG file (8-bit RGB/RGBA) into RGBA8 pixel buffer; returns false on error
bool pngDecodeRGBA(const uint8_t* data, size_t size, std::vector<uint8_t>& out,
                   int& w, int& h);

// upload RGBA pixels as a repeating mip-mapped texture; returns GL id (0 on fail)
GLuint texUploadRGBA(const uint8_t* px, int w, int h, bool repeat, bool mipmaps);

} // namespace lv
