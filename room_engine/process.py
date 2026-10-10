"""
PROCESSING CHAIN: apply a Room to new audio.

    input audio
      -> resonant filter bank            (tonal path)
      -> convolve with synthetic IR      (spatial path)
      -> blend tonal and spatial output using the balance ratio
      -> wet/dry mix against the original dry signal
      -> output

Offline only: the whole file is processed at once.
"""

import numpy as np
from scipy import signal

from .audio_io import rms, peak_normalize


def apply_filter_bank(x, filters):
    """
    Tonal path. The dry signal passes straight through, and each resonant
    filter adds a narrow boost on top:

        out = x + sum_k (gain_k - 1) * resonator_k(x)

    At each peak's center this gives exactly the measured boost; away from
    the peaks the sound is left alone. Filters run in parallel, so they
    don't stack into each other.
    """
    out = x.copy()
    for f in filters:
        extra = 10 ** (f.gain_db / 20) - 1.0
        out += extra * signal.lfilter(f.b, f.a, x, axis=-1)
    return out


def convolve_ir(x, ir):
    """
    Spatial path: convolve with the synthetic IR.
    Mono input is spread to the IR's stereo channels; stereo input is
    convolved channel by channel. Output length = input + tail, so the
    reverb rings out fully.
    """
    if x.shape[0] == 1 and ir.shape[0] > 1:
        x = np.repeat(x, ir.shape[0], axis=0)
    if ir.shape[0] == 1 and x.shape[0] > 1:
        ir = np.repeat(ir, x.shape[0], axis=0)
    return np.stack([signal.fftconvolve(x[c], ir[c]) for c in range(x.shape[0])])


def match_channels(a, n_ch):
    return np.repeat(a, n_ch, axis=0) if a.shape[0] == 1 and n_ch > 1 else a


def pad_to(a, n):
    return np.pad(a, ((0, 0), (0, n - a.shape[1])))


def apply_room(x, room, mix=0.5, normalize=True):
    """
    Run the full chain on x (channels, samples). Returns (channels, samples)
    with the IR tail appended to the end.

    mix: 0.0 = dry only, 1.0 = fully processed.
    The tonal and spatial paths are each loudness-matched to the dry input
    before blending, so moving the balance or mix changes character, not volume.
    """
    x = np.atleast_2d(x)
    dry_level = rms(x)

    # 1. tonal path
    tonal = apply_filter_bank(x, room.filters)
    tonal *= dry_level / rms(tonal)

    # 2. spatial path (fed by the tonal path, as in the brief's chain)
    spatial = convolve_ir(tonal, room.ir)
    spatial *= dry_level / rms(spatial[:, : x.shape[1]])

    # Line everything up: same channel count, same length (input + tail).
    n_ch = spatial.shape[0]
    n = spatial.shape[1]
    dry = pad_to(match_channels(x, n_ch), n)
    tonal = pad_to(match_channels(tonal, n_ch), n)

    # 3. blend by balance: 0 = all tonal, 1 = all spatial
    r = room.balance
    blended = (1.0 - r) * tonal + r * spatial

    # 4. wet/dry
    out = (1.0 - mix) * dry + mix * blended

    # Trim trailing near-silence so files don't end in seconds of nothing.
    out = trim_tail(out, keep=x.shape[1])

    return peak_normalize(out) if normalize else out


def trim_tail(audio, keep, threshold_db=-80.0):
    """Cut the end of the file once it's quieter than threshold_db (never before `keep`)."""
    peak = np.max(np.abs(audio)) + 1e-20
    loud = np.nonzero(np.max(np.abs(audio), axis=0) > peak * 10 ** (threshold_db / 20))[0]
    end = max(keep, (loud[-1] + 1) if len(loud) else keep)
    return audio[:, :end]
