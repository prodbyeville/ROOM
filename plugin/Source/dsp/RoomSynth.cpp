#include "RoomSynth.h"

#include <juce_dsp/juce_dsp.h>
#include <random>

namespace room
{

namespace
{
    // Mirrors room_engine/synthesis.py
    constexpr double kPreDelayS = 0.010;
    constexpr double kFadeInS = 0.003;
    constexpr double kFadeOutS = 0.05;
    constexpr float kMinT60 = 0.05f;
    constexpr float kMaxResonanceGainDb = 18.0f;
    constexpr float kEqMatchDepth = 0.6f;
    constexpr float kEqMaxDb = 12.0f;

    // The live plugin can't see the whole incoming track ahead of time, so
    // the room's tone curve is compared against a fixed "typical music"
    // tilt instead of the target's own curve. -6 dB/octave sits in the
    // middle of Ethan's test sounds (about -5 to -15). Tune by ear.
    constexpr double kReferenceTiltDbPerOct = -6.0;

    // EQ bands: low shelf, seven peaks one octave apart, high shelf.
    constexpr double kEqCentres[ToneSettings::maxEq] = { 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
    constexpr double kEqPeakQ = 1.4;
    constexpr double kLowShelfHz = 90.0;
    constexpr double kHighShelfHz = 11300.0;

    BiquadCoeffs eqBand (int i, double gainDb, double sr)
    {
        if (i == 0)                       return design::lowShelf (kLowShelfHz, gainDb, sr);
        if (i == ToneSettings::maxEq - 1) return design::highShelf (std::min (kHighShelfHz, 0.4 * sr), gainDb, sr);
        return design::peaking (kEqCentres[i], kEqPeakQ, gainDb, sr);
    }

    double referenceDb (double f) { return kReferenceTiltDbPerOct * std::log2 (f / 1000.0); }

    /** Average power gain of an IR for music with the reference tilt. */
    float referencePowerGain (const juce::AudioBuffer<float>& ir, double sr)
    {
        const auto order = juce::jlimit (8, 21, (int) std::ceil (std::log2 ((double) ir.getNumSamples())));
        const auto size = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) size * 2);

        double num = 0.0, den = 0.0;
        for (int ch = 0; ch < ir.getNumChannels(); ++ch)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            std::copy_n (ir.getReadPointer (ch), std::min (size, ir.getNumSamples()), buf.begin());
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);
            for (int b = 1; b <= size / 2; ++b)
            {
                const auto f = b * sr / size;
                if (f < 60.0 || f > std::min (16000.0, 0.45 * sr))
                    continue;
                const auto w = std::pow (10.0, referenceDb (f) / 10.0); // power per Hz
                num += w * (double) buf[(size_t) b] * buf[(size_t) b];
                den += w;
            }
        }
        return den > 0 ? (float) std::max (1e-6, num / den) : 1.0f;
    }

    float interpCurve (const std::vector<float>& xs, const std::vector<float>& ys, double x)
    {
        if (x <= xs.front()) return ys.front();
        if (x >= xs.back())  return ys.back();
        const auto it = std::upper_bound (xs.begin(), xs.end(), (float) x);
        const auto i = (size_t) std::distance (xs.begin(), it);
        const auto t = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
        return (float) (ys[i - 1] + t * (ys[i] - ys[i - 1]));
    }
}

//==============================================================================

juce::AudioBuffer<float> synthesizeIR (const RoomAnalysis& a, double sr,
                                       float tailSeconds, float intensity, int seed)
{
    const auto n = std::max (1, (int) (tailSeconds * sr));
    juce::AudioBuffer<float> ir (2, n);
    ir.clear();
    if (! a.valid)
        return ir;

    std::mt19937 rng ((unsigned) seed);
    std::normal_distribution<double> normal;
    std::vector<double> chan ((size_t) n), band ((size_t) n);

    for (int ch = 0; ch < 2; ++ch)
    {
        std::fill (chan.begin(), chan.end(), 0.0);
        for (int b = 0; b < a.numBands(); ++b)
        {
            BandFilter f;
            f.design (a.bandEdgesHz[(size_t) b], a.bandEdgesHz[(size_t) b + 1], sr);

            double sumSq = 0.0;
            for (int i = 0; i < n; ++i)
            {
                band[(size_t) i] = f.process (normal (rng));
                sumSq += band[(size_t) i] * band[(size_t) i];
            }
            // Unit loudness per band, so narrow low bands aren't quieter.
            const auto norm = 1.0 / (std::sqrt (sumSq / n) + 1e-20);

            // exp(-6.91 t / T60) falls by 60 dB at t = T60.
            const auto t60 = std::max (kMinT60, a.bandT60s[(size_t) b] * intensity);
            const auto k = std::exp (-6.91 / (t60 * sr)); // per-sample decay factor
            double env = 1.0;
            for (int i = 0; i < n; ++i)
            {
                chan[(size_t) i] += band[(size_t) i] * norm * env;
                env *= k;
            }
        }

        // Fade in, pre-delay, fade out.
        const auto fadeIn = (int) (kFadeInS * sr);
        for (int i = 0; i < fadeIn && i < n; ++i)
            chan[(size_t) i] *= (double) i / fadeIn;

        const auto pre = std::min (n, (int) (kPreDelayS * sr));
        auto* out = ir.getWritePointer (ch);
        for (int i = n - 1; i >= 0; --i)
            out[i] = i >= pre ? (float) chan[(size_t) (i - pre)] : 0.0f;

        const auto fadeOut = std::min (n, (int) (kFadeOutS * sr));
        for (int i = 0; i < fadeOut; ++i)
            out[n - 1 - i] *= (float) i / (float) fadeOut;

        // Unit energy, so the wet level is predictable.
        double e = 0.0;
        for (int i = 0; i < n; ++i) e += (double) out[i] * out[i];
        const auto g = (float) (1.0 / (std::sqrt (e) + 1e-20));
        for (int i = 0; i < n; ++i) out[i] *= g;
    }

    // Each band gets equal power, and low bands are narrow, so the IR is
    // heavier in the lows where most music lives. Scale it so typical music
    // (the reference tilt) comes out of the reverb at the level it went in.
    // The prototype got this for free by level-matching each whole file.
    ir.applyGain (1.0f / std::sqrt (referencePowerGain (ir, sr)));
    return ir;
}

