#include "ResonanceSuppressor.h"

namespace room
{

namespace
{
    // Tuning. Provisional: adjust by ear.
    constexpr double kNeighbourhoodOct = 1.0 / 3.0;  // +/- this many octaves form the "baseline"
    constexpr float kThresholdMaxDb = 12.0f;         // at a low Amount only big peaks are touched
    constexpr float kThresholdMinDb = 5.0f;          // at full Amount smaller ones are too
    constexpr float kRatioAtFull = 0.6f;             // share of the excess removed at full Amount
    constexpr float kMaxReductionMinDb = 4.0f;       // deepest allowed cut at a low Amount...
    constexpr float kMaxReductionDb = 12.0f;         // ...and at full Amount
    constexpr double kAttackS = 0.005;
    constexpr double kReleaseS = 0.08;
    constexpr double kDetectorS = 0.03;  // detector averaging: noise wobble cancels, real resonances persist

    // Focus on the harsh range: full strength 2-10 kHz, fading to nothing
    // at 800 Hz and 16 kHz (log scale). Bass and air are left alone.
    float focusWeight (double f)
    {
        auto ramp = [] (double x, double a, double b)
        {
            return (float) juce::jlimit (0.0, 1.0, (std::log (x) - std::log (a)) / (std::log (b) - std::log (a)));
        };
        if (f <= 800.0 || f >= 16000.0) return 0.0f;
        if (f < 2000.0)  return ramp (f, 800.0, 2000.0);
        if (f > 10000.0) return 1.0f - ramp (f, 10000.0, 16000.0);
        return 1.0f;
    }
}

void ResonanceSuppressor::prepare (double sr, int numChannels)
{
    sampleRate = sr;
    numChans = juce::jlimit (1, maxChannels, numChannels);

    // Same window length in milliseconds at any sample rate (~43 ms).
    fftOrder = sr <= 50000.0 ? 11 : sr <= 100000.0 ? 12 : 13;
    fftSize = 1 << fftOrder;
    hop = fftSize / 4;
    numBins = fftSize / 2 + 1;
    fft = std::make_unique<juce::dsp::FFT> (fftOrder);

    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i) // sqrt of a periodic Hann
        window[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / fftSize));

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        inRing[ch].assign ((size_t) fftSize, 0.0f);
        outAcc[ch].assign ((size_t) fftSize, 0.0f);
        frame[ch].assign ((size_t) fftSize * 2, 0.0f);
    }

    weight.resize ((size_t) numBins);
    nbLo.resize ((size_t) numBins);
    nbHi.resize ((size_t) numBins);
    const auto spread = std::pow (2.0, kNeighbourhoodOct);
    for (int k = 0; k < numBins; ++k)
    {
        const auto f = k * sr / fftSize;
        weight[(size_t) k] = focusWeight (f);
        nbLo[(size_t) k] = juce::jlimit (0, numBins - 1, std::min (k - 2, (int) std::floor (k / spread)));
        nbHi[(size_t) k] = juce::jlimit (0, numBins - 1, std::max (k + 2, (int) std::ceil (k * spread)));
    }

    for (auto* v : { &powerDb, &baselineDb, &targetDb, &reductionDb, &smoothTmp, &detector })
        v->assign ((size_t) numBins, 0.0f);
    prefix.assign ((size_t) numBins + 1, 0.0f);

    const auto frameS = hop / sr;
    attackCoeff = (float) std::exp (-frameS / kAttackS);
    releaseCoeff = (float) std::exp (-frameS / kReleaseS);
    detectorCoeff = (float) std::exp (-frameS / kDetectorS);

    // Each display point covers the bins between it and its neighbours, so
    // narrow cuts between points still show up.
    displayLo.resize (numDisplayPoints);
    displayHi.resize (numDisplayPoints);
    const auto halfStep = std::pow (1000.0, 0.5 / (numDisplayPoints - 1));
    for (int i = 0; i < numDisplayPoints; ++i)
    {
        const auto f = (double) displayFrequency (i);
        displayLo[(size_t) i] = juce::jlimit (0, numBins - 1, (int) std::floor (f / halfStep * fftSize / sr));
        displayHi[(size_t) i] = juce::jlimit (displayLo[(size_t) i], numBins - 1, (int) std::ceil (f * halfStep * fftSize / sr));
    }

    reset();
}

void ResonanceSuppressor::reset()
{
    for (int ch = 0; ch < maxChannels; ++ch)
    {
        std::fill (inRing[ch].begin(), inRing[ch].end(), 0.0f);
        std::fill (outAcc[ch].begin(), outAcc[ch].end(), 0.0f);
    }
    std::fill (reductionDb.begin(), reductionDb.end(), 0.0f);
    std::fill (detector.begin(), detector.end(), 0.0f);
    for (auto& d : display) d.store (0.0f);
    ringPos = 0;
    hopCount = 0;
}

void ResonanceSuppressor::process (juce::AudioBuffer<float>& buffer, float amount)
{
    const auto n = buffer.getNumSamples();
    const auto chans = std::min (buffer.getNumChannels(), numChans);

    for (int i = 0; i < n; ++i)
    {
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            inRing[ch][(size_t) ringPos] = d[i];
            d[i] = outAcc[ch][(size_t) ringPos];
            outAcc[ch][(size_t) ringPos] = 0.0f;
        }
        ringPos = (ringPos + 1) % fftSize;

        if (++hopCount == hop)
        {
            hopCount = 0;
            processFrame (amount);
        }
    }
}

