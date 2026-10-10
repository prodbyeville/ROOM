"""
SYNTHESIS STAGE: measurements in, processing tools out.

Turns a RoomAnalysis into two things:

  1. A synthetic impulse response (the SPATIAL path).
     Built from scratch out of filtered noise, one band at a time, each band
     fading out at the speed measured for that band. It has the room's
     decay character but none of the original recording's actual content.

  2. A tone curve + resonant filter bank (the TONAL path).
     The tone curve is the source's broad EQ shape (bright, dark, boxy...),
     matched onto the target at processing time. The filter bank is one
     narrow filter per detected spectral peak, tuned to that peak's
     frequency, gain and width.

Everything is scaled by INTENSITY (0 = no room character, 1 = as measured,
2 = exaggerated): EQ depth, resonance gains and decay lengths together.

The original recording is never used here. Only the numbers from analysis.
"""

from dataclasses import dataclass

import numpy as np
from scipy import signal

from .analysis import RoomAnalysis, band_filter, smooth_octaves

# Silence before the tail starts, like the gap before the first reflection.
PRE_DELAY_S = 0.010

# Very short fade-in so the IR doesn't start with a click.
FADE_IN_S = 0.003

# Short fade at the very end so a long decay cut off by the tail length
# doesn't end in a click.
FADE_OUT_S = 0.05

# Cap on how hard any one resonance is boosted. Very ringy sources (a glass,
# a bell) can measure 40+ dB peaks, which would turn the target into pure
# sine tones. 18 dB keeps it strongly colored but still recognizable.
MAX_RESONANCE_GAIN_DB = 18.0

# How much of the source's tone curve is matched onto the target at
# intensity 1.0. 1.0 would copy it fully; partial keeps the target's own
# identity. Tune by ear.
EQ_MATCH_DEPTH = 0.6

# Max boost/cut of the tone-match EQ at any frequency (before intensity).
EQ_MAX_DB = 12.0

# Range of the intensity knob.
MAX_INTENSITY = 2.0


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
    tone_freqs_hz: np.ndarray # log-spaced frequency grid for the tone curve
    tone_curve_db: np.ndarray # source's broad EQ shape (1-octave smoothed)
    eq_depth: float           # how much of the tone curve to match (incl. intensity)
    balance: float            # 0 tonal ... 1 spatial
    intensity: float
    analysis: RoomAnalysis


def synthesize_ir(analysis, tail_s=2.5, seed=0, channels=2, intensity=1.0):
    """
    Build the synthetic impulse response.

    For each band:
      white noise -> band filter -> multiply by an exponential fade that
      drops 60 dB in exactly that band's measured T60 (times intensity).
    Sum all bands. Each stereo channel uses different random noise so the
    tail sounds wide instead of mono-in-the-middle.

    The IR itself is tonally flat: the room's EQ color is applied by the
    tone-match EQ earlier in the chain, and the IR is fed by that, so the
    tail inherits it without coloring twice.
    """
    sr = analysis.sr
    n = int(tail_s * sr)
    t = np.arange(n) / sr
    edges = analysis.band_edges_hz
    rng = np.random.default_rng(seed)

    # Intensity stretches or shrinks every band's decay together.
    # Intensity 0 leaves only a tiny 50 ms burst (practically no room).
    t60s = np.maximum(np.array(analysis.band_t60_s) * intensity, 0.05)

    ir = np.zeros((channels, n))
    for ch in range(channels):
        for k, (lo, hi) in enumerate(zip(edges[:-1], edges[1:])):
            noise = rng.standard_normal(n)
            band_noise = signal.sosfilt(band_filter(lo, hi, sr), noise)
            # Normalize each band to unit loudness, so narrow low bands
            # aren't quieter just because they're narrow.
            band_noise /= np.sqrt(np.mean(band_noise ** 2)) + 1e-20
            # exp(-6.91 * t / T60) falls by 60 dB (a factor of 1000) at t = T60.
            decay = np.exp(-6.91 * t / t60s[k])
            ir[ch] += band_noise * decay

    # Shape the start: fade in, then shift by the pre-delay.
    fade = int(FADE_IN_S * sr)
    ir[:, :fade] *= np.linspace(0, 1, fade)
    pre = int(PRE_DELAY_S * sr)
    ir = np.concatenate([np.zeros((channels, pre)), ir[:, : n - pre]], axis=1)
    fade_out = int(FADE_OUT_S * sr)
    ir[:, -fade_out:] *= np.linspace(1, 0, fade_out)

    # Unit energy per channel: the wet signal comes out at a predictable level.
    ir /= np.sqrt(np.sum(ir ** 2, axis=1, keepdims=True)) + 1e-20
    return ir


def build_filter_bank(analysis, intensity=1.0):
    """
    One resonant bandpass filter per detected peak.
    scipy's iirpeak gives a 2nd-order resonator with 0 dB gain at its
    center; the measured gain (capped, times intensity) is applied when
    the branches are mixed (see process.apply_filter_bank).
    """
    filters = []
    for p in analysis.peaks:
        if p.freq_hz >= 0.45 * analysis.sr:
            continue
        b, a = signal.iirpeak(p.freq_hz, p.q, fs=analysis.sr)
        gain = min(p.gain_db, MAX_RESONANCE_GAIN_DB) * intensity
        filters.append(ResonantFilter(p.freq_hz, p.q, gain, b, a))
    return filters


def tone_curve(analysis):
    """
    The source's broad EQ shape: its averaged spectrum smoothed over a full
    octave, so only the overall tilt and bumps remain (dark, bright, boxy,
    thin...). Sharp resonances are handled separately by the filter bank.
    """
    freqs = np.array(analysis.spectrum_freqs_hz)
    curve = smooth_octaves(np.array(analysis.spectrum_db), 1.0)
    return freqs, curve


def synthesize_room(analysis, tail_s=2.5, balance_override=None, seed=0, intensity=1.0):
    """
    Analysis -> Room.
    balance_override (0..1) replaces the detected balance.
    intensity (0..2) scales how strongly the room's character is applied.
    """
    intensity = float(np.clip(intensity, 0.0, MAX_INTENSITY))
    balance = analysis.balance if balance_override is None else float(balance_override)
    freqs, curve = tone_curve(analysis)
    return Room(
        sr=analysis.sr,
        ir=synthesize_ir(analysis, tail_s=tail_s, seed=seed, intensity=intensity),
        filters=build_filter_bank(analysis, intensity=intensity),
        tone_freqs_hz=freqs,
        tone_curve_db=curve,
        eq_depth=EQ_MATCH_DEPTH * intensity,
        balance=float(np.clip(balance, 0.0, 1.0)),
        intensity=intensity,
        analysis=analysis,
    )
