#!/usr/bin/env python3
"""
Generate synthetic test files so the prototype can run with zero setup.

Writes to examples/audio/:

  ROOM SOURCES (things to derive a room from)
    hall_clap.wav     a clap in a big diffuse space (should score SPATIAL, near 1)
    bottle_tap.wav    a tap on a resonant glass object, dry (should score TONAL, near 0)
    room_tone.wav     steady background hiss + hum, no transients (tests the fallback)
    small_room_clap.wav  a clap in a small, fairly dead room
    metal_pipe.wav    a struck metal pipe (inharmonic, long ringing)

  TARGETS (things to process)
    drum_oneshot.wav  a kick + snare-ish hit
    melody.wav        a short synth phrase

Real recordings will always be the true test. These are known quantities,
so we can check the analysis is measuring what we think it is.

Usage:
    python examples/make_test_audio.py
"""

import os
import sys

import numpy as np
from scipy import signal

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from room_engine.audio_io import save_audio, DEFAULT_SR  # noqa: E402

SR = DEFAULT_SR
OUT = os.path.join(os.path.dirname(__file__), "audio")
rng = np.random.default_rng(1234)


def hall_clap():
    """
    Clap = 4 ms noise burst. Hall = band-by-band decaying noise where lows
    ring longer (2.2 s) than highs (0.7 s), like a real large room.
    """
    dur = 3.5
    n = int(dur * SR)
    t = np.arange(n) / SR
    edges = np.geomspace(60, 16000, 25)
    tail = np.zeros(n)
    for lo, hi in zip(edges[:-1], edges[1:]):
        fc = np.sqrt(lo * hi)
        # T60 slides from 2.2 s at 60 Hz down to 0.7 s at 16 kHz.
        t60 = np.interp(np.log2(fc), [np.log2(60), np.log2(16000)], [2.2, 0.7])
        sos = signal.butter(4, [lo, min(hi, 0.45 * SR)], btype="bandpass", fs=SR, output="sos")
        tail += signal.sosfilt(sos, rng.standard_normal(n)) * np.exp(-6.91 * t / t60)
    burst = rng.standard_normal(int(0.004 * SR)) * 4.0
    x = 0.15 * tail
    x[: len(burst)] += burst * 0.3
    x += 1e-4 * rng.standard_normal(n)  # tiny noise floor, like a real mic
    return x / np.max(np.abs(x)) * 0.8


def bottle_tap():
    """
    A short click exciting a few narrow resonances (damped sine waves),
    with almost no room around it.
    """
    dur = 2.0
    n = int(dur * SR)
    t = np.arange(n) / SR
    modes = [  # (frequency Hz, amplitude, T60 s)
        (410.0, 1.0, 0.9),
        (1130.0, 0.6, 0.7),
        (2280.0, 0.4, 0.5),
        (3870.0, 0.25, 0.35),
    ]
    x = np.zeros(n)
    for f, a, t60 in modes:
        x += a * np.sin(2 * np.pi * f * t) * np.exp(-6.91 * t / t60)
    click = np.zeros(n)
    click[: int(0.002 * SR)] = rng.standard_normal(int(0.002 * SR))
    x += 0.5 * click
    x += 1e-4 * rng.standard_normal(n)
    return x / np.max(np.abs(x)) * 0.8


def room_tone():
    """Steady pink-ish noise with a 120 Hz hum and a faint 2.5 kHz whine."""
    dur = 4.0
    n = int(dur * SR)
    t = np.arange(n) / SR
    white = rng.standard_normal(n)
    # Simple pink-ish tilt: lowpass the white noise and mix.
    sos = signal.butter(1, 800, btype="lowpass", fs=SR, output="sos")
    noise = signal.sosfilt(sos, white) * 3 + white * 0.3
    x = noise * 0.05
    x += 0.04 * np.sin(2 * np.pi * 120 * t)
    x += 0.01 * np.sin(2 * np.pi * 2500 * t)
    return x / np.max(np.abs(x)) * 0.5


