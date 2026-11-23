#!/usr/bin/env python3
"""LEDGE 1.1 — soundtrack + SFX synthesizer.
Composes 7 original background-music tracks and ~14 sound effects entirely
with numpy (no samples), encodes to OGG via ffmpeg.
Output: ledgevr/app/assets/music/*.ogg, ledgevr/app/assets/sfx/*.ogg
"""
import numpy as np, subprocess, os, struct, wave, sys

SR = 44100
OUT_M = "/home/z/my-project/ledgevr/app/assets/music"
OUT_S = "/home/z/my-project/ledgevr/app/assets/sfx"
os.makedirs(OUT_M, exist_ok=True)
os.makedirs(OUT_S, exist_ok=True)

def wavwrite(path, x, sr=SR):
    x = np.clip(x, -1, 1)
    d = (x * 32767).astype('<i2').tobytes()
    with wave.open(path, 'wb') as w:
        w.setnchannels(2 if x.ndim == 2 else 1)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(d)

def to_ogg(wavpath, oggpath, q="4"):
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", wavpath,
                    "-c:a", "libvorbis", "-q:a", q, oggpath], check=True)

# ---------------------------------------------------------------------------
# instrument primitives
# ---------------------------------------------------------------------------
def envexp(n, a, d):
    t = np.arange(n) / SR
    e = np.minimum(t / max(a, 1e-4), 1.0) * np.exp(-t / d)
    return e

def sine(f, n, ph=0.0):
    return np.sin(2 * np.pi * f * np.arange(n) / SR + ph)

def pluck(freq, dur, vol=0.5, bright=3.0):
    n = int(dur * SR)
    t = np.arange(n) / SR
    x = np.zeros(n)
    for k, a in ((1, 1.0), (2, 0.42), (3, 0.24), (4, 0.12)):
        if freq * k < SR * 0.45:
            x += a * np.sin(2 * np.pi * freq * k * t)
    x *= envexp(n, 0.004, dur * 0.42) * bright * 0.3
    return vol * x

def padchord(freqs, dur, vol=0.3, detune=0.0015):
    n = int(dur * SR)
    t = np.arange(n) / SR
    x = np.zeros(n)
    for f in freqs:
        for dt in (-detune, 0, detune):
            x += np.sin(2 * np.pi * f * (1 + dt) * t)
        x += 0.35 * np.sin(2 * np.pi * f * 2 * t)
    e = np.minimum(t / 0.35, 1.0) * np.minimum((dur - t) / 0.35, 1.0)
    return vol * x * e

def bass(freq, dur, vol=0.55):
    n = int(dur * SR)
    t = np.arange(n) / SR
    x = np.sin(2 * np.pi * freq * t) + 0.25 * np.sin(4 * np.pi * freq * t)
    x = np.tanh(x * 1.8)
    e = envexp(n, 0.006, dur * 0.5) * np.minimum((dur - t) / 0.05 + 1, 1)
    return vol * x * e

def kick(dur=0.28, vol=0.9):
    n = int(dur * SR)
    t = np.arange(n) / SR
    f = 130 * np.exp(-t * 26) + 42
    x = np.sin(2 * np.pi * np.cumsum(f) / SR)
    x *= envexp(n, 0.002, 0.11)
    return vol * x

def snare(dur=0.18, vol=0.5):
    n = int(dur * SR)
    t = np.arange(n) / SR
    x = np.random.default_rng(7).standard_normal(n) * np.exp(-t * 30)
    x += 0.5 * sine(210, n) * np.exp(-t * 40)
    return vol * x

def hat(dur=0.07, vol=0.22, open_=False):
    n = int((dur if not open_ else 0.16) * SR)
    t = np.arange(n) / SR
    x = np.random.default_rng(3).standard_normal(n)
    x = np.diff(np.concatenate([[0], x]))  # crude highpass
    x *= np.exp(-t * (60 if not open_ else 22))
    return vol * x * 2.2

