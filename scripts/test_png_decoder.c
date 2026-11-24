// host test for the minimal PNG decoder (same code as textures.cpp)
#include <cstdio>
#include <cstring>
#include <vector>
#include <zlib.h>
#include <cstdlib>
#include <cmath>

typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef unsigned long uLongf_;  // unused shim

static uint32_t pngChunk(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static int paeth(int a, int b, int c) {
    int p = a + b - c;
    int pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
}
static bool pngDecodeRGBA(const uint8_t* data, size_t size, std::vector<uint8_t>& out,
                          int& w, int& h) {
    static const uint8_t sig[8] = {137, 'P', 'N', 'G', 13, 10, 26, 10};
    if (size < 33 || memcmp(data, sig, 8) != 0) return false;
    size_t pos = 16;
    uint32_t iw = pngChunk(data + pos); pos += 4;
    uint32_t ih = pngChunk(data + pos); pos += 4;
    uint8_t bd = data[pos], ct = data[pos + 1], interlace = data[pos + 4];
    pos += 5 + 4;
    if (bd != 8 || interlace != 0) return false;
    int ch = ct == 6 ? 4 : ct == 2 ? 3 : ct == 0 ? 1 : 0;
    if (!ch) return false;
    w = (int)iw; h = (int)ih;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
    std::vector<uint8_t> idat;
    bool seenEnd = false;
    while (pos + 8 <= size && !seenEnd) {
        uint32_t len = pngChunk(data + pos);
        const uint8_t* type = data + pos + 4;
        pos += 8;
        if (pos + len + 4 > size) return false;
        if (!memcmp(type, "IDAT", 4)) idat.insert(idat.end(), data + pos, data + pos + len);
        else if (!memcmp(type, "IEND", 4)) seenEnd = true;
        pos += len + 4;
    }
    if (idat.empty()) return false;
    uLongf rawLen = (uLongf)h * (1 + (uLongf)w * ch);
    std::vector<uint8_t> raw(rawLen);
    if (uncompress(raw.data(), &rawLen, idat.data(), (uLong)idat.size()) != Z_OK) return false;
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
        uint8_t* dst = out.data() + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            dst[x * 4] = line[x * ch];
            dst[x * 4 + 1] = line[x * ch + 1];
            dst[x * 4 + 2] = line[x * ch + 2];
            dst[x * 4 + 3] = 255;
        }
        memcpy(prevLine.data(), line.data(), stride);
        havePrev = true;
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: %s file.png\n", argv[0]); return 1; }
    FILE* f = fopen(argv[1], "rb");
    if (!f) { printf("open fail\n"); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(n);
    if (fread(buf.data(), 1, n, f) != (size_t)n) { printf("read fail\n"); fclose(f); return 1; }
    fclose(f);
    std::vector<uint8_t> px;
    int w = 0, h = 0;
    if (!pngDecodeRGBA(buf.data(), buf.size(), px, w, h)) { printf("DECODE FAIL\n"); return 1; }
    // corner + center samples
    auto at = [&](int x, int y) {
        uint8_t* p = px.data() + ((size_t)y * w + x) * 4;
        printf("px(%d,%d) = %d,%d,%d,%d\n", x, y, p[0], p[1], p[2], p[3]);
    };
    printf("DECODE OK %dx%d  bytes=%zu\n", w, h, px.size());
    at(0, 0); at(w - 1, 0); at(0, h - 1); at(w / 2, h / 2);
    if (argc >= 3) {
        FILE* o = fopen(argv[2], "wb");
        fprintf(o, "P6\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; i++) fwrite(px.data() + i * 4, 1, 3, o);
        fclose(o);
    }
    // seam check: row 0 vs row h-1 should be close (tiling texture)
    uint8_t* r0 = px.data();
    uint8_t* rN = px.data() + (size_t)(h - 1) * w * 4;
    long diff = 0;
    for (int x = 0; x < w * 4; x++) diff += abs(r0[x] - rN[x]);
    printf("top/bottom seam diff total=%ld (avg %.2f)\n", diff, diff / (double)(w * 4));
    return 0;
}
