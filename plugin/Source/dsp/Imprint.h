#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include "Biquad.h"
#include "RoomAnalysis.h"
#include "RoomSynth.h"

// IMPRINT: the sound takes on the room's timbre, with or without its tail.
//
// ROOM listens to the incoming sound's spectral envelope (nine octave bands, about a
// quarter second of memory) and compares it with the room's own (its tone curve). Each
// band is pushed by the difference, so whatever plays is re-voiced to sound like it was
// made of the room: the room's broad colour plus its three strongest notes. Pitch and
// timing are untouched. It runs on the input before the room, so the dry sound and the
// room both carry it.
//
// A minimum-phase filter chain whose gains follow the input: no latency, no smearing,
// level-matched (the makeup keeps the input's loudness). At 0 it is exactly bypassed.
// Everything is preallocated; process() never allocates or waits.

namespace room
{

class Imprint
{
public:
    static constexpr int numBands = 9;
    static constexpr int maxPeaks = 3;

    /** The room's side of the comparison, made on the worker thread at a room change. */
    struct Profile
    {
        bool valid = false;
        float bandDb[numBands] {};      // the room's tone, relative to its own average
        float peakHz[maxPeaks] {}, peakDb[maxPeaks] {}, peakQ[maxPeaks] {};
        int numPeaks = 0;
    };

    static Profile makeProfile (const RoomAnalysis& a, const ToneSettings& tone)
    {
        Profile p;
        if (! a.valid || a.toneFreqsHz.empty() || a.toneFreqsHz.size() != a.toneCurveDb.size())
            return p;
        float mean = 0.0f;
        for (int b = 0; b < numBands; ++b)
        {
            p.bandDb[b] = interp (a.toneFreqsHz, a.toneCurveDb, centres()[b]);
            mean += p.bandDb[b];
        }
        mean /= (float) numBands;
        for (auto& d : p.bandDb)
            d -= mean;

        // the room's strongest notes (already tuned when Tune to key is on)
        for (int pass = 0; pass < maxPeaks; ++pass)
        {
            int best = -1;
            for (int k = 0; k < tone.numResonators; ++k)
            {
                bool used = false;
                for (int j = 0; j < p.numPeaks; ++j)
                    used = used || std::abs (p.peakHz[j] - tone.resonatorHz[k]) < 1.0f;
                if (! used && (best < 0 || tone.resonatorExtra[k] > tone.resonatorExtra[best]))
                    best = k;
            }
            if (best < 0)
                break;
            p.peakHz[p.numPeaks] = tone.resonatorHz[best];
            p.peakQ[p.numPeaks] = juce::jlimit (2.0f, 30.0f, tone.resonatorQ[best]);
            p.peakDb[p.numPeaks] = juce::jlimit (0.0f, 12.0f, 20.0f * std::log10 (1.0f + std::max (0.0f, tone.resonatorExtra[best])));
            ++p.numPeaks;
        }
        p.valid = true;
        return p;
    }

    void prepare (double sr, int channels)
    {
        sampleRate = sr;
        chans = juce::jlimit (1, maxCh, channels);
        for (int b = 0; b < numBands; ++b)
            detect[b].c = design::resonator (centres()[b], 1.4, sr);
        amountSm.reset (sr, 0.05);
        amountSm.setCurrentAndTargetValue (0.0f);
        reset();
    }

    void reset()
    {
        for (auto& d : detect) d.reset();
        for (auto& chan : eq) for (auto& f : chan) f.reset();
        for (auto& chan : peaks) for (auto& f : chan) f.reset();
        for (auto& p : bandPow) p = 1.0e-7f;
        for (auto& g : gainDb) g = 0.0f;
        makeup = 1.0f;
    }

    /** Any thread but the audio thread. */
    void setProfile (const Profile& p)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        pending = p;
        hasPending = true;
    }

    void setAmount (float amount) { amountSm.setTargetValue (juce::jlimit (0.0f, 1.0f, amount)); }
    bool isIdle() const { return amountSm.getCurrentValue() <= 0.0f && ! amountSm.isSmoothing(); }