def shaker(dur=0.12, vol=0.16):
    n = int(dur * SR)
    t = np.arange(n) / SR
    x = np.random.default_rng(11).standard_normal(n)
    x = np.diff(np.concatenate([[0], x]))
    x *= np.sin(np.pi * np.arange(n) / n) ** 1.5 * np.exp(-t * 18)
    return vol * x * 2.0

def mix_at(bus, snd, at):
    i = int(at * SR)
    need = i + len(snd)
    if need > len(bus):
        bus = np.concatenate([bus, np.zeros(need - len(bus) + SR)])
    bus[i:i + len(snd)] += snd
    return bus

def note_hz(semi_from_a4):
    return 440.0 * 2 ** (semi_from_a4 / 12.0)

# scale helpers (semitones from A4)
def scale_degrees(root_semi, steps):
    return [root_semi + s for s in steps]

MAJ = [0, 2, 4, 5, 7, 9, 11]
MIN = [0, 2, 3, 5, 7, 8, 10]

def chord_of(root_semi, quality, idx):
    r = root_semi + (idx * 2 + int(idx == 6)) % 7 if False else root_semi
    base = MAJ if quality == 'maj' else MIN
    d = [0, 2, 4] if len(base) > 4 else [0, 2, 4]
    out = []
    for k in d:
        s = base[(k + idx * 0) % 7] + 12 * ((k + idx * 0) // 7) + idx * 0
        out.append(root_semi + base[(idx + k) % 7] + 12 * ((idx + k) // 7))
    return out

# ---------------------------------------------------------------------------
# song renderer
# ---------------------------------------------------------------------------
def render_song(name, bpm, root, quality, prog, style, bars=48, seed=1):
    """prog: list of chord indices per bar. style: 'lofi','house','synth','chill','bounce','groove','epic'"""
    rng = np.random.default_rng(seed)
    spb = 60.0 / bpm                      # seconds per beat
    bus = np.zeros(int(bars * 4 * spb * SR) + SR * 2)
    for bar in range(bars):
        ci = prog[bar % len(prog)]
        ch = chord_of(root, quality, ci)
        t0 = bar * 4 * spb
        # pad chord every bar (octave up for air)
        bus = mix_at(bus, padchord([note_hz(c - 12) for c in ch], 4 * spb, vol=0.16 if style != 'epic' else 0.22), t0)
        # bass on beat pattern
        bp = [0, 1.5, 2, 3] if style in ('groove', 'bounce') else [0, 2] if style == 'chill' else [0, 1, 2, 3]
        for b in bp:
            bus = mix_at(bus, bass(note_hz(ch[0] - 24), spb * 0.9, vol=0.34), t0 + b * spb)
        # arpeggio 8ths
        if style in ('house', 'synth', 'epic', 'groove'):
            for i in range(8):
                nn = ch[i % 3] + 12 * (1 + (i % 4 == 3))
                bus = mix_at(bus, pluck(note_hz(nn), 0.22, vol=0.14, bright=2.2), t0 + i * 0.5 * spb)
        # melody — seeded walk on scale
        if style in ('lofi', 'chill', 'synth', 'epic'):
            t = t0
            while t < t0 + 4 * spb - 0.3:
                dur = rng.choice([0.5, 0.5, 1.0, 1.5]) * spb
                step = int(rng.integers(0, 7))
                sc = MAJ if quality == 'maj' else MIN
                nn = root + sc[step] + 12 * rng.integers(0, 2)
                bus = mix_at(bus, pluck(note_hz(nn), dur * 0.95, vol=0.16, bright=2.8), t)
                t += dur
        # drums
        if style != 'chill':
            for b in range(4):
                bus = mix_at(bus, kick(vol=0.5 if style == 'lofi' else 0.75), t0 + b * spb)
        else:
            bus = mix_at(bus, kick(vol=0.45), t0)
            bus = mix_at(bus, kick(vol=0.4), t0 + 2.5 * spb)
        # snare/clap on 2 & 4
        if style in ('house', 'synth', 'groove', 'epic', 'bounce'):
            bus = mix_at(bus, snare(vol=0.4), t0 + 1 * spb)
            bus = mix_at(bus, snare(vol=0.4), t0 + 3 * spb)
        elif style == 'lofi':
            bus = mix_at(bus, snare(vol=0.22, dur=0.14), t0 + 1 * spb)
            bus = mix_at(bus, snare(vol=0.22, dur=0.14), t0 + 3 * spb)
        # hats
        for i in range(8):
            if style == 'lofi' and i % 2 == 0:
                continue
            bus = mix_at(bus, hat(open_=(i == 7 and style != 'chill'),
                                  vol=0.10 if style == 'lofi' else 0.16), t0 + i * 0.5 * spb)
        if style in ('lofi', 'chill'):
            bus = mix_at(bus, shaker(vol=0.12), t0 + 0.5 * spb)
            bus = mix_at(bus, shaker(vol=0.10), t0 + 2.5 * spb)
    # stereo widen: slight delay right channel
    delay = int(0.0007 * SR)
    left, right = bus[:-delay], bus[delay:]
    n = min(len(left), len(right))
    stereo = np.stack([left[:n], right[:n]], axis=1)
    # master: normalize, soft clip, fade in/out
    stereo = stereo / (np.abs(stereo).max() + 1e-9) * 0.82
    stereo = np.tanh(stereo * 1.15) * 0.9
    fi, fo = int(0.6 * SR), int(2.2 * SR)
    stereo[:fi] *= np.linspace(0, 1, fi)[:, None]
    stereo[-fo:] *= np.linspace(1, 0, fo)[:, None]
    wav = f"/tmp/{name}.wav"
    ogg = f"{OUT_M}/{name}.ogg"
    wavwrite(wav, stereo)
    to_ogg(wav, ogg, q="6")
    os.remove(wav)
    sz = os.path.getsize(ogg) / 1e6
    print(f"music {name}.ogg  {sz:.2f} MB")

SONGS = [
    ("menu_candy_skies", 84, 0, 'maj', [0, 5, 3, 4], 'lofi', 88, 11),
    ("build_ledge_loops", 100, -2, 'maj', [0, 3, 4, 5], 'groove', 96, 22),
    ("game_pastel_rush", 118, 2, 'maj', [0, 4, 5, 3], 'house', 104, 33),
    ("game_cloud_hop", 92, -5, 'min', [0, 5, 3, 4], 'chill', 88, 44),
    ("game_neon_climb", 126, 3, 'min', [0, 2, 5, 6], 'synth', 112, 55),
    ("game_bubble_bass", 108, -7, 'maj', [0, 3, 5, 1], 'bounce', 96, 66),
    ("credits_star_splash", 112, 5, 'maj', [0, 5, 1, 4], 'epic', 96, 77),
]

# ---------------------------------------------------------------------------
# SFX
# ---------------------------------------------------------------------------
def sfx_grab():
    n = int(0.16 * SR); t = np.arange(n) / SR
    f = np.linspace(340, 520, n)
    x = np.sin(2 * np.pi * np.cumsum(f) / SR) * np.sin(np.pi * np.arange(n) / n) * 0.5
    return x * envexp(n, 0.004, 0.06)

def sfx_release():
    n = int(0.2 * SR); t = np.arange(n) / SR
    f = np.linspace(520, 300, n)
    x = np.sin(2 * np.pi * np.cumsum(f) / SR) * 0.4
    return x * envexp(n, 0.003, 0.07)

def sfx_land():
    n = int(0.22 * SR); t = np.arange(n) / SR
    x = np.random.default_rng(5).standard_normal(n) * np.exp(-t * 26) * 0.6
    x += np.sin(2 * np.pi * 95 * t) * np.exp(-t * 22) * 0.7
    return np.tanh(x * 2)

def sfx_goal():
    bus = np.zeros(int(1.3 * SR))
    for i, semi in enumerate([0, 4, 7, 12]):
        bus = mix_at(bus, pluck(note_hz(semi + 3), 0.5, vol=0.4, bright=4), i * 0.09)
        bus = mix_at(bus, pluck(note_hz(semi + 15), 0.4, vol=0.22, bright=3), i * 0.09 + 0.02)
    return bus

def sfx_bounce():
    n = int(0.3 * SR); t = np.arange(n) / SR
    f = 90 + 160 * np.exp(-t * 8)
    x = np.sin(2 * np.pi * np.cumsum(f) / SR)
    x *= envexp(n, 0.003, 0.13)
    return np.tanh(x * 2.2) * 0.8

def sfx_place():
    n = int(0.14 * SR); t = np.arange(n) / SR
    f = np.linspace(190, 420, n)
    x = np.sin(2 * np.pi * np.cumsum(f) / SR) * 0.5
    x += np.diff(np.concatenate([[0], np.random.default_rng(2).standard_normal(n)])) * np.exp(-t * 55) * 0.5
    return x * envexp(n, 0.002, 0.05)

def sfx_delete():
    n = int(0.2 * SR); t = np.arange(n) / SR
    f = np.linspace(500, 140, n)
    x = np.sin(2 * np.pi * np.cumsum(f) / SR) * 0.5
    return x * envexp(n, 0.002, 0.08)

def sfx_click():
    n = int(0.06 * SR); t = np.arange(n) / SR
    x = np.sin(2 * np.pi * 900 * t) * np.exp(-t * 90) * 0.5
    x += np.sin(2 * np.pi * 1400 * t) * np.exp(-t * 120) * 0.25
    return x

def sfx_open():
    bus = np.zeros(int(0.5 * SR))
    for i, semi in enumerate([10, 15, 19]):
        bus = mix_at(bus, pluck(note_hz(semi), 0.3, vol=0.25, bright=3), i * 0.05)
    return bus

def sfx_success():
    bus = np.zeros(int(0.9 * SR))
    for i, semi in enumerate([4, 7, 11]):
        bus = mix_at(bus, pluck(note_hz(semi + 3), 0.4, vol=0.3, bright=3.5), i * 0.07)
    return bus

def sfx_whoosh():
    n = int(0.35 * SR); t = np.arange(n) / SR
    x = np.random.default_rng(9).standard_normal(n)
    x = np.convolve(x, np.ones(24) / 24, 'same')
    e = np.sin(np.pi * np.arange(n) / n) ** 2
    return x * e * 0.7

def sfx_pop():
    n = int(0.09 * SR); t = np.arange(n) / SR
    f = np.linspace(260, 760, n)
    x = np.sin(2 * np.pi * np.cumsum(f) / SR)
    return x * np.exp(-t * 34) * 0.55

def sfx_emote():
    bus = np.zeros(int(0.7 * SR))
    for i, semi in enumerate([7, 11, 14]):
        bus = mix_at(bus, pluck(note_hz(semi + 3), 0.25, vol=0.3, bright=3), i * 0.08)
    return bus

def write_sfx(name, x, vol=0.9):
    x = np.asarray(x, dtype=np.float64)
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    x = x / (np.abs(x).max() + 1e-9) * vol
    wav = f"/tmp/{name}.wav"
    ogg = f"{OUT_S}/{name}.ogg"
    wavwrite(wav, x)
    to_ogg(wav, ogg, q="4")
    os.remove(wav)
    print(f"sfx {name}.ogg  {os.path.getsize(ogg)/1024:.0f} KB")

if __name__ == "__main__":
    only_sfx = "--sfx" in sys.argv
    only_music = "--music" in sys.argv
    if not only_sfx:
        for s in SONGS:
            render_song(*s)
    if not only_music:
        write_sfx("grab", sfx_grab())
        write_sfx("release", sfx_release())
        write_sfx("land", sfx_land())
        write_sfx("goal", sfx_goal())
        write_sfx("bounce", sfx_bounce())
        write_sfx("place", sfx_place())
        write_sfx("delete", sfx_delete())
        write_sfx("click", sfx_click())
        write_sfx("open", sfx_open())
        write_sfx("success", sfx_success())
        write_sfx("whoosh", sfx_whoosh())
        write_sfx("pop", sfx_pop())
        write_sfx("emote", sfx_emote())
    print("AUDIO DONE")
