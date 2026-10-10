"""
ANALYSIS STAGE: recording in, measurements out.

Takes the "room source" (any recording) and measures five things:

  1. Multiband split        -> chop the sound into frequency bands
  2. Per-band decay (T60)   -> how long each band takes to die away (spatial fingerprint)
  3. Averaged spectrum      -> the overall tonal shape (tonal fingerprint)
  4. Resonant peaks         -> sharp bumps in that shape (ringing frequencies)
  5. Balance score          -> 0.0 = purely tonal room, 1.0 = purely spatial room

Nothing here touches the target audio. The result is a RoomAnalysis object
that synthesis.py turns into actual processing.
"""

from dataclasses import dataclass, field, asdict

import numpy as np
import librosa
from scipy import signal

# Frequency range we care about. Below ~50 Hz decay measurements get
# unreliable (too few cycles), above ~16 kHz there's rarely useful room info.
F_MIN = 60.0
F_MAX = 16000.0

# T60 can't be shorter than this (keeps the IR from becoming a click).
MIN_T60 = 0.05

# If a band's loudest point is less than this many dB above its noise floor,
# we can't see a real decay in it (typical for steady room tone).
MIN_DYNAMIC_RANGE_DB = 15.0

# T60 used for bands where no decay could be measured.
FALLBACK_T60 = 0.4

# At least this share of bands must show a real decay, otherwise the whole
# recording is treated as a steady sound.
MIN_CONFIDENT_BAND_SHARE = 0.25


@dataclass
class Peak:
    """One resonant peak found in the averaged spectrum."""
    freq_hz: float    # center frequency
    gain_db: float    # how far it sticks out above the smoothed spectrum
    q: float          # sharpness (higher Q = narrower, more "ringy")


@dataclass
class RoomAnalysis:
    sr: int
    duration_s: float

    # --- bands (stage 1 + 2) ---
    band_edges_hz: list          # n_bands + 1 edges
    band_centers_hz: list        # n_bands centers
    band_t60_s: list             # measured decay per band
    band_confident: list         # True if the decay was actually measurable
    band_level_db: list          # relative loudness per band (0 dB = loudest band)

    # --- spectrum (stage 3 + 4) ---
    spectrum_freqs_hz: list      # log-spaced frequency grid
    spectrum_db: list            # smoothed averaged spectrum on that grid
    peaks: list                  # list of Peak

    # --- balance (stage 5) ---
    late_energy_fraction: float  # share of energy arriving > 50 ms after the hit
    balance: float               # 0 tonal ... 1 spatial
    balance_details: dict = field(default_factory=dict)

    def to_dict(self):
        return asdict(self)


# ---------------------------------------------------------------------------
# Stage 1: multiband split
# ---------------------------------------------------------------------------

def band_edges(n_bands, f_min=F_MIN, f_max=F_MAX):
    """Log-spaced band edges, so each band covers the same musical width."""
    return np.geomspace(f_min, f_max, n_bands + 1)


def band_filter(lo, hi, sr, order=4):
    """
    A Butterworth bandpass filter for one band, in 'sos' form
    (second-order sections, which stay numerically stable).
    Shared with synthesis.py so the IR uses the exact same bands.
    """
    hi = min(hi, 0.45 * sr)
    return signal.butter(order, [lo, hi], btype="bandpass", fs=sr, output="sos")


def split_bands(x, sr, edges):
    """
    Filter mono signal x into one signal per band.
    sosfiltfilt runs the filter forward and backward, so there's no time
    smearing: every band's envelope lines up with the original.
    """
    return np.stack([
        signal.sosfiltfilt(band_filter(lo, hi, sr), x)
        for lo, hi in zip(edges[:-1], edges[1:])
    ])


# ---------------------------------------------------------------------------
# Stage 2: per-band decay (T60)
# ---------------------------------------------------------------------------

