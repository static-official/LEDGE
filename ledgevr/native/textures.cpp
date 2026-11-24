// LEDGE 1.1 — textures implementation: minimal PNG decoder + GL upload.
#include "textures.h"
#include "log.h"
#include <zlib.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace lv {

// ---------------------------------------------------------------------------
// minimal PNG decoder: 8-bit RGB/RGBA/grayscale, non-interlaced, deflate
// ---------------------------------------------------------------------------
struct PngCtx {
    const uint8_t* d;
    size_t n, pos = 0;
    bool read(uint8_t* dst, size_t k) {
        if (pos + k > n) return false;
        memcpy(dst, d + pos, k);
        pos += k;
        return true;
    }
    uint32_t u32() {
        uint8_t b[4];
        if (!read(b, 4)) return 0;
        return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | b[3];
    }
};

static uint32_t pngChunk(const uint8_t* p) {  // chunk length big-endian
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}

bool pngDecodeRGBA(const uint8_t* data, size_t size, std::vector<uint8_t>& out,
                   int& w, int& h) {
    static const uint8_t sig[8] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
    if (size < 33 || memcmp(data, sig, 8) != 0) return false;
    PngCtx c{data, size, 8};
    uint32_t ihdrLen = pngChunk(data + 8);
    if (ihdrLen != 13) return false;
    c.pos = 16;  // skip length+type
    uint32_t iw = c.u32(), ih = c.u32();
    uint8_t bd = data[c.pos], ct = data[c.pos + 1], interlace = data[c.pos + 4];
    c.pos += 5 + 4;  // depth,color,comp,filter,interlace + CRC
    (void)ihdrLen;
    if (bd != 8 || interlace != 0) return false;
    int ch = ct == 6 ? 4 : ct == 2 ? 3 : ct == 0 ? 1 : 0;  // RGBA/RGB/gray
    if (!ch) return false;
    w = (int)iw;
    h = (int)ih;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;

    // gather IDAT stream
    std::vector<uint8_t> idat;
    bool seenEnd = false;
    while (c.pos + 8 <= size && !seenEnd) {
        uint32_t len = pngChunk(data + c.pos);
        const uint8_t* type = data + c.pos + 4;
        c.pos += 8;
        if (c.pos + len + 4 > size) return false;
        if (!memcmp(type, "IDAT", 4))
            idat.insert(idat.end(), data + c.pos, data + c.pos + len);
        else if (!memcmp(type, "IEND", 4))
            seenEnd = true;
        c.pos += len + 4;
    }
    if (idat.empty()) return false;

    // inflate
    uLongf rawLen = (uLongf)h * (1 + (uLongf)w * ch);
    std::vector<uint8_t> raw(rawLen);
    if (uncompress(raw.data(), &rawLen, idat.data(), (uLong)idat.size()) != Z_OK) return false;

    // unfilter — filters reference the previous RAW line, not the RGBA output
    out.resize((size_t)w * h * 4);
    int stride = w * ch;
    std::vector<uint8_t> line(stride), prevLine(stride);
    bool havePrev = false;
    for (int y = 0; y < h; y++) {
        const uint8_t* src = raw.data() + y * (1 + stride);
        uint8_t ft = *src++;
        memcpy(line.data(), src, stride);
        for (int i = 0; i < stride; i++) {
            int a = i >= ch ? line[i - ch] : 0;
            int b = havePrev ? prevLine[i] : 0;
            int cc = (havePrev && i >= ch) ? prevLine[i - ch] : 0;
            switch (ft) {
            case 1: line[i] = (uint8_t)(line[i] + a); break;
            case 2: line[i] = (uint8_t)(line[i] + b); break;
            case 3: line[i] = (uint8_t)(line[i] + (a + b) / 2); break;
            case 4: line[i] = (uint8_t)(line[i] + paeth(a, b, cc)); break;
            }
        }
        // expand to RGBA
        uint8_t* dst = out.data() + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            switch (ch) {
            case 4:
                dst[x * 4] = line[x * 4];
                dst[x * 4 + 1] = line[x * 4 + 1];
                dst[x * 4 + 2] = line[x * 4 + 2];
                dst[x * 4 + 3] = line[x * 4 + 3];
                break;
            case 3:
                dst[x * 4] = line[x * 3];
                dst[x * 4 + 1] = line[x * 3 + 1];
                dst[x * 4 + 2] = line[x * 3 + 2];
                dst[x * 4 + 3] = 255;
                break;
            case 1: {
                uint8_t g = line[x];
                dst[x * 4] = dst[x * 4 + 1] = dst[x * 4 + 2] = g;
                dst[x * 4 + 3] = 255;
                break;
            }
            }
        }
        memcpy(prevLine.data(), line.data(), stride);
        havePrev = true;
    }
    return true;
}

