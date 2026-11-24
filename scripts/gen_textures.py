#!/usr/bin/env python3
"""LEDGE 1.1 — material pack generator. 12 seamless 2048x2048 tileable
textures (periodic value noise -> compresses to rich, heavy PNGs).
Output: ledgevr/app/assets/tex/*.png"""
import numpy as np, os
from PIL import Image

OUT = "/home/z/my-project/ledgevr/app/assets/tex"
os.makedirs(OUT, exist_ok=True)
S = 2048
rng = np.random.default_rng(42)

# tiling value noise: periodic lattice noise summed in octaves
def tiled_noise(size, cells, seed):
    r = np.random.default_rng(seed)
    lat = r.random((cells, cells))
    yi, xi = np.mgrid[0:size, 0:size]
    gy = (yi * cells / size).astype(np.int32) % cells
    gx = (xi * cells / size).astype(np.int32) % cells
    gy1 = (gy + 1) % cells
    gx1 = (gx + 1) % cells
    fy = (yi * cells / size) - np.floor(yi * cells / size)
    fx = (xi * cells / size) - np.floor(xi * cells / size)
    fy = fy * fy * (3 - 2 * fy)
    fx = fx * fx * (3 - 2 * fx)
    a = lat[gy, gx]; b = lat[gy, gx1]; c = lat[gy1, gx]; d = lat[gy1, gx1]
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy

def fbm(size, seed, base_cells=8, octaves=6, gain=0.55):
    total = np.zeros((size, size))
    amp, cells, norm = 1.0, base_cells, 0.0
    for o in range(octaves):
        total += amp * tiled_noise(size, cells, seed + o * 131)
        norm += amp
        amp *= gain
        cells *= 2
    return total / norm

def lerp(a, b, t):
    return a + (b - a) * t

def hexc(h):
    return np.array([int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)])

def save(name, arr):
    im = Image.fromarray((np.clip(arr, 0, 1) * 255).astype(np.uint8), "RGB")
    im.save(f"{OUT}/{name}.png", optimize=True)
    print(f"tex {name}.png  {os.path.getsize(f'{OUT}/{name}.png')/1e6:.2f} MB")

def wood():
    n1 = fbm(S, 101, 4, 5)
    # grain: stretched rings along x
    yy = np.linspace(0, 1, S)[:, None]
    rings = np.sin((yy * 28 + n1 * 6.0) * np.pi) * 0.5 + 0.5
    grain = fbm(S, 102, 3, 6)
    grain = np.repeat(grain, 1, axis=0)
    grain = (grain + np.roll(grain, 7, axis=1)) * 0.5
    base = hexc("E8C9A0")
    dark = hexc("B98A5E")
    light = hexc("F4DFC0")
    t = np.clip(rings * 0.55 + n1 * 0.45, 0, 1)
    img = lerp(base[None, None, :], dark[None, None, :], t[..., None] * 0.8)
    img = lerp(img, light[None, None, :], np.clip(1.2 - t, 0, 1)[..., None] * 0.35)
    # plank seams every 256px along y
    seam = np.zeros((S, S))
    for y0 in range(0, S, 256):
        seam[y0:y0 + 6, :] = 1
    img = img * (1 - seam[..., None] * 0.35)
    return img

def grass():
    n1 = fbm(S, 201, 12, 7)
    n2 = fbm(S, 202, 40, 5)
    blades = tiled_noise(S, 160, 203)
    base = hexc("A8E6C2")
    dark = hexc("6FCF9A")
    light = hexc("D2F5DE")
    t = np.clip(n1 * 0.6 + n2 * 0.25 + blades * 0.35, 0, 1)
    img = lerp(base[None, None, :], dark[None, None, :], t[..., None] * 0.85)
    img = lerp(img, light[None, None, :], np.clip(t - 0.75, 0, 0.3)[..., None] * 1.6)
    # tiny pastel flowers
    fl = tiled_noise(S, 24, 204)
    dots = (fl > 0.86).astype(np.float64)
    img = lerp(img, np.array([1.0, 0.92, 0.96])[None, None, :], dots[..., None] * 0.85)
    return img

