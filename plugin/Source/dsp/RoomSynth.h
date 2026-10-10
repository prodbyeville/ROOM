#pragma once

#include "RoomAnalysis.h"
#include "Biquad.h"

// SYNTHESIS STAGE (C++ port of room_engine/synthesis.py).
//
// Turns a RoomAnalysis into processing tools, at the host's sample rate:
//   - a synthetic stereo impulse response (spatial path)
//   - a tone-match EQ + resonant filter bank (tonal path)
//
// INTENSITY (0..2) scales EQ depth, resonance gains and decay lengths together.

namespace room
{

/** Everything the real-time tonal path needs. Fixed-size so it can be copied
    to the audio thread without allocating. */
struct ToneSettings
{
    static constexpr int maxEq = 9;
    static constexpr int maxResonators = 12;

    int numEq = 0;
    BiquadCoeffs eq[maxEq];

    int numResonators = 0;
    BiquadCoeffs resonator[maxResonators];
    float resonatorExtra[maxResonators] {}; // linear gain added on top of the dry signal

    float tonalGain = 1.0f;                 // keeps the tonal path at the input's loudness
};

/** Synthetic stereo IR: per band, filtered noise decaying at that band's
    T60 (times intensity). Unit energy per channel. */
juce::AudioBuffer<float> synthesizeIR (const RoomAnalysis&, double sampleRate,
                                       float tailSeconds, float intensity, int seed = 0);

/** Tone-match EQ (room tone curve vs a neutral reference) + resonators. */
ToneSettings designTone (const RoomAnalysis&, double sampleRate, float intensity);

/** The EQ curve (dB) the tone path aims for, on the analysis' tone grid.
    Exposed so the UI and tests can show it. */
std::vector<float> targetEqCurveDb (const RoomAnalysis&, float intensity);

} // namespace room