// ---------------------------------------------------------------------------
GLuint texUploadRGBA(const uint8_t* px, int w, int h, bool repeat, bool mipmaps) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    if (mipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    return id;
}

// ---------------------------------------------------------------------------
static const char* kTexFiles[TEX_COUNT] = {
    "", "wood_pastel", "grass_mint", "stone_sky", "candy_pink", "metal_silver",
    "slate_dark", "sand_gold", "leaf_green", "cloud_soft", "neon_grid",
    "rubber_blue", "marble_white",
};

// palette colors (kBlockColors) -> material per index
int TexLib::materialForPalette(int colorIdx) {
    static const int map[8] = {TEX_WOOD, TEX_GRASS, TEX_STONE, TEX_CANDY,
                               TEX_MARBLE, TEX_SLATE, TEX_SAND, TEX_LEAF};
    return map[colorIdx % 8];
}

bool TexLib::init(AAssetManager* am) {
    am_ = am;
    // white fallback 1x1 (id 0 means "none" in drawMeshTex, so store at slot 0)
    uint8_t white[4] = {255, 255, 255, 255};
    ids_[TEX_WHITE] = texUploadRGBA(white, 1, 1, false, false);
    loaded_[TEX_WHITE] = true;
    return true;
}

bool TexLib::loadAll() {
    if (!am_ && bundleDir_.empty()) {
        LOGW("TexLib: no asset manager / bundle dir — materials stay white");
        return false;
    }
    int ok = 0;
    for (int t = 1; t < TEX_COUNT; t++) {
        char path[96];
        snprintf(path, sizeof(path), "tex/%s.png", kTexFiles[t]);
        std::vector<uint8_t> px;
        int w = 0, h = 0;
        bool got = false;
        if (am_) {
            AAsset* a = AAssetManager_open(am_, path, AASSET_MODE_BUFFER);
            if (!a) {
                LOGE("TexLib: asset missing %s", path);
                continue;
            }
            size_t n = (size_t)AAsset_getLength(a);
            const uint8_t* buf = (const uint8_t*)AAsset_getBuffer(a);
            got = pngDecodeRGBA(buf, n, px, w, h);
            AAsset_close(a);
        } else {
            // iOS/desktop: plain file from the bundle directory
            std::string fp = bundleDir_ + "/" + path;
            FILE* f = fopen(fp.c_str(), "rb");
            if (!f) {
                LOGE("TexLib: bundle file missing %s", fp.c_str());
                continue;
            }
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            std::vector<uint8_t> buf((size_t)n);
            if (fread(buf.data(), 1, (size_t)n, f) == (size_t)n)
                got = pngDecodeRGBA(buf.data(), buf.size(), px, w, h);
            fclose(f);
        }
        if (got) {
            ids_[t] = texUploadRGBA(px.data(), w, h, true, true);
            loaded_[t] = true;
            ok++;
            LOGI("TexLib: %s %dx%d ok", path, w, h);
        } else {
            LOGE("TexLib: decode failed %s", path);
        }
    }
    LOGI("TexLib: %d/%d materials", ok, TEX_COUNT - 1);
    return ok == TEX_COUNT - 1;
}

void TexLib::shutdown() {
    for (int t = 0; t < TEX_COUNT; t++)
        if (ids_[t]) glDeleteTextures(1, &ids_[t]);
    memset(ids_, 0, sizeof(ids_));
    memset(loaded_, 0, sizeof(loaded_));
}

} // namespace lv