def small_room_clap():
    """A clap in a small, fairly dead room: short decays (0.45 s lows, 0.2 s highs)."""
    dur = 1.5
    n = int(dur * SR)
    t = np.arange(n) / SR
    edges = np.geomspace(60, 16000, 25)
    tail = np.zeros(n)
    for lo, hi in zip(edges[:-1], edges[1:]):
        fc = np.sqrt(lo * hi)
        t60 = np.interp(np.log2(fc), [np.log2(60), np.log2(16000)], [0.45, 0.2])
        sos = signal.butter(4, [lo, min(hi, 0.45 * SR)], btype="bandpass", fs=SR, output="sos")
        tail += signal.sosfilt(sos, rng.standard_normal(n)) * np.exp(-6.91 * t / t60)
    burst = rng.standard_normal(int(0.004 * SR)) * 4.0
    x = 0.25 * tail
    x[: len(burst)] += burst * 0.3
    x += 1e-4 * rng.standard_normal(n)
    return x / np.max(np.abs(x)) * 0.8


def metal_pipe():
    """A struck metal pipe: inharmonic, long-ringing modes, slightly in a room."""
    dur = 3.0
    n = int(dur * SR)
    t = np.arange(n) / SR
    modes = [(523.0, 1.0, 2.2), (1443.0, 0.7, 1.8), (2830.0, 0.5, 1.4),
             (4679.0, 0.35, 1.0), (6990.0, 0.2, 0.7)]
    x = np.zeros(n)
    for f, a, t60 in modes:
        x += a * np.sin(2 * np.pi * f * t + rng.uniform(0, 6.28)) * np.exp(-6.91 * t / t60)
    hit = np.zeros(n)
    hit[: int(0.003 * SR)] = rng.standard_normal(int(0.003 * SR))
    x += 0.6 * hit
    x += 1e-4 * rng.standard_normal(n)
    return x / np.max(np.abs(x)) * 0.8


def drum_oneshot():
    """Kick (falling sine) + snare-ish noise, ~0.6 s."""
    n = int(0.6 * SR)
    t = np.arange(n) / SR
    freq = 50 + 120 * np.exp(-t * 30)
    kick = np.sin(2 * np.pi * np.cumsum(freq) / SR) * np.exp(-t * 9)
    sos = signal.butter(2, [1500, 9000], btype="bandpass", fs=SR, output="sos")
    snare = signal.sosfilt(sos, rng.standard_normal(n)) * np.exp(-t * 25) * 1.5
    x = kick + snare
    return x / np.max(np.abs(x)) * 0.8


def melody():
    """Four-note synth phrase (A minor-ish), soft saw wave with an envelope."""
    notes = [220.0, 261.63, 329.63, 293.66, 220.0]  # A3 C4 E4 D4 A3
    note_s = 0.45
    out = []
    for f in notes:
        n = int(note_s * SR)
        t = np.arange(n) / SR
        # Band-limited-ish saw: sum of the first 12 harmonics.
        saw = sum(np.sin(2 * np.pi * f * k * t) / k for k in range(1, 13))
        env = np.minimum(t / 0.01, 1.0) * np.exp(-t * 3)
        env[-int(0.02 * SR):] *= np.linspace(1, 0, int(0.02 * SR))
        out.append(saw * env)
    x = np.concatenate(out)
    return x / np.max(np.abs(x)) * 0.8


# ---------------------------------------------------------------------------
# A small acoustics toolkit for the factory library: a shoebox room (image-
# source early reflections + a band-by-band decaying diffuse tail), modal
# objects, and a few exciters. Deterministic (seeded), so presets rebuild the
# same every time.
# ---------------------------------------------------------------------------

lib_rng = np.random.default_rng(2024)   # separate from rng: the old sources stay bit-identical
C_AIR = 343.0


def _t60_curve(points):
    """points: [(freq Hz, T60 s), ...] -> function of frequency (log interpolation)."""
    fs_, ts_ = zip(*points)
    lf = np.log2(fs_)
    return lambda f: float(np.interp(np.log2(f), lf, ts_))