void ResonanceSuppressor::processFrame (float amount)
{
    const auto chans = numChans;

    // 1. Windowed frames (oldest sample first) -> spectrum.
    for (int ch = 0; ch < chans; ++ch)
    {
        auto& fr = frame[ch];
        for (int i = 0; i < fftSize; ++i)
            fr[(size_t) i] = inRing[ch][(size_t) ((ringPos + i) % fftSize)] * window[(size_t) i];
        std::fill (fr.begin() + fftSize, fr.end(), 0.0f);
        fft->performRealOnlyForwardTransform (fr.data(), true);
    }

    // 2. Combined power spectrum (both channels), averaged over neighbouring
    //    bins and a few frames. Random noise wobbles a lot from frame to frame
    //    and averages out; a real resonance stays put and stands out.
    for (int k = 0; k < numBins; ++k)
    {
        double p = 0.0;
        for (int ch = 0; ch < chans; ++ch)
        {
            const auto re = frame[ch][(size_t) (2 * k)], im = frame[ch][(size_t) (2 * k + 1)];
            p += (double) re * re + (double) im * im;
        }
        smoothTmp[(size_t) k] = (float) p;
    }
    for (int k = 0; k < numBins; ++k)
    {
        const auto l = smoothTmp[(size_t) std::max (0, k - 1)], r = smoothTmp[(size_t) std::min (numBins - 1, k + 1)];
        const auto p = 0.25f * l + 0.5f * smoothTmp[(size_t) k] + 0.25f * r;
        auto& avg = detector[(size_t) k];
        avg = detectorCoeff * avg + (1.0f - detectorCoeff) * p;
        powerDb[(size_t) k] = 10.0f * std::log10 (avg + 1e-12f);
    }

    // 3. Baseline: average dB over each bin's +/- 1/3 octave neighbourhood.
    prefix[0] = 0.0f;
    for (int k = 0; k < numBins; ++k)
        prefix[(size_t) k + 1] = prefix[(size_t) k] + powerDb[(size_t) k];
    for (int k = 0; k < numBins; ++k)
    {
        const auto lo = nbLo[(size_t) k], hi = nbHi[(size_t) k];
        baselineDb[(size_t) k] = (prefix[(size_t) hi + 1] - prefix[(size_t) lo]) / (float) (hi - lo + 1);
    }

    // 4. Target reduction: how far each bin sticks out above its baseline.
    const auto a = juce::jlimit (0.0f, 1.0f, amount);
    const auto threshold = juce::jmap (a, kThresholdMaxDb, kThresholdMinDb);
    const auto ratio = kRatioAtFull * a;
    const auto maxCut = juce::jmap (a, kMaxReductionMinDb, kMaxReductionDb);
    for (int k = 0; k < numBins; ++k)
    {
        const auto excess = powerDb[(size_t) k] - baselineDb[(size_t) k] - threshold;
        targetDb[(size_t) k] = a > 0.0f && excess > 0.0f
                                   ? std::min (maxCut, excess * ratio * weight[(size_t) k])
                                   : 0.0f;
    }

    // 5. Smooth across frequency (3-bin average) so cuts aren't razor thin.
    for (int k = 0; k < numBins; ++k)
    {
        const auto l = targetDb[(size_t) std::max (0, k - 1)], r = targetDb[(size_t) std::min (numBins - 1, k + 1)];
        smoothTmp[(size_t) k] = std::max (targetDb[(size_t) k] * 0.6f, (l + targetDb[(size_t) k] + r) / 3.0f);
    }

    // 6. Smooth over time (fast attack, slower release) and apply.
    for (int k = 0; k < numBins; ++k)
    {
        auto& r = reductionDb[(size_t) k];
        const auto t = smoothTmp[(size_t) k];
        const auto c = t > r ? attackCoeff : releaseCoeff;
        r = c * r + (1.0f - c) * t;
        if (r < 1e-4f) r = 0.0f;

        if (r > 0.0f)
        {
            const auto g = juce::Decibels::decibelsToGain (-r);
            for (int ch = 0; ch < chans; ++ch)
            {
                frame[ch][(size_t) (2 * k)] *= g;
                frame[ch][(size_t) (2 * k + 1)] *= g;
            }
        }
    }

    // 7. Back to audio: inverse FFT, synthesis window, overlap-add.
    //    sqrt-Hann * sqrt-Hann = Hann, which sums to 2 at 75 % overlap.
    for (int ch = 0; ch < chans; ++ch)
    {
        auto& fr = frame[ch];
        fft->performRealOnlyInverseTransform (fr.data());
        for (int i = 0; i < fftSize; ++i)
            outAcc[ch][(size_t) ((ringPos + i) % fftSize)] += fr[(size_t) i] * window[(size_t) i] * 0.5f;
    }

    for (int i = 0; i < numDisplayPoints; ++i)
    {
        float m = 0.0f;
        for (int k = displayLo[(size_t) i]; k <= displayHi[(size_t) i]; ++k)
            m = std::max (m, reductionDb[(size_t) k]);
        display[(size_t) i].store (m, std::memory_order_relaxed);
    }
}

} // namespace room
