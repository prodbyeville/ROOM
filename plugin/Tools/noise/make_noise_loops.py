#!/usr/bin/env python3
"""Turns Ethan's noise recordings into ROOM's seamless noise loops.

    python3 -I make_noise_loops.py <folder of source WAVs> <plugin/Resources/Noise>

For every entry in LOOPS below (source file stem, menu name, section, edge trims):
  1. read it; keep it mono when both channels are the same (most are)
  2. trim the edges (fade-ins, handling bumps) and keep at most 30 s
  3. make it loop: pick the loop end where what follows it best matches the start,
     then crossfade that continuation into the start (equal power, corrected for how
     alike the two are, so the level doesn't dip or bump), so the last sample flows
     straight into the first
  4. set the level: about -22 dBFS rms, peaks under -1 dBFS (sparse crackles get a
     soft limit instead of being turned right down)
  5. check the seam (no step bigger than the loop's own sample-to-sample changes, no
     level change across it) and fail loudly if it isn't clean
  6. write "NN name.ogg" (Vorbis q6) and a line in noise.txt: number, name, section and
     the noise screen's look worked out from the sound

The numbers are the noise type parameter's index + 1 and are saved in projects:
never renumber, only append.
"""
import os, sys, subprocess, tempfile
import numpy as np, soundfile as sf
from scipy import signal

SR = 44100
# stem, name, section, trim start s, trim end s
LOOPS = [
    # nature
    ("env_waves",             "waves",             "nature",   0.5, 0.5),
    ("env_waves2",            "tide",              "nature",   0.5, 0.5),
    ("env_waves3",            "shore",             "nature",   0.5, 0.5),
    ("env_beforethewaves",    "before the waves",  "nature",   0.6, 0.3),
    ("rad_rainingitspouring", "pouring rain",      "nature",   0.3, 0.3),
    ("env_gloomydays",        "grey day",          "nature",   0.3, 0.3),
    ("psy_crazyrain",         "drips",             "nature",   0.1, 0.1),
    ("env_flowing",           "stream",            "nature",   0.3, 0.3),
    ("rad_water",             "water",             "nature",   0.2, 0.3),
    ("env_birdsfly",          "birds",             "nature",   0.3, 0.3),
    ("rad_dolphins",          "dolphins",          "nature",   0.5, 0.5),
    ("env_gravelroad",        "gravel",            "nature",   0.3, 0.3),
    ("env_awayfromit",        "far away",          "nature",   0.3, 0.3),
    # places
    ("atm_room",              "empty room",        "places",   0.8, 0.2),
    ("atm_ac1",               "air con",           "places",   0.2, 0.2),
    ("atm_ac2",               "ac hum",            "places",   0.2, 0.2),
    ("psy_artclass",          "art class",         "places",   0.3, 0.3),
    ("psy_takeout",           "takeout",           "places",   0.2, 0.2),
    ("rad_talking",           "chatter",           "places",   0.2, 0.6),
    ("rad_bus",               "bus",               "places",   0.3, 0.3),
    ("psy_enviromental",      "outside",           "places",   0.2, 0.2),
    ("rad_nightnight",        "night",             "places",   0.3, 0.3),
    ("rad_paradiseisland",    "island",            "places",   0.3, 0.3),
    ("atm_birdflew",          "open window",       "places",   0.2, 0.2),
    ("atm_spotted",           "hallway",           "places",   0.3, 0.3),
    # machines & tape
    ("tas_tascam",            "tascam",            "machines", 0.3, 0.3),
    ("tas_crackle",           "crackle",           "machines", 0.1, 0.1),
    ("tas_ecofriendly",       "tape eco",          "machines", 1.4, 0.2),
    ("tas_isle",              "tape isle",         "machines", 0.1, 0.1),
    ("tas_script",            "tape script",       "machines", 0.9, 0.2),
    ("tas_chambers",          "tape chamber",      "machines", 1.1, 0.2),
    ("tas_rainquestionmark",  "tape rain",         "machines", 0.1, 0.1),
    ("rad_electrical",        "electric",          "machines", 0.1, 0.1),
    ("rad_stopthebuzz",       "buzz",              "machines", 0.3, 0.3),
    ("env_sostransmission",   "sos radio",         "machines", 0.3, 0.3),
    ("rad_somethingswrong",   "something's wrong", "machines", 0.1, 0.1),
    ("rad_micronoise",        "micro",             "machines", 0.3, 0.3),
    ("psy_blooperair",        "dead air",          "machines", 0.2, 0.2),
    ("psy_warmandfuzzy",      "warm fuzz",         "machines", 0.2, 0.2),
    # tonal
    ("rad_anthem",            "anthem",            "tonal",    0.3, 0.3),
    ("rad_ascent",            "ascent",            "tonal",    0.3, 0.3),
    ("rad_ooohahhh",          "ooh ahh",           "tonal",    0.3, 2.5),
    ("rad_whistle",           "whistle",           "tonal",    0.3, 0.3),
    ("atm_sphere",            "sphere",            "tonal",    1.2, 0.2),
    ("atm_bloopersonics",     "bloops",            "tonal",    0.2, 0.2),
    ("atm_shakeit",           "shaker",            "tonal",    0.2, 0.2),
    ("rad_lovestruck",        "lovestruck",        "tonal",    0.2, 0.2),
    ("psy_life",              "life",              "tonal",    0.2, 0.8),
    ("psy_carbine",           "carbine",           "tonal",    0.1, 0.1),
    ("psy_lukewarmwater",     "lukewarm",          "tonal",    0.2, 0.2),
]
FIRST_NUMBER = 5          # 1-4 are the built-in vinyl, tape hiss, air, room tone
MAX_SECONDS = 30.0
TARGET_RMS_DB, PEAK_DB = -22.0, -1.0


