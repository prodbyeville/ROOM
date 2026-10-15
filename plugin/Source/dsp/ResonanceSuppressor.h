#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>

// AUTO EQ: a live resonance suppressor (in the spirit of soothe2).
//
// The sound is analysed in short overlapping windows (STFT). In each window,
// any frequency that sticks out above its smoothed neighbourhood is pulled
// down by however much it sticks out (times Amount). Reductions are focused
// on the harsh range (roughly 2-10 kHz) and smoothed over time and frequency,
// so it only acts on resonances at the moments they poke out.
//
// Both channels share one set of gains, so the stereo image doesn't wobble.
// Latency = one window (reported to the host by the processor).

namespace room
{

class ResonanceSuppressor
{
public:
    static constexpr int numDisplayPoints = 128;

    void prepare (double sampleRate, int numChannels);
    void reset();

    /** Audio thread. amount: 0 (off) .. 1 (strong). */
    void process (juce::AudioBuffer<float>&, float amount);

    int getLatency() const { return fftSize; }

    /** Current gain reduction (dB, >= 0) at log-spaced frequencies from 20 Hz
        to 20 kHz, for the UI. Safe to read from any thread. */
    float getDisplayReductionDb (int i) const { return display[(size_t) i].load (std::memory_order_relaxed); }
    static float displayFrequency (int i) { return 20.0f * std::pow (1000.0f, (float) i / (numDisplayPoints - 1)); }

private:
    void processFrame (float amount);

    static constexpr int maxChannels = 2;

    double sampleRate = 48000.0;
    int numChans = 2;
    int fftOrder = 11, fftSize = 2048, hop = 512, numBins = 1025;
    bool reducing = false;   // any bin still being cut (or releasing)

    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window;                       // sqrt-Hann (analysis and synthesis)
    std::vector<float> inRing[maxChannels], outAcc[maxChannels];
    std::vector<float> frame[maxChannels];           // 2 * fftSize, FFT work buffers
    int ringPos = 0, hopCount = 0;

    std::vector<float> weight;                       // per bin: how much the bin may be reduced
    std::vector<float> powerDb, baselineDb, prefix, targetDb, reductionDb, smoothTmp, detector;
    std::vector<int> nbLo, nbHi;                     // per-bin neighbourhood for the baseline
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, detectorCoeff = 0.0f;

    std::array<std::atomic<float>, numDisplayPoints> display {};
    std::vector<int> displayLo, displayHi;
};

} // namespace room
