#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

// ANALYSIS STAGE (C++ port of room_engine/analysis.py).
//
// Recording in, measurements out:
//   1. multiband split      -> log-spaced bands
//   2. per-band decay       -> T60 per band (spatial fingerprint)
//   3. averaged spectrum    -> broad tone curve (tonal fingerprint)
//   4. resonant peaks       -> sharp ringing frequencies
//   5. balance score        -> 0 = tonal ... 1 = spatial
//
// The result is only numbers. The plugin saves these numbers with the
// session, never the audio file itself.

namespace room
{

struct Peak
{
    float freqHz = 0.0f;
    float gainDb = 0.0f;  // how far it sticks out above the smoothed spectrum
    float q      = 1.0f;  // sharpness
};

struct RoomAnalysis
{
    bool valid = false;
    juce::String sourceName;
    juce::String origin;               // "File", "Recorded" or "Factory" (empty = unknown)
    double durationS = 0.0;

    std::vector<float> bandEdgesHz;    // numBands + 1
    std::vector<float> bandCentersHz;  // numBands
    std::vector<float> bandT60s;       // measured decay per band
    std::vector<int>   bandConfident;  // 1 = decay was measurable

    std::vector<float> toneFreqsHz;    // log-spaced grid
    std::vector<float> toneCurveDb;    // 1-octave smoothed spectrum (broad EQ shape)

    std::vector<Peak> peaks;

    float lateEnergyFraction = 0.0f;
    bool  steady = false;              // hiss / room tone with no hits
    float balance = 0.5f;              // 0 tonal ... 1 spatial
    float spatialEvidence = 0.0f;
    float tonalEvidence = 0.0f;
    float medianT60 = 0.0f;

    int numBands() const { return (int) bandT60s.size(); }

    juce::ValueTree toValueTree() const;
    static RoomAnalysis fromValueTree (const juce::ValueTree&);
};

/** Settings exposed by the Python prototype; fixed in the plugin for now. */
struct AnalysisSettings
{
    int numBands = 16;
    int numPeaks = 6;
    float maxT60 = 6.0f;   // longest decay reported (matches the max Tail setting)
};

/** Run all five analysis stages. audio can be mono or multichannel at any
    sample rate; it is mixed to mono and resampled to 48 kHz internally. */
RoomAnalysis analyzeRoom (const juce::AudioBuffer<float>& audio, double sampleRate,
                          const AnalysisSettings& settings = {});

} // namespace room
