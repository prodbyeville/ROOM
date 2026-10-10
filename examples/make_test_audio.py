#!/usr/bin/env python3
"""
Generate synthetic test files so the prototype can run with zero setup.

Writes to examples/audio/:

  ROOM SOURCES (things to derive a room from)
    hall_clap.wav     a clap in a big diffuse space (should score SPATIAL, near 1)
    bottle_tap.wav    a tap on a resonant glass object, dry (should score TONAL, near 0)
    room_tone.wav     steady background hiss + hum, no transients (tests the fallback)

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


def main():
    os.makedirs(OUT, exist_ok=True)
    files = {
        "hall_clap.wav": hall_clap(),
        "bottle_tap.wav": bottle_tap(),
        "room_tone.wav": room_tone(),
        "drum_oneshot.wav": drum_oneshot(),
        "melody.wav": melody(),
    }
    for name, x in files.items():
        path = os.path.join(OUT, name)
        save_audio(path, x, SR)
        print(f"wrote {path}  ({len(x) / SR:.2f}s)")


if __name__ == "__main__":
    main()