//==============================================================================

std::vector<float> targetEqCurveDb (const RoomAnalysis& a, float intensity)
{
    const auto& fr = a.toneFreqsHz;
    std::vector<float> out (fr.size(), 0.0f);
    if (! a.valid || fr.empty())
        return out;

    double srcMean = 0.0, refMean = 0.0;
    for (size_t i = 0; i < fr.size(); ++i)
    {
        srcMean += a.toneCurveDb[i];
        refMean += referenceDb (fr[i]);
    }
    srcMean /= (double) fr.size();
    refMean /= (double) fr.size();

    const auto depth = kEqMatchDepth * intensity;
    for (size_t i = 0; i < fr.size(); ++i)
    {
        const auto diff = (a.toneCurveDb[i] - srcMean) - (referenceDb (fr[i]) - refMean);
        out[i] = juce::jlimit (-kEqMaxDb, kEqMaxDb, (float) diff) * depth;
    }
    return out;
}

ToneSettings designTone (const RoomAnalysis& a, double sr, float intensity)
{
    ToneSettings t;
    if (! a.valid)
        return t;

    // --- Tone-match EQ: fit 9 bands to the target curve -----------------------
    const auto curve = targetEqCurveDb (a, intensity);
    double target[ToneSettings::maxEq], gains[ToneSettings::maxEq];
    for (int i = 0; i < ToneSettings::maxEq; ++i)
        gains[i] = target[i] = interpCurve (a.toneFreqsHz, curve, kEqCentres[i]);

    // Neighbouring bands overlap, so nudge the gains a few times until the
    // combined response hits the target at each band centre.
    for (int iter = 0; iter < 8; ++iter)
    {
        for (int i = 0; i < ToneSettings::maxEq; ++i)
        {
            double resp = 0.0;
            for (int j = 0; j < ToneSettings::maxEq; ++j)
                resp += eqBand (j, gains[j], sr).magnitudeDb (kEqCentres[i], sr);
            gains[i] = juce::jlimit (-24.0, 24.0, gains[i] + 0.7 * (target[i] - resp));
        }
    }

    t.numEq = 0;
    for (int i = 0; i < ToneSettings::maxEq; ++i)
        if (kEqCentres[i] < 0.45 * sr || i == ToneSettings::maxEq - 1)
            t.eq[t.numEq++] = eqBand (i, gains[i], sr);

    // --- Resonators ------------------------------------------------------------
    for (auto& p : a.peaks)
    {
        if (t.numResonators >= ToneSettings::maxResonators || p.freqHz >= 0.45 * sr)
            continue;
        const auto gainDb = std::min (p.gainDb, kMaxResonanceGainDb) * intensity;
        t.resonator[t.numResonators] = design::resonator (p.freqHz, p.q, sr);
        t.resonatorExtra[t.numResonators] = std::pow (10.0f, gainDb / 20.0f) - 1.0f;
        ++t.numResonators;
    }

    // --- Loudness compensation ------------------------------------------------
    // Average power gain of the whole tonal path for typical music (the
    // reference tilt), so changing rooms or intensity doesn't jump in level.
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i < a.toneFreqsHz.size(); i += 4)
    {
        const double f = a.toneFreqsHz[i];
        if (f >= 0.45 * sr)
            break;
        std::complex<double> h = 1.0;
        for (int k = 0; k < t.numEq; ++k)
            h *= t.eq[k].response (f, sr);
        std::complex<double> res = 1.0;
        for (int k = 0; k < t.numResonators; ++k)
            res += (double) t.resonatorExtra[k] * t.resonator[k].response (f, sr);
        h *= res;

        const auto w = std::pow (10.0, referenceDb (f) / 10.0) * f;
        num += w * std::norm (h);
        den += w;
    }
    t.tonalGain = den > 0 ? (float) (1.0 / std::sqrt (num / den + 1e-20)) : 1.0f;
    return t;
}

} // namespace room