def stone():
    n1 = fbm(S, 301, 6, 7)
    cracks = np.abs(fbm(S, 302, 10, 6) - 0.5)
    cracklines = np.clip(1 - cracks * 14, 0, 1)
    base = hexc("BFC9D9")
    dark = hexc("8E9BB0")
    t = np.clip(n1 * 0.9, 0, 1)
    img = lerp(base[None, None, :], dark[None, None, :], t[..., None] * 0.7)
    img = lerp(img, hexc("6E7A8E")[None, None, :], cracklines[..., None] * 0.5)
    speck = (tiled_noise(S, 90, 303) > 0.9).astype(np.float64)
    img = lerp(img, hexc("FFFFFF")[None, None, :], speck[..., None] * 0.25)
    return img

def candy():
    n1 = fbm(S, 401, 14, 5)
    # diagonal candy stripes
    yi, xi = np.mgrid[0:S, 0:S]
    stripe = np.sin((xi + yi) / 30.0) * 0.5 + 0.5
    stripe = (stripe > 0.5).astype(np.float64)
    soft = fbm(S, 403, 8, 4)
    pink = hexc("FF9FB2")
    white = hexc("FFF3F6")
    img = lerp(pink[None, None, :], white[None, None, :], stripe[..., None] * 0.8)
    # glossy swirl
    img = lerp(img, hexc("FFC7D4")[None, None, :], np.clip(n1 - 0.4, 0, 0.6)[..., None] * 0.8)
    # sprinkles
    sp = (tiled_noise(S, 120, 402) > 0.93).astype(np.float64)
    for c, w in ((hexc("8FD8FF"), 1), (hexc("FFF08F"), 2), (hexc("B6F5B0"), 3)):
        m = (tiled_noise(S, 120, 404 + w) > 0.95).astype(np.float64) * (w == 1) + \
            (tiled_noise(S, 130, 405 + w) > 0.955).astype(np.float64) * (w == 2) + \
            (tiled_noise(S, 140, 406 + w) > 0.96).astype(np.float64) * (w == 3)
        img = lerp(img, c[None, None, :], np.clip(m, 0, 1)[..., None] * 0.9)
    img = lerp(img, hexc("FFE3EA")[None, None, :], soft[..., None] * 0.15)
    return img

def metal():
    n1 = fbm(S, 501, 8, 6)
    yy = np.linspace(0, 1, S)[:, None]
    brush = tiled_noise(S, 220, 502)
    brush = np.roll(brush, rng.integers(0, 8), axis=0)
    base = hexc("C9D2DC")
    t = np.clip(brush * 0.5 + n1 * 0.4, 0, 1)
    img = lerp(hexc("AAB4C0")[None, None, :], hexc("E4EAF2")[None, None, :], t[..., None])
    # brushed streaks along y
    streak = tiled_noise(S, 300, 503)
    streak = np.repeat(streak, 1, axis=1)
    img = lerp(img, hexc("98A2AE")[None, None, :], streak[..., None] * 0.25)
    # rivets grid
    ri, rj = np.mgrid[0:S, 0:S]
    rx = (ri % 256 - 128) / 128.0; ry = (rj % 256 - 128) / 128.0
    rd = np.sqrt(rx ** 2 + ry ** 2)
    rivet = np.clip(1 - (rd - 0.14) * 18, 0, 1)
    shade = np.clip(0.12 - rd * 0.1, 0, 1) * (rd < 0.2)
    img = img + shade[..., None] * 0.6
    img = lerp(img, hexc("8B95A2")[None, None, :], rivet[..., None] * 0.3)
    return img

def slate():
    n1 = fbm(S, 601, 5, 7)
    n2 = fbm(S, 602, 16, 5)
    base = hexc("5A6270")
    t = np.clip(n1 * 0.7 + n2 * 0.3, 0, 1)
    img = lerp(hexc("434A57")[None, None, :], hexc("77808F")[None, None, :], t[..., None])
    # faceted look: cell edges
    ci = tiled_noise(S, 18, 603)
    ce = np.abs(ci - 0.5)
    edges = np.clip(1 - ce * 22, 0, 1)
    img = lerp(img, hexc("2E343E")[None, None, :], edges[..., None] * 0.5)
    return img

def sand():
    n1 = fbm(S, 701, 10, 6)
    dunes = np.sin(np.linspace(0, 14, S)[:, None] + n1 * 4) * 0.5 + 0.5
    base = hexc("F2D98C")
    t = np.clip(n1 * 0.45 + dunes * 0.55, 0, 1)
    img = lerp(hexc("E8C878")[None, None, :], hexc("FBEDB8")[None, None, :], t[..., None])
    grains = (tiled_noise(S, 200, 702) > 0.88).astype(np.float64)
    img = lerp(img, hexc("C9A75E")[None, None, :], grains[..., None] * 0.3)
    return img