def diffuse_tail(n, t60_at, delay_s=0.0, bands=26, tilt=0.0):
    """Noise in third-ish octave bands, each fading at its own T60. Pink like a
    real room (equal energy per octave), plus `tilt` dB per octave (+ brighter)."""
    t = np.arange(n) / SR
    edges = np.geomspace(40, 18000, bands + 1)
    tail = np.zeros(n)
    for lo, hi in zip(edges[:-1], edges[1:]):
        fc = np.sqrt(lo * hi)
        sos = signal.butter(3, [lo, min(hi, 0.45 * SR)], btype="bandpass", fs=SR, output="sos")
        band = signal.sosfilt(sos, lib_rng.standard_normal(n))
        band *= np.exp(-6.91 * np.maximum(t - delay_s, 0.0) / max(0.03, t60_at(fc)))
        weight = (fc / 1000.0) ** -0.5 * 10 ** (tilt * np.log2(fc / 1000.0) / 20.0)
        tail += band * weight
    # build-up: diffuse energy rises over the first few ms after the delay
    rise = np.clip((t - delay_s) / 0.012, 0.0, 1.0)
    return tail * rise


def early_reflections(n, dims, src, mic, absorb=0.3, order=3, hf_loss=0.15):
    """Image-source reflections in a shoebox (dims in m). Returns an impulse train."""
    out = np.zeros(n)
    lx, ly, lz = dims
    for i in range(-order, order + 1):
        for j in range(-order, order + 1):
            for k in range(-order, order + 1):
                refl = abs(i) + abs(j) + abs(k)
                if refl == 0 or refl > order:
                    continue
                img = [(i * lx + (src[0] if i % 2 == 0 else lx - src[0])),
                       (j * ly + (src[1] if j % 2 == 0 else ly - src[1])),
                       (k * lz + (src[2] if k % 2 == 0 else lz - src[2]))]
                d = np.sqrt(sum((a - b) ** 2 for a, b in zip(img, mic)))
                idx = int(d / C_AIR * SR)
                if idx < n:
                    out[idx] += (1.0 - absorb) ** refl / max(d, 0.3)
    # walls soak up some highs on every bounce: soften the train a little
    sos = signal.butter(1, 9000 * (1.0 - hf_loss), btype="lowpass", fs=SR, output="sos")
    return signal.sosfilt(sos, out)


def clap(n):
    x = np.zeros(n)
    m = int(0.004 * SR)
    burst = lib_rng.standard_normal(m) * np.exp(-np.arange(m) / (0.0012 * SR))
    sos = signal.butter(2, [700, 9000], btype="bandpass", fs=SR, output="sos")
    x[:m] = signal.sosfilt(sos, burst) * 4.0
    return x


def thump(n, f0=90.0):
    t = np.arange(n) / SR
    return np.sin(2 * np.pi * f0 * t) * np.exp(-t * 40) * (t < 0.15)


def space(dur, dims, t60_points, er_gain=0.5, tail_gain=0.25, absorb=0.3, exciter=clap,
          src=None, mic=None, extra=None, tilt=0.0):
    """A recording of an exciter in a shoebox space."""
    n = int(dur * SR)
    src = src or [dims[0] * 0.3, dims[1] * 0.4, min(1.5, dims[2] * 0.5)]
    mic = mic or [dims[0] * 0.7, dims[1] * 0.6, min(1.6, dims[2] * 0.55)]
    ex = exciter(n)
    direct_d = np.sqrt(sum((a - b) ** 2 for a, b in zip(src, mic)))
    er = early_reflections(n, dims, src, mic, absorb=absorb) * er_gain
    first = direct_d / C_AIR
    tail = diffuse_tail(n, _t60_curve(t60_points), delay_s=first + 0.004, tilt=tilt) * tail_gain
    tail /= max(1e-9, np.sqrt(np.mean(tail[: int(0.2 * SR)] ** 2)) * 8)   # pink weighting changes the scale
    ir = er + tail
    ir[0] += 1.0 / max(direct_d, 0.3)
    if extra is not None:
        ir += extra(n)
    x = signal.fftconvolve(ex, ir)[:n]
    x += 1e-4 * lib_rng.standard_normal(n)
    return x / np.max(np.abs(x)) * 0.8


