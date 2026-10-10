"""
Loading and saving audio.

Everything inside the engine works on float32/float64 numpy arrays shaped
(channels, samples) at one shared sample rate. These helpers take care of
getting files into that shape and writing results back out safely.
"""

import numpy as np
import soundfile as sf
import librosa

# Every file is resampled to this rate so the room source and the target
# always line up. 48 kHz is a common production rate.
DEFAULT_SR = 48000


def load_audio(path, sr=DEFAULT_SR, mono=False):
    """
    Read any audio file soundfile/librosa understands.

    Returns (audio, sr) where audio is shaped (channels, samples).
    If mono=True the channels are averaged down to one.
    """
    audio, file_sr = sf.read(path, always_2d=True, dtype="float64")
    audio = audio.T  # soundfile gives (samples, channels); we want (channels, samples)

    if file_sr != sr:
        audio = librosa.resample(audio, orig_sr=file_sr, target_sr=sr, axis=-1)

    if mono:
        audio = audio.mean(axis=0, keepdims=True)

    return audio, sr


def save_audio(path, audio, sr=DEFAULT_SR):
    """Write a (channels, samples) array to a 24-bit WAV file."""
    audio = np.atleast_2d(audio)
    sf.write(path, audio.T, sr, subtype="PCM_24")


def peak_normalize(audio, target_peak_db=-1.0):
    """
    Scale audio down (never up past the target) so the loudest sample
    sits at target_peak_db. Prevents clipping in the rendered file.
    """
    peak = np.max(np.abs(audio))
    if peak < 1e-12:
        return audio
    target = 10 ** (target_peak_db / 20)
    if peak > target:
        audio = audio * (target / peak)
    return audio


def rms(audio):
    """Root-mean-square level (a simple loudness measure)."""
    return float(np.sqrt(np.mean(np.square(audio)) + 1e-20))