    void process (juce::AudioBuffer<float>& buffer, int numSamples)
    {
        {
            const juce::SpinLock::ScopedTryLockType sl (lock);
            if (sl.isLocked() && hasPending)
            {
                profile = pending;
                hasPending = false;
            }
        }
        if (isIdle() || ! profile.valid)
        {
            if (active)
            {
                reset();
                active = false;
            }
            if (! profile.valid)
                amountSm.setCurrentAndTargetValue (amountSm.getTargetValue());
            return;
        }
        active = true;
        const auto ch = std::min (buffer.getNumChannels(), chans);

        for (int start = 0; start < numSamples; start += chunk)
        {
            const auto len = std::min (chunk, numSamples - start);
            const auto amt = amountSm.getCurrentValue();
            amountSm.skip (len);
            updateFilters (amt, len);

            for (int i = start; i < start + len; ++i)
            {
                // listen to the input (mono sum) in nine bands
                float mono = 0.0f;
                for (int c = 0; c < ch; ++c)
                    mono += buffer.getSample (c, i);
                mono /= (float) ch;
                for (int b = 0; b < numBands; ++b)
                {
                    const auto y = (float) detect[b].process (mono);
                    bandPow[b] += (y * y - bandPow[b]) * powCoeff();
                }
                for (int c = 0; c < ch; ++c)
                {
                    double x = buffer.getSample (c, i);
                    for (int b = 0; b < numBands; ++b)
                        x = eq[c][b].process (x);
                    for (int k = 0; k < profile.numPeaks; ++k)
                        x = peaks[c][k].process (x);
                    buffer.setSample (c, i, (float) x * makeup);
                }
            }
        }
    }

    /** For the UI / tests: the gain (dB) each band is pushed by right now. */
    float bandGainDb (int b) const { return gainDb[juce::jlimit (0, numBands - 1, b)]; }

    static const double* centres()
    {
        static constexpr double c[numBands] = { 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000 };
        return c;
    }

private:
    static constexpr int maxCh = 2;
    static constexpr int chunk = 32;

    static float interp (const std::vector<float>& xs, const std::vector<float>& ys, double x)
    {
        if (x <= xs.front()) return ys.front();
        if (x >= xs.back())  return ys.back();
        const auto it = std::upper_bound (xs.begin(), xs.end(), (float) x);
        const auto i = (size_t) std::distance (xs.begin(), it);
        const auto t = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
        return (float) (ys[i - 1] + t * (ys[i] - ys[i - 1]));
    }

    float powCoeff() const { return 1.0f - std::exp (-1.0f / (0.25f * (float) sampleRate)); }

    void updateFilters (float amt, int len)
    {
        // The input's envelope, relative to its own average (bands it doesn't reach count as -60 dB down)
        float inDb[numBands], mean = 0.0f, loudest = -200.0f;
        for (int b = 0; b < numBands; ++b)
        {
            inDb[b] = 10.0f * std::log10 (bandPow[b] + 1.0e-12f);
            loudest = std::max (loudest, inDb[b]);
        }
        const auto quiet = loudest < -80.0f;   // nothing playing: keep the last shape
        for (auto& d : inDb)
        {
            d = std::max (d, loudest - 60.0f);
            mean += d;
        }
        mean /= (float) numBands;

        const auto ease = 1.0f - std::exp (-(float) len / (0.06f * (float) sampleRate));
        double num = 0.0, den = 0.0;
        for (int b = 0; b < numBands; ++b)
        {
            if (! quiet)
            {
                const auto want = juce::jlimit (-15.0f, 15.0f, profile.bandDb[b] - (inDb[b] - mean)) * amt;
                gainDb[b] += (want - gainDb[b]) * ease;
            }
            const auto p = std::pow (10.0, inDb[b] / 10.0);
            num += p * std::pow (10.0, gainDb[b] / 10.0);
            den += p;
        }
        // keep the input's loudness
        const auto wantMakeup = den > 0.0 ? (float) std::sqrt (den / std::max (1.0e-20, num)) : 1.0f;
        makeup += (juce::jlimit (0.25f, 4.0f, wantMakeup) - makeup) * ease;

        for (int b = 0; b < numBands; ++b)
        {
            const auto f = centres()[b];
            BiquadCoeffs c;
            if (b == 0)                 c = design::lowShelf (90.0, gainDb[b], sampleRate);
            else if (b == numBands - 1) c = design::highShelf (11300.0, gainDb[b], sampleRate);
            else                        c = design::peaking (f, 1.4, gainDb[b], sampleRate);
            for (int ch = 0; ch < maxCh; ++ch)
                eq[ch][b].c = c;
        }
        for (int k = 0; k < profile.numPeaks; ++k)
        {
            const auto c = design::peaking (std::min ((double) profile.peakHz[k], 0.45 * sampleRate), profile.peakQ[k],
                                            profile.peakDb[k] * amt, sampleRate);
            for (int ch = 0; ch < maxCh; ++ch)
                peaks[ch][k].c = c;
        }
    }

    double sampleRate = 44100.0;
    int chans = 2;
    Biquad detect[numBands];
    Biquad eq[maxCh][numBands];
    Biquad peaks[maxCh][maxPeaks];
    float bandPow[numBands] {};
    float gainDb[numBands] {};
    float makeup = 1.0f;
    bool active = false;
    juce::SmoothedValue<float> amountSm;

    Profile profile, pending;
    bool hasPending = false;
    juce::SpinLock lock;
};

} // namespace room