def modal(dur, modes, hit=0.4, hit_ms=2.0, room=None):
    """Decaying resonant modes: [(freq, amp, T60), ...]; optional small room around it."""
    n = int(dur * SR)
    t = np.arange(n) / SR
    x = np.zeros(n)
    for f, a, t60 in modes:
        x += a * np.sin(2 * np.pi * f * t + lib_rng.uniform(0, 6.28)) * np.exp(-6.91 * t / t60)
    m = int(hit_ms * 0.001 * SR)
    x[:m] += hit * lib_rng.standard_normal(m)
    if room is not None:
        x = x + 0.25 * signal.fftconvolve(x, diffuse_tail(int(0.8 * SR), _t60_curve(room)))[:n]
    x += 1e-4 * lib_rng.standard_normal(n)
    return x / np.max(np.abs(x)) * 0.8


def flutter(period_s, decay_s, gain=0.5):
    """Slap echoes between two parallel walls."""
    def f(n):
        out = np.zeros(n)
        k = 1
        while True:
            idx = int(k * period_s * SR)
            if idx >= n:
                break
            out[idx] += gain * np.exp(-6.91 * k * period_s / decay_s)
            k += 1
        sos = signal.butter(1, 5000, btype="lowpass", fs=SR, output="sos")
        return signal.sosfilt(sos, out)
    return f


# --- spaces ---------------------------------------------------------------
def stairwell():
    """Concrete stairwell: tall, hard, a flutter between the walls."""
    return space(4.0, [3.0, 3.2, 14.0], [(60, 2.8), (500, 2.4), (4000, 1.5), (16000, 0.7)],
                 er_gain=0.6, tail_gain=0.3, absorb=0.12, extra=flutter(0.0175, 1.2, 0.35), tilt=0.5)


def tiled_bathroom():
    """Small, hard and bright: tiles everywhere."""
    return space(2.5, [2.2, 2.6, 2.4], [(60, 0.9), (500, 1.2), (4000, 1.1), (16000, 0.6)],
                 er_gain=0.8, tail_gain=0.35, absorb=0.06, tilt=1.5)


def concrete_garage():
    """Wide, low, concrete: long and a little metallic."""
    return space(4.5, [30.0, 16.0, 2.6], [(60, 3.2), (500, 2.6), (4000, 1.8), (16000, 0.8)],
                 er_gain=0.5, tail_gain=0.28, absorb=0.1, extra=flutter(0.0152, 0.9, 0.3), tilt=0.0)


def cathedral():
    """Stone nave: huge, slow, dark at the top."""
    return space(9.0, [26.0, 64.0, 30.0], [(60, 7.0), (500, 5.5), (4000, 3.0), (16000, 1.2)],
                 er_gain=0.25, tail_gain=0.3, absorb=0.08, tilt=-7.0)


def wooden_closet():
    """A small cupboard of wood and clothes: warm and short."""
    return space(1.2, [1.0, 1.3, 2.4], [(60, 0.35), (500, 0.25), (4000, 0.12), (16000, 0.05)],
                 er_gain=0.7, tail_gain=0.2, absorb=0.45, tilt=-6.5)


def car_interior():
    """Inside a car: tiny, boxy, a low hump."""
    def boom(n):
        t = np.arange(n) / SR
        return 0.15 * np.sin(2 * np.pi * 118 * t) * np.exp(-6.91 * t / 0.25)
    return space(1.0, [1.5, 2.0, 1.2], [(60, 0.22), (500, 0.15), (4000, 0.08), (16000, 0.04)],
                 er_gain=0.8, tail_gain=0.15, absorb=0.5, extra=boom, tilt=-1.5)


