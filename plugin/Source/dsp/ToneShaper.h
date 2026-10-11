#pragma once

#include "Biquad.h"
#include <juce_audio_basics/juce_audio_basics.h>

// TAPE + PARAMETRIC EQ (post-room tone shaping)
//
//   Tape: a low cut and a high cut that slide toward each other as the knob
//         goes up (0 = off, 1 = only ~80 Hz .. ~9 kHz left).
//   EQ:   5 bands (low shelf, three peaks, high shelf) for small cuts/boosts.
//
// The coefficient design functions are shared with the UI so the drawn curve
// is exactly what is heard.

namespace room
{

struct EqBand
{
    enum class Type { lowShelf, peak, highShelf };
    Type type = Type::peak;
    float freqHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 1.0f;
};

namespace tone
{
    constexpr int numEqBands = 5;

    // Defaults for the 5 bands (frequency, Q). Gains start at 0 dB.
    constexpr EqBand::Type kBandTypes[numEqBands] = { EqBand::Type::lowShelf, EqBand::Type::peak,
                                                      EqBand::Type::peak, EqBand::Type::peak,
                                                      EqBand::Type::highShelf };
    constexpr float kBandFreqs[numEqBands] = { 100.0f, 300.0f, 1000.0f, 3500.0f, 8000.0f };
    constexpr float kBandQs[numEqBands]    = { 0.71f, 1.0f, 1.0f, 1.0f, 0.71f };

    // Tape range: cutoffs at knob = 1 (gentle "worn tape").
    constexpr float kTapeLowCutMax = 80.0f;
    constexpr float kTapeHighCutMin = 9000.0f;

    /** Low-cut / high-cut frequencies for a Tape knob value (0..1), log-spaced. */
    inline std::pair<double, double> tapeCutoffs (float amount)
    {
        const auto t = juce::jlimit (0.0, 1.0, (double) amount);
        return { 20.0 * std::pow (kTapeLowCutMax / 20.0, t),
                 20000.0 * std::pow (kTapeHighCutMin / 20000.0, t) };
    }

    /** The four biquads of the Tape filter: 2x high-pass then 2x low-pass.
        The first stage of each is a touch resonant for a vintage bump. */
    void designTape (float amount, double sampleRate, BiquadCoeffs out[4]);

    BiquadCoeffs designBand (const EqBand&, double sampleRate);

    /** Combined Tape + EQ response at f (dB), for drawing. */
    double responseDb (float tapeAmount, const EqBand* bands, double f, double sampleRate);
}

class ToneShaper
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    /** Audio thread. Smoothly moves to the given settings over the block. */
    void process (juce::AudioBuffer<float>&, float tapeAmount, const EqBand (&bands)[tone::numEqBands]);

private:
    static constexpr int maxChannels = 2;
    static constexpr int updateInterval = 32;   // samples between coefficient updates

    void updateCoefficients (float tape, const EqBand (&bands)[tone::numEqBands]);

    double sampleRate = 48000.0;
    int numChans = 2;

    Biquad tapeF[maxChannels][4];
    Biquad eqF[maxChannels][tone::numEqBands];
    bool tapeActive = false, eqActive[tone::numEqBands] {};

    juce::SmoothedValue<float> tapeSm;
    juce::SmoothedValue<float> gainSm[tone::numEqBands];
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> freqSm[tone::numEqBands];
    juce::SmoothedValue<float> qSm[tone::numEqBands];
};

} // namespace room