def smooth_energy_db(y, sr, window_s=0.02):
    """Short moving-average energy envelope in dB (20 ms window)."""
    n = max(1, int(window_s * sr))
    energy = np.convolve(y ** 2, np.ones(n) / n, mode="same")
    return 10 * np.log10(energy + 1e-20)


def measure_t60(y, sr, max_t60):
    """
    Estimate how long this band takes to decay by 60 dB.

    Method (standard "Schroeder backward integration"):
      - start at the loudest moment
      - stop where the sound sinks into the noise floor
      - add up remaining energy from the end backwards: this gives a smooth
        decay curve even for noisy signals
      - fit a straight line to the curve between -5 dB and -25 dB and
        extend it to -60 dB

    Returns (t60_seconds, confident). confident=False means the band never
    really decayed (steady sound), so a fallback value was used.
    """
    env_db = smooth_energy_db(y, sr)
    peak_i = int(np.argmax(env_db))

    # Noise floor = typical level in the quietest 10% of the signal after the peak.
    tail = env_db[peak_i:]
    if len(tail) < int(0.05 * sr):
        return FALLBACK_T60, False
    floor_db = np.percentile(tail, 10)
    dynamic_range = env_db[peak_i] - floor_db
    if dynamic_range < MIN_DYNAMIC_RANGE_DB:
        return FALLBACK_T60, False

    # Cut the signal where it first gets within 5 dB of the floor.
    above = np.nonzero(tail <= floor_db + 5.0)[0]
    end_i = peak_i + (above[0] if len(above) else len(tail))
    seg = y[peak_i:end_i]
    if len(seg) < int(0.01 * sr):
        return MIN_T60, True

    # Schroeder curve: energy remaining from each moment until the end.
    edc = np.cumsum(seg[::-1] ** 2)[::-1]
    edc_db = 10 * np.log10(edc / edc[0] + 1e-20)
    t = np.arange(len(seg)) / sr

    # Fit between -5 and -25 dB if the curve gets that low, otherwise use
    # whatever range there is (from -1 dB down to 2/3 of the way).
    lo_db = -25.0 if edc_db.min() < -25.0 else max(edc_db.min() * 0.66, -25.0)
    mask = (edc_db <= -5.0 if lo_db < -10 else edc_db <= -1.0) & (edc_db >= lo_db)
    if mask.sum() < 4:
        return MIN_T60, True

    slope, _ = np.polyfit(t[mask], edc_db[mask], 1)  # dB per second (negative)
    if slope >= 0:
        return max_t60, True
    t60 = -60.0 / slope
    return float(np.clip(t60, MIN_T60, max_t60)), True


# ---------------------------------------------------------------------------
# Stage 3: averaged spectral shape
# ---------------------------------------------------------------------------