def tunnel():
    """Underpass: long, with slap echoes running away from you."""
    return space(5.0, [4.0, 80.0, 4.0], [(60, 3.0), (500, 2.6), (4000, 1.6), (16000, 0.6)],
                 er_gain=0.45, tail_gain=0.25, absorb=0.1, extra=flutter(0.21, 2.2, 0.45), tilt=-0.5)


def empty_gym():
    """A school gym: big, bright, a ringing flutter between the walls."""
    return space(4.0, [30.0, 20.0, 9.0], [(60, 2.2), (500, 2.0), (4000, 1.6), (16000, 0.8)],
                 er_gain=0.4, tail_gain=0.3, absorb=0.12, extra=flutter(0.026, 1.4, 0.3), tilt=0.8)


def live_room():
    """A good studio live room: even, short, open."""
    return space(2.0, [8.0, 6.0, 3.6], [(60, 0.75), (500, 0.6), (4000, 0.5), (16000, 0.3)],
                 er_gain=0.45, tail_gain=0.22, absorb=0.3, tilt=-0.5)


def vocal_booth():
    """A treated booth: almost nothing, a little wood."""
    return space(0.8, [1.8, 1.8, 2.2], [(60, 0.14), (500, 0.1), (4000, 0.06), (16000, 0.03)],
                 er_gain=0.35, tail_gain=0.08, absorb=0.7, tilt=-1.5)


def init_room():
    """A plain, short room: the neutral starting point."""
    return space(1.8, [5.0, 4.0, 3.0], [(60, 0.55), (500, 0.5), (4000, 0.38), (16000, 0.2)],
                 er_gain=0.4, tail_gain=0.22, absorb=0.3, tilt=0.0)


# --- objects --------------------------------------------------------------
def singing_bowl():
    """A struck singing bowl: pairs of close modes that beat slowly."""
    return modal(8.0, [(220.0, 1.0, 9.0), (221.4, 0.8, 9.0), (597.0, 0.6, 7.0), (599.1, 0.5, 7.0),
                       (1112.0, 0.35, 5.0), (1731.0, 0.2, 3.5), (2480.0, 0.1, 2.5)], hit=0.15, hit_ms=3.0)


def cardboard_box():
    """A big cardboard box, knocked: papery thud, quick."""
    return modal(0.8, [(178.0, 1.0, 0.12), (342.0, 0.7, 0.09), (515.0, 0.5, 0.07), (860.0, 0.3, 0.05)],
                 hit=0.6, hit_ms=6.0, room=[(60, 0.3), (4000, 0.15)])


def piano_body():
    """A thump on a piano with the pedal down: the strings answer."""
    notes = [65.4, 98.0, 130.8, 164.8, 196.0, 261.6]
    modes = []
    for i, f in enumerate(notes):
        for h in range(1, 6):
            modes.append((f * h * (1 + 0.0004 * h * h), 0.5 / h, 5.0 / (1 + 0.4 * h) * (1.2 - 0.08 * i)))
    x = modal(6.0, modes, hit=1.2, hit_ms=8.0, room=[(60, 0.8), (4000, 0.5)])
    return x


def spring_tank():
    """A spring reverb tank, kicked: falling "boing" chirps that repeat and fade."""
    n = int(3.0 * SR)
    x = np.zeros(n)
    m = int(0.03 * SR)
    tc = np.arange(m) / SR
    # one echo: a chirp sweeping from 3.5 kHz down to 250 Hz (dispersion), short fade
    f_inst = 250 + 3250 * np.exp(-tc / 0.008)
    chirp = np.sin(2 * np.pi * np.cumsum(f_inst) / SR) * np.exp(-tc / 0.012)
    period = 0.037
    k = 0
    while int(k * period * SR) + m < n:
        at = int(k * period * SR + lib_rng.uniform(-0.002, 0.002) * SR * (k > 0))
        at = max(0, at)
        g = np.exp(-6.91 * k * period / 2.4) * (1.0 if k == 0 else 0.75)
        x[at:at + m] += g * chirp
        k += 1
    sos = signal.butter(2, [150, 5000], btype="bandpass", fs=SR, output="sos")
    x = signal.sosfilt(sos, x)
    x += 0.15 * signal.fftconvolve(x, diffuse_tail(int(1.2 * SR), _t60_curve([(100, 1.6), (4000, 0.9)])))[:n]
    x += 1e-4 * lib_rng.standard_normal(n)
    return x / np.max(np.abs(x)) * 0.8


