#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

// Two effects on the wet (room) sound only, after the room and before freeze:
//
//   EXTREME (Intensity above 100%): `amount` 0..1 across the knob's top half.
//     0 .. 0.5   smear + shimmer: an octave-up shimmer that feeds on itself and
//                reversed swells, so the room turns into a wash.
//     0.5 .. 1   digital breakup on top: sample-rate reduction, bit crushing,
//                a drifting ring modulator and choppy gating.
//     At 0 it is exactly bypassed (bit-identical), so 0..100% is untouched.
//
//   STUTTER (the glitch key): every so often it grabs a short slice of the
//     room and repeats it a few times, sometimes chopped; between stutters the
//     room passes through. Switching it off finishes the current repeat.
//
// Everything is preallocated in prepare(); process() never allocates.

namespace room
{

class WetFx
{
public:
    void prepare (double sr, int channels)
    {
        sampleRate = sr;
        chans = juce::jlimit (1, maxCh, channels);
        histLen = std::max (64, (int) (2.0 * sr));
        hist.setSize (maxCh, histLen);
        stutterBuf.setSize (maxCh, std::max (64, (int) (0.4 * sr)));
        shimmerWin = std::max (64, (int) (0.09 * sr));
        reverseLen = std::max (64, (int) (0.35 * sr));
        extremeSm.reset (sr, 0.08);
        stutterSm.reset (sr, 0.03);
        reset();
    }

    void reset()
    {
        hist.clear();
        stutterBuf.clear();
        write = 0;
        shimPhase = 0.0;
        revPos = 0;
        holdCount = 0;
        for (auto& h : held) h = 0.0f;
        ringPhase = 0.0;
        ringHz = 120.0;
        gate = 1.0f;
        gateCount = 0;
        stutterActive = false;
        roomUnder = 1.0f;
        stutterWait = (int) (0.3 * sampleRate);
        extremeSm.setCurrentAndTargetValue (0.0f);
        stutterSm.setCurrentAndTargetValue (0.0f);
    }

    /** Sample by sample: `wet` holds one sample per channel and is changed in place. */
    void setTargets (float extremeAmount, bool stutterOn)
    {
        extremeSm.setTargetValue (juce::jlimit (0.0f, 1.0f, extremeAmount));
        stutterSm.setTargetValue (stutterOn ? 1.0f : 0.0f);
    }

    bool isIdle() const
    {
        return extremeSm.getCurrentValue() <= 0.0f && ! extremeSm.isSmoothing()
            && stutterSm.getCurrentValue() <= 0.0f && ! stutterSm.isSmoothing() && ! stutterActive;
    }

    void processSample (float* wet, int numCh)
    {
        const auto ch = std::min (numCh, chans);
        const auto e = extremeSm.getNextValue();
        const auto st = stutterSm.getNextValue();

        // Keep a short history of the (incoming) room for the shimmer / reverse / stutter reads.
        for (int c = 0; c < ch; ++c)
            hist.setSample (c, write, wet[c]);

        if (e > 0.0f)
            extreme (wet, ch, e);
        if (st > 0.0f || stutterActive || roomUnder < 1.0f)
            stutter (wet, ch, st);

        // The shimmer feeds back: what we hear goes back into the history.
        if (e > 0.0f)
            for (int c = 0; c < ch; ++c)
                hist.setSample (c, write, hist.getSample (c, write) + 0.42f * smooth (e * 2.0f) * shimmerOut[c]);

        write = (write + 1) % histLen;
    }

private:
    static constexpr int maxCh = 2;

    static float smooth (float x) { x = juce::jlimit (0.0f, 1.0f, x); return x * x * (3.0f - 2.0f * x); }

    float readHist (int c, double samplesAgo) const
    {
        auto pos = (double) write - samplesAgo;
        while (pos < 0) pos += histLen;
        const auto i0 = (int) pos % histLen;
        const auto i1 = (i0 + 1) % histLen;
        const auto f = (float) (pos - std::floor (pos));
        return hist.getSample (c, i0) + f * (hist.getSample (c, i1) - hist.getSample (c, i0));
    }