def leaf():
    n1 = fbm(S, 801, 9, 6)
    vi = tiled_noise(S, 26, 802)
    veins = np.clip(1 - np.abs(vi - 0.5) * 16, 0, 1)
    base = hexc("9CD98C")
    t = np.clip(n1 * 0.8, 0, 1)
    img = lerp(hexc("6FBC64")[None, None, :], hexc("C4EBAF")[None, None, :], t[..., None])
    img = lerp(img, hexc("529A4B")[None, None, :], veins[..., None] * 0.4)
    return img

def cloud():
    n1 = fbm(S, 901, 6, 7)
    n1 = np.clip((n1 - 0.25) * 1.6, 0, 1)
    base = hexc("FFFFFF")
    shade = hexc("D8E4F2")
    img = lerp(base[None, None, :], shade[None, None, :], n1[..., None] * 0.55)
    # soft puffy bumps
    puff = tiled_noise(S, 20, 902)
    img = lerp(img, hexc("EFF5FC")[None, None, :], np.clip(puff - 0.4, 0, 0.6)[..., None] * 0.6)
    return img

def neon_grid():
    yi, xi = np.mgrid[0:S, 0:S]
    gx = np.abs((xi % 128) - 64) / 64.0
    gy = np.abs((yi % 128) - 64) / 64.0
    lines = np.clip(1 - gx * 8, 0, 1) + np.clip(1 - gy * 8, 0, 1)
    base = hexc("1E2433")
    glow = np.clip(lines, 0, 1)
    cyan = hexc("6FE3FF")
    pink = hexc("FF9ADF")
    img = np.tile(base[None, None, :], (S, S, 1)).astype(np.float64)
    t = (xi + yi) / (2 * S)
    nline = lerp(cyan[None, None, :], pink[None, None, :], t[..., None])
    img = lerp(img, nline, glow[..., None] * 0.95)
    n1 = fbm(S, 1001, 10, 5)
    img = lerp(img, hexc("2A3247")[None, None, :], np.clip(n1 - 0.3, 0, 0.7)[..., None] * 0.5)
    return img

def rubber():
    n1 = fbm(S, 1101, 12, 6)
    base = hexc("7FA8E8")
    t = np.clip(n1 * 0.9, 0, 1)
    img = lerp(hexc("5F88D4")[None, None, :], hexc("A5C6F2")[None, None, :], t[..., None])
    # rubber nubs
    ri, rj = np.mgrid[0:S, 0:S]
    rx = (ri % 64 - 32) / 32.0; ry = (rj % 64 - 32) / 32.0
    rd = np.sqrt(rx ** 2 + ry ** 2)
    nub = np.clip(1 - rd * 2.2, 0, 1)
    img = img + nub[..., None] * 0.18
    return img

def marble():
    n1 = fbm(S, 1201, 5, 6)
    veins = np.abs(fbm(S, 1202, 8, 7) - 0.5)
    veinlines = np.clip(1 - veins * 10, 0, 1) ** 1.5
    base = hexc("F4F1EA")
    img = lerp(base[None, None, :], hexc("DDD8CE")[None, None, :], n1[..., None] * 0.5)
    img = lerp(img, hexc("B8B0A4")[None, None, :], veinlines[..., None] * 0.55)
    gold = (tiled_noise(S, 30, 1203) > 0.9).astype(np.float64)
    img = lerp(img, hexc("E8C87A")[None, None, :], gold[..., None] * 0.35)
    return img

if __name__ == "__main__":
    save("wood_pastel", wood())
    save("grass_mint", grass())
    save("stone_sky", stone())
    save("candy_pink", candy())
    save("metal_silver", metal())
    save("slate_dark", slate())
    save("sand_gold", sand())
    save("leaf_green", leaf())
    save("cloud_soft", cloud())
    save("neon_grid", neon_grid())
    save("rubber_blue", rubber())
    save("marble_white", marble())
    total = sum(os.path.getsize(f"{OUT}/{f}") for f in os.listdir(OUT))
    print(f"TEXTURES DONE total {total/1e6:.1f} MB")