def wine_glass():
    """A wine glass, flicked: clean, high, long."""
    return modal(4.0, [(742.0, 1.0, 3.2), (1991.0, 0.45, 2.1), (3712.0, 0.25, 1.3), (5890.0, 0.1, 0.8)],
                 hit=0.2, hit_ms=1.5)


# --- textures (steady: no hits) -------------------------------------------
def vinyl_room():
    """A record's run-out groove in a small room: crackle, hiss, rumble."""
    n = int(5.0 * SR)
    t = np.arange(n) / SR
    x = 0.01 * lib_rng.standard_normal(n)
    pops = lib_rng.random(n) < 0.0009
    x[pops] += lib_rng.standard_normal(pops.sum()) * 0.5
    x += 0.02 * np.sin(2 * np.pi * 0.55 * t) * np.sin(2 * np.pi * 33 * t)
    sos = signal.butter(2, [30, 9000], btype="bandpass", fs=SR, output="sos")
    x = signal.sosfilt(sos, x)
    x = x + 0.3 * signal.fftconvolve(x, diffuse_tail(int(0.5 * SR), _t60_curve([(60, 0.4), (4000, 0.25)])))[:n]
    return x / np.max(np.abs(x)) * 0.5


def rain_window():
    """Rain on a window, heard from inside: soft hiss and drops."""
    n = int(5.0 * SR)
    hiss = signal.sosfilt(signal.butter(2, [400, 7000], btype="bandpass", fs=SR, output="sos"),
                          lib_rng.standard_normal(n)) * 0.04
    drops = np.zeros(n)
    idx = lib_rng.integers(0, n - 400, 1600)
    for i in idx:
        f = lib_rng.uniform(1500, 5000)
        m = np.arange(300)
        drops[i:i + 300] += lib_rng.uniform(0.05, 0.25) * np.sin(2 * np.pi * f * m / SR) * np.exp(-m / 40)
    x = hiss + drops
    x = x + 0.2 * signal.fftconvolve(x, diffuse_tail(int(0.6 * SR), _t60_curve([(60, 0.5), (4000, 0.3)])))[:n]
    return x / np.max(np.abs(x)) * 0.5


LIBRARY = {
    "stairwell": stairwell, "tiled_bathroom": tiled_bathroom, "concrete_garage": concrete_garage,
    "cathedral": cathedral, "wooden_closet": wooden_closet, "car_interior": car_interior,
    "tunnel": tunnel, "empty_gym": empty_gym, "live_room": live_room, "vocal_booth": vocal_booth,
    "init_room": init_room, "singing_bowl": singing_bowl, "cardboard_box": cardboard_box,
    "piano_body": piano_body, "spring_tank": spring_tank, "wine_glass": wine_glass,
    "vinyl_room": vinyl_room, "rain_window": rain_window,
}


def main():
    os.makedirs(OUT, exist_ok=True)
    files = {
        "hall_clap.wav": hall_clap(),
        "bottle_tap.wav": bottle_tap(),
        "room_tone.wav": room_tone(),
        "small_room_clap.wav": small_room_clap(),
        "metal_pipe.wav": metal_pipe(),
        "drum_oneshot.wav": drum_oneshot(),
        "melody.wav": melody(),
    }
    for name, fn in LIBRARY.items():   # after the originals, so they stay bit-identical
        files[name + ".wav"] = fn()
    for name, x in files.items():
        path = os.path.join(OUT, name)
        save_audio(path, x, SR)
        print(f"wrote {path}  ({len(x) / SR:.2f}s)")


if __name__ == "__main__":
    main()