def averaged_spectrum(x, sr, n_fft=4096, active_range_db=40.0):
    """
    Time-averaged magnitude spectrum: the "average color" of the recording.

    Averaging is done in dB across every frame that is within
    active_range_db of the loudest frame (silence is ignored). Averaging in
    dB, rather than averaging raw power, gives every active frame an equal
    vote. Random noise wiggles change from frame to frame and average out;
    real resonances sit in the same place every frame and survive. With a
    decaying sound (a clap) a power average would be dominated by the first
    few frames and keep their random wiggles, which look like fake peaks.
    """
    S = np.abs(librosa.stft(x, n_fft=n_fft, hop_length=n_fft // 4)) ** 2
    S_db = 10 * np.log10(S + 1e-20)
    frame_db = 10 * np.log10(S.sum(axis=0) + 1e-20)
    active = frame_db >= frame_db.max() - active_range_db
    freqs = librosa.fft_frequencies(sr=sr, n_fft=n_fft)
    return freqs, S_db[:, active].mean(axis=1)


def to_log_grid(freqs, spec_db, points_per_octave=48, f_min=F_MIN, f_max=F_MAX):
    """
    Resample the spectrum onto a log-frequency grid, so "one octave" is the
    same number of points everywhere. Makes smoothing and peak widths musical.
    """
    n_oct = np.log2(f_max / f_min)
    grid = f_min * 2 ** (np.arange(int(n_oct * points_per_octave) + 1) / points_per_octave)
    return grid, np.interp(grid, freqs, spec_db)


def smooth_octaves(spec_db, octaves, points_per_octave=48):
    """Moving average across a given fraction of an octave on the log grid."""
    n = max(1, int(round(octaves * points_per_octave)))
    if n <= 1:
        return spec_db.copy()
    padded = np.pad(spec_db, n, mode="edge")
    kernel = np.hanning(n + 2)[1:-1]
    kernel /= kernel.sum()
    return np.convolve(padded, kernel, mode="same")[n:-n]


# ---------------------------------------------------------------------------
# Stage 4: resonant peak detection
# ---------------------------------------------------------------------------

def find_resonant_peaks(spec_db, grid, n_peaks, points_per_octave=48, min_prominence_db=4.0):
    """
    Find sharp bumps in the spectrum.

    Trick: subtract a heavily smoothed (1 octave) version of the spectrum
    from a lightly smoothed (1/24 octave) one. Broad tilts and slopes cancel,
    leaving only the narrow resonances sticking up.
    """
    fine = smooth_octaves(spec_db, 1 / 24, points_per_octave)
    baseline = smooth_octaves(spec_db, 1.0, points_per_octave)
    residual = fine - baseline

    idx, props = signal.find_peaks(
        residual,
        prominence=min_prominence_db,
        distance=points_per_octave // 6,  # peaks at least 1/6 octave apart
    )
    if len(idx) == 0:
        return [], residual

    # Width at half the prominence, measured in grid points -> octaves.
    widths, _, _, _ = signal.peak_widths(residual, idx, rel_height=0.5,
                                         prominence_data=(props["prominences"],
                                                          props["left_bases"],
                                                          props["right_bases"]))
    bw_oct = np.maximum(widths / points_per_octave, 1 / 48)

    # Keep the n most prominent.
    order = np.argsort(props["prominences"])[::-1][:n_peaks]
    peaks = []
    for k in sorted(order, key=lambda k: grid[idx[k]]):
        # Convert bandwidth in octaves to filter Q.
        two_n = 2 ** bw_oct[k]
        q = np.sqrt(two_n) / (two_n - 1)
        peaks.append(Peak(
            freq_hz=float(grid[idx[k]]),
            gain_db=float(props["prominences"][k]),
            q=float(np.clip(q, 0.5, 40.0)),
        ))
    return peaks, residual


# ---------------------------------------------------------------------------
# Stage 5: spatial-versus-tonal balance
# ---------------------------------------------------------------------------

def late_energy_fraction(x, sr, early_s=0.05):
    """Share of energy arriving more than early_s after the loudest moment."""
    env_db = smooth_energy_db(x, sr)
    peak_i = int(np.argmax(env_db))
    e = x[peak_i:] ** 2
    split = int(early_s * sr)
    total = e.sum() + 1e-20
    return float(e[split:].sum() / total)


def balance_score(t60s, confident, peaks, late_frac):
    """
    PROVISIONAL FORMULA. Tweak freely; this is the main thing to tune by ear.

    Spatial evidence goes up when:
      - bands decay slowly (long median T60)
      - all bands decay similarly (a real room rings everywhere; a bottle
        only rings at a few frequencies)
      - a lot of energy arrives late
    Tonal evidence goes up when:
      - spectral peaks are tall (prominent)
      - spectral peaks are narrow (high Q)

    Result = spatial / (spatial + tonal), from 0 (tonal) to 1 (spatial).
    """
    t60s = np.asarray(t60s)
    confident = np.asarray(confident, dtype=bool)

    if confident.any():
        good = t60s[confident]
        median_t60 = float(np.median(good))
        # How much the decay varies across bands (spread in log terms).
        spread = float(np.std(np.log(good)))
        magnitude = np.clip(median_t60 / 1.2, 0.0, 1.5)
        uniformity = 1.0 - 0.6 * np.clip(spread / 1.0, 0.0, 1.0)
        spatial = 0.7 * magnitude * uniformity + 0.3 * late_frac
    else:
        # Steady sound (room tone): no decays to measure. Stay neutral-ish.
        median_t60, spread, magnitude, uniformity = FALLBACK_T60, 0.0, 0.0, 0.0
        spatial = 0.3

    if peaks:
        mean_gain = float(np.mean([p.gain_db for p in peaks]))
        mean_q = float(np.mean([p.q for p in peaks]))
        prominence = np.clip(mean_gain / 12.0, 0.0, 1.5)
        sharpness = np.clip(mean_q / 8.0, 0.2, 1.0)
        tonal = prominence * sharpness
    else:
        mean_gain, mean_q, tonal = 0.0, 0.0, 0.0

    if spatial + tonal < 1e-6:
        ratio = 0.5
    else:
        ratio = spatial / (spatial + tonal)

    details = {
        "spatial_evidence": float(spatial),
        "tonal_evidence": float(tonal),
        "median_t60_s": median_t60,
        "t60_spread_log": spread,
        "mean_peak_gain_db": mean_gain,
        "mean_peak_q": mean_q,
    }
    return float(np.clip(ratio, 0.0, 1.0)), details


# ---------------------------------------------------------------------------
# Main entry
# ---------------------------------------------------------------------------

def analyze_room(audio, sr, n_bands=16, n_peaks=6, max_t60=2.5):
    """
    Run all five analysis stages on a room source recording.

    audio: (channels, samples) or (samples,) array. Analysis is done in mono.
    max_t60: longest decay we'll report (normally the IR tail length).
    """
    x = np.atleast_2d(audio).mean(axis=0)

    # 1. multiband split
    edges = band_edges(n_bands)
    centers = np.sqrt(edges[:-1] * edges[1:])
    bands = split_bands(x, sr, edges)

    # 2. per-band decay + per-band level
    t60s, conf = zip(*(measure_t60(b, sr, max_t60) for b in bands))
    t60s, conf = list(t60s), list(conf)
    # If only a few stray bands show a decay, the recording is really a
    # steady sound (room tone): treat every band as unmeasurable.
    if sum(conf) < MIN_CONFIDENT_BAND_SHARE * n_bands:
        t60s = [FALLBACK_T60] * n_bands
        conf = [False] * n_bands
    energies = np.array([np.sum(b ** 2) for b in bands]) + 1e-20
    level_db = 10 * np.log10(energies / energies.max())

    # 3. averaged spectrum on a log grid, lightly smoothed (1/6 octave)
    freqs, spec = averaged_spectrum(x, sr)
    grid, spec_log = to_log_grid(freqs, spec)
    spec_smooth = smooth_octaves(spec_log, 1 / 6)

    # 4. resonant peaks
    peaks, _ = find_resonant_peaks(spec_log, grid, n_peaks)

    # 5. balance
    late = late_energy_fraction(x, sr)
    ratio, details = balance_score(t60s, conf, peaks, late)

    return RoomAnalysis(
        sr=sr,
        duration_s=len(x) / sr,
        band_edges_hz=edges.tolist(),
        band_centers_hz=centers.tolist(),
        band_t60_s=[float(t) for t in t60s],
        band_confident=[bool(c) for c in conf],
        band_level_db=level_db.tolist(),
        spectrum_freqs_hz=grid.tolist(),
        spectrum_db=(spec_smooth - spec_smooth.max()).tolist(),
        peaks=peaks,
        late_energy_fraction=late,
        balance=ratio,
        balance_details=details,
    )
