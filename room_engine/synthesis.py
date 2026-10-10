"""
SYNTHESIS STAGE: measurements in, processing tools out.

Turns a RoomAnalysis into two things:

  1. A synthetic impulse response (the SPATIAL path).
     Built from scratch out of filtered noise, one band at a time, each band
     fading out at the speed measured for that band. It has the room's
     decay character but none of the original recording's actual content.

  2. A resonant filter bank (the TONAL path).
     One narrow filter per detected spectral peak, tuned to that peak's
     frequency, gain and width.

The original recording is never used here. Only the numbers from analysis.
"""

from dataclasses import dataclass

import numpy as np
from scipy import signal

from .analysis import RoomAnalysis, band_filter

# Silence before the tail starts, like the gap before the first reflection.
PRE_DELAY_S = 0.010

# Very short fade-in so the IR doesn't start with a click.
FADE_IN_S = 0.003

# How strongly the source's per-band loudness colors the IR.
# 1.0 = copy it exactly, 0.0 = flat. Kept partial because the tonal path
# already carries the sharp tone color.
BAND_LEVEL_AMOUNT = 0.5

# Never let a band drop more than this far below the loudest one in the IR.
BAND_LEVEL_FLOOR_DB = -24.0

# Cap on how hard any one resonance is boosted. Very ringy sources (a glass,
# a bell) can measure 40+ dB peaks, which would turn the target into pure
# sine tones. 18 dB keeps it strongly colored but still recognizable.
MAX_RESONANCE_GAIN_DB = 18.0


@dataclass
class ResonantFilter:
    freq_hz: float
    q: float
    gain_db: float
    b: np.ndarray   # filter coefficients (numerator)
    a: np.ndarray   # filter coefficients (denominator)


@dataclass
class Room:
    """Everything needed to process audio. Derived entirely from analysis."""
    sr: int
    ir: np.ndarray            # (2, samples) stereo impulse response
    filters: list             # list of ResonantFilter
    balance: float            # 0 tonal ... 1 spatial
    analysis: RoomAnalysis


def synthesize_ir(analysis, tail_s=2.5, seed=0, channels=2):
    """
    Build the synthetic impulse response.

    For each band:
      white noise -> band filter -> multiply by an exponential fade that
      drops 60 dB in exactly that band's measured T60 -> scale by the
      band's measured loudness.
    Sum all bands. Each stereo channel uses different random noise so the
    tail sounds wide instead of mono-in-the-middle.
    """
    sr = analysis.sr
    n = int(tail_s * sr)
    t = np.arange(n) / sr
    edges = analysis.band_edges_hz
    rng = np.random.default_rng(seed)

    # Per-band gains from the source's band levels (softened, floored).
    levels_db = np.maximum(np.array(analysis.band_level_db) * BAND_LEVEL_AMOUNT,
                           BAND_LEVEL_FLOOR_DB)
    gains = 10 ** (levels_db / 20)

    ir = np.zeros((channels, n))
    for ch in range(channels):
        for k, (lo, hi) in enumerate(zip(edges[:-1], edges[1:])):
            noise = rng.standard_normal(n)
            band_noise = signal.sosfilt(band_filter(lo, hi, sr), noise)
            # Normalize each band to unit loudness before applying its gain,
            # so narrow low bands aren't quieter just because they're narrow.
            band_noise /= np.sqrt(np.mean(band_noise ** 2)) + 1e-20
            # exp(-6.91 * t / T60) falls by 60 dB (a factor of 1000) at t = T60.
            decay = np.exp(-6.91 * t / analysis.band_t60_s[k])
            ir[ch] += gains[k] * band_noise * decay

    # Shape the start: fade in, then shift by the pre-delay.
    fade = int(FADE_IN_S * sr)
    ir[:, :fade] *= np.linspace(0, 1, fade)
    pre = int(PRE_DELAY_S * sr)
    ir = np.concatenate([np.zeros((channels, pre)), ir[:, : n - pre]], axis=1)

    # Unit energy per channel: the wet signal comes out at a predictable level.
    ir /= np.sqrt(np.sum(ir ** 2, axis=1, keepdims=True)) + 1e-20
    return ir


def build_filter_bank(analysis):
    """
    One resonant bandpass filter per detected peak.
    scipy's iirpeak gives a 2nd-order resonator with 0 dB gain at its
    center; the measured gain is applied when the branches are mixed
    (see process.apply_filter_bank).
    """
    filters = []
    for p in analysis.peaks:
        if p.freq_hz >= 0.45 * analysis.sr:
            continue
        b, a = signal.iirpeak(p.freq_hz, p.q, fs=analysis.sr)
        gain = min(p.gain_db, MAX_RESONANCE_GAIN_DB)
        filters.append(ResonantFilter(p.freq_hz, p.q, gain, b, a))
    return filters


def synthesize_room(analysis, tail_s=2.5, balance_override=None, seed=0):
    """Analysis -> Room. balance_override (0..1) replaces the detected balance."""
    balance = analysis.balance if balance_override is None else float(balance_override)
    return Room(
        sr=analysis.sr,
        ir=synthesize_ir(analysis, tail_s=tail_s, seed=seed),
        filters=build_filter_bank(analysis),
        balance=float(np.clip(balance, 0.0, 1.0)),
        analysis=analysis,
    )