    void extreme (float* wet, int ch, float e)
    {
        const auto a = smooth (e * 2.0f);             // smear + shimmer: full by 150%
        const auto d = smooth ((e - 0.5f) * 2.0f);    // digital breakup: 150% .. 200%

        // --- Shimmer: two read heads sweeping at double speed (octave up), crossfaded.
        shimPhase += 1.0;   // the delay shrinks by one sample per sample = 2x speed
        if (shimPhase >= shimmerWin) shimPhase -= shimmerWin;
        const auto p0 = shimPhase, p1 = std::fmod (shimPhase + shimmerWin * 0.5, (double) shimmerWin);
        const auto w0 = (float) std::sin (juce::MathConstants<double>::pi * p0 / shimmerWin);
        const auto w1 = (float) std::sin (juce::MathConstants<double>::pi * p1 / shimmerWin);

        // --- Reverse: read the last 350 ms backwards in overlapping windows.
        revPos = (revPos + 1) % reverseLen;
        const auto rw = (float) std::sin (juce::MathConstants<double>::pi * revPos / reverseLen);

        // --- Digital: hold (sample-rate reduction) + crush + ring + gate.
        const auto holdN = 1 + (int) (d * 22.0f);
        const auto bits = 16.0f - d * 12.0f;
        const auto steps = std::pow (2.0f, bits);
        if (--holdCount <= 0) holdCount = holdN;
        ringHz += (rng.nextFloat() - 0.5f) * 0.6;   // the carrier drifts
        ringHz = juce::jlimit (40.0, 520.0, ringHz);
        ringPhase += ringHz / sampleRate;
        if (ringPhase > 1.0) ringPhase -= 1.0;
        const auto ring = (float) std::sin (juce::MathConstants<double>::twoPi * ringPhase);
        if (--gateCount <= 0)
        {
            gateCount = (int) (sampleRate / (6.0 + rng.nextFloat() * 14.0));
            gate = rng.nextFloat() < 0.35f * d ? 0.0f : 1.0f;
        }
        gateSm += (gate - gateSm) * 0.004f;

        for (int c = 0; c < ch; ++c)
        {
            const auto dry = wet[c];
            const auto shim = readHist (c, shimmerWin - p0) * w0 + readHist (c, shimmerWin - p1) * w1;
            shimmerOut[c] = std::tanh (shim);
            const auto rev = readHist (c, (double) (2 * revPos)) * rw;

            auto y = dry * (1.0f - 0.35f * a) + 0.75f * a * shimmerOut[c] + 0.55f * a * rev;

            if (d > 0.0f)
            {
                if (holdCount == holdN) held[c] = y;
                auto z = std::round (held[c] * steps) / steps;
                z = z * (1.0f - 0.6f * d) + 0.6f * d * z * ring;
                y = (1.0f - d) * y + d * z;
                y *= 1.0f - 0.85f * d * (1.0f - gateSm);
            }
            // Keep it about as loud as the room it came from, and tame the feedback.
            wet[c] = std::tanh (y * (1.0f / (1.0f + 0.45f * a)));
        }
    }

    void stutter (float* wet, int ch, float st)
    {
        // The room ducks under a repeat and comes back after it (3 ms ramps: no clicks).
        const auto ramp = 1.0f / (float) std::max (8.0, 0.003 * sampleRate);
        roomUnder = stutterActive ? std::max (0.0f, roomUnder - ramp) : std::min (1.0f, roomUnder + ramp);

        if (! stutterActive)
        {
            for (int c = 0; c < ch; ++c)
                wet[c] *= roomUnder;
            if (st >= 0.999f && --stutterWait <= 0)
            {
                // Grab a slice that just went by and start repeating it.
                static constexpr double lens[] = { 0.045, 0.06, 0.09, 0.125, 0.18 };
                sliceLen = std::min (stutterBuf.getNumSamples(), (int) (lens[rng.nextInt (5)] * sampleRate));
                for (int c = 0; c < ch; ++c)
                    for (int k = 0; k < sliceLen; ++k)
                        stutterBuf.setSample (c, k, readHist (c, (double) (sliceLen - k)));
                repeatsLeft = 2 + rng.nextInt (5);
                chopped = rng.nextFloat() < 0.4f;
                slicePos = 0;
                stutterActive = true;
            }
            return;
        }

        const auto fade = std::max (8, (int) (0.003 * sampleRate));
        auto env = 1.0f;
        if (slicePos < fade) env = (float) slicePos / (float) fade;
        else if (slicePos > sliceLen - fade) env = (float) (sliceLen - slicePos) / (float) fade;
        if (chopped && slicePos > sliceLen / 2)
            env *= juce::jlimit (0.0f, 1.0f, (float) (sliceLen / 2 + fade - slicePos) / (float) fade);

        for (int c = 0; c < ch; ++c)
            wet[c] = roomUnder * wet[c] + stutterBuf.getSample (c, slicePos) * env;

        if (++slicePos >= sliceLen)
        {
            slicePos = 0;
            if (--repeatsLeft <= 0 || st < 0.5f)
            {
                stutterActive = false;
                stutterWait = (int) ((0.25 + rng.nextFloat() * 1.1) * sampleRate);
            }
        }
    }

    double sampleRate = 44100.0;
    int chans = 2, histLen = 1, write = 0;
    juce::AudioBuffer<float> hist, stutterBuf;
    juce::SmoothedValue<float> extremeSm, stutterSm;
    juce::Random rng { 0x524f4f4d };

    int shimmerWin = 1, reverseLen = 1, revPos = 0;
    double shimPhase = 0.0;
    float shimmerOut[maxCh] {};

    int holdCount = 0;
    float held[maxCh] {};
    double ringPhase = 0.0, ringHz = 120.0;
    float gate = 1.0f, gateSm = 1.0f;
    int gateCount = 0;

    bool stutterActive = false, chopped = false;
    float roomUnder = 1.0f;
    int stutterWait = 0, sliceLen = 1, slicePos = 0, repeatsLeft = 0;
};

} // namespace room