def db(x):
    return 20 * np.log10(max(float(x), 1e-12))


def make_loop(x):
    """x: (n, ch). Returns a loop whose end flows into its start."""
    n = len(x)
    fade = int(np.clip(0.15 * n, 0.4 * SR, 3.0 * SR))
    fade = min(fade, n // 3)
    mono = x.mean(1)
    head = mono[:fade]
    # where can the loop end so that what follows it looks most like the start?
    lo = max(fade * 2, n - fade - int(min(0.5 * SR, 0.06 * n)))
    best, best_r = n - fade, -2.0
    for end in range(lo, n - fade + 1, 64):
        tail = mono[end:end + fade]
        r = np.dot(head, tail) / (np.linalg.norm(head) * np.linalg.norm(tail) + 1e-12)
        if r > best_r:
            best, best_r = end, r
    end = best
    t = (np.arange(fade) + 0.5) / fade * (np.pi / 2)
    a, b = np.sin(t), np.cos(t)                       # a: the start fading in, b: the continuation fading out
    r = float(np.clip(best_r, -0.5, 1.0))
    norm = 1.0 / np.sqrt(a * a + b * b + 2 * r * a * b)   # keeps the level flat whatever the likeness
    out = x[:end].copy()
    out[:fade] = (x[:fade] * a[:, None] + x[end:end + fade] * b[:, None]) * norm[:, None]
    return out, fade, best_r


def set_level(x):
    rms = np.sqrt((x ** 2).mean())
    peak = np.abs(x).max()
    g = 10 ** (TARGET_RMS_DB / 20) / max(rms, 1e-9)
    y = x * g
    lim = 10 ** (PEAK_DB / 20)
    if np.abs(y).max() > lim:
        # soft-limit at most 9 dB of peaks (crackles, clicks); beyond that turn it down
        over = np.abs(y).max() / lim
        if over > 10 ** (9 / 20):
            y = y / (over / 10 ** (9 / 20))
        knee = 0.5 * lim
        a = np.abs(y)
        soft = np.where(a > knee, knee + (lim - knee) * np.tanh((a - knee) / (lim - knee)), a)
        y = np.sign(y) * soft
    return y


def seam_report(loop):
    """Step at the wrap vs the loop's own steps, and level either side of it."""
    m = loop.mean(1)
    d = np.abs(np.diff(m))
    wrap = abs(m[0] - m[-1])
    p999 = np.percentile(d, 99.9)
    w = int(0.05 * SR)
    before = db(np.sqrt((m[-w:] ** 2).mean()))
    after = db(np.sqrt((m[:w] ** 2).mean()))
    # typical change of 50 ms rms between neighbouring windows elsewhere
    frames = m[: len(m) // w * w].reshape(-1, w)
    lv = 20 * np.log10(np.sqrt((frames ** 2).mean(1)) + 1e-12)
    typical = np.percentile(np.abs(np.diff(lv)), 95)
    return wrap, p999, after - before, typical


def look(loop):
    """The noise screen's particles, from the sound: count (density), thickness (level
    swing), speed (brightness), waves (slow movement), sparkle (crackles)."""
    m = loop.mean(1)
    f, p = signal.welch(m, SR, nperseg=4096)
    centroid = float((f * p).sum() / (p.sum() + 1e-20))
    flat = float(np.exp(np.mean(np.log(p + 1e-20))) / (np.mean(p) + 1e-20))
    w = int(0.05 * SR)
    frames = m[: len(m) // w * w].reshape(-1, w)
    lv = 20 * np.log10(np.sqrt((frames ** 2).mean(1)) + 1e-9)
    swing = float(np.percentile(lv, 90) - np.percentile(lv, 10))
    crest = db(np.abs(m).max()) - db(np.sqrt((m ** 2).mean()))
    count = int(np.clip(400 + 1600 * flat ** 0.5, 300, 1600))
    thickness = float(np.clip(0.12 + swing / 60.0, 0.12, 0.45))
    speed = float(np.clip(0.04 + centroid / 20000.0, 0.04, 0.32))
    waves = float(np.clip(0.6 + swing / 12.0, 0.6, 2.4))
    sparkle = float(np.clip((crest - 12.0) / 30.0, 0.0, 0.3))
    return count, thickness, speed, waves, sparkle


def main(src, dst):
    os.makedirs(dst, exist_ok=True)
    lines, failed = [], []
    for k, (stem, name, section, t0, t1) in enumerate(LOOPS):
        num = FIRST_NUMBER + k
        x, sr = sf.read(os.path.join(src, stem + ".wav"), always_2d=True, dtype="float64")
        assert sr == SR, (stem, sr)
        if x.shape[1] > 1 and np.allclose(x[:, 0], x[:, 1], atol=1e-5):
            x = x[:, :1]
        x = x[:, :2]
        x = x[int(t0 * SR): len(x) - int(t1 * SR)]
        x = x[: int(MAX_SECONDS * SR)]
        x = x - x.mean(0)                          # no DC: a DC step at the seam would click
        loop, fade, r = make_loop(x)
        loop = set_level(loop)
        wrap, p999, jump_db, typical = seam_report(loop)
        ok = wrap <= max(p999, 1e-4) and abs(jump_db) <= max(1.5, 1.5 * typical)
        fname = f"{num:02d} {name}.ogg"
        with tempfile.NamedTemporaryFile(suffix=".wav") as tmp:
            sf.write(tmp.name, loop.astype(np.float32), SR, subtype="FLOAT")
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", tmp.name, "-c:a", "libvorbis", "-q:a", "6",
                            os.path.join(dst, fname)], check=True)
        # decode it back: the seam must survive the encoder too
        dec, _ = sf.read(os.path.join(dst, fname), always_2d=True)
        if abs(len(dec) - len(loop)) > 0:
            print(f"   note: {fname} decodes to {len(dec)} samples, not {len(loop)}")
        dwrap, dp999, djump, dtyp = seam_report(dec)
        ok = ok and dwrap <= max(dp999, 1e-4) and abs(djump) <= max(1.5, 1.5 * dtyp)
        c, th, sp, wv, spk = look(loop)
        lines.append(f"{num}|{name}|{section}|{c}|{th:.2f}|{sp:.2f}|{wv:.2f}|{spk:.2f}")
        print(f"{fname:26s} {loop.shape[1]}ch {len(loop)/SR:5.1f}s fade {fade/SR:4.2f}s match {r:+.2f} "
              f"wrap {wrap:.4f}/{p999:.4f} level {jump_db:+.1f}/{typical:.1f} dB  ogg {dwrap:.4f}/{dp999:.4f} {djump:+.1f}  "
              f"{'ok' if ok else 'SEAM!'}")
        if not ok:
            failed.append(fname)
    with open(os.path.join(dst, "noise.txt"), "w") as f:
        f.write("# number|name|section|count|thickness|speed|waves|sparkle (made by Tools/noise/make_noise_loops.py)\n")
        f.write("\n".join(lines) + "\n")
    if failed:
        sys.exit("seams not clean: " + ", ".join(failed))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
