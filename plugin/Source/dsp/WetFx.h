#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>
#include "Biquad.h"

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
//     How much follows Intensity (`glitchAmount` 0..1): more often, more repeats
//     and more chopping as it goes up. Sync: slices are 1/32 to 1/8 notes at the
//     host tempo and every stutter starts on a 1/16 (free timing without a tempo).
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
        stutterBuf.setSize (maxCh, std::max (64, (int) (1.0 * sr)));
        shimmerWin = std::max (64, (int) (0.09 * sr));
        reverseLen = std::max (64, (int) (0.35 * sr));
        extremeSm.reset (sr, 0.08);
        stutterSm.reset (sr, 0.03);
        reset();
        setHarmonics (nullptr, 0);
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
        for (auto& chan : res) for (auto& f : chan) f.reset();
        fifthPhase = lowPhase = 0.0;
    }

    /** Sample by sample: `wet` holds one sample per channel and is changed in place. */
    void setTargets (float extremeAmount, bool stutterOn, float glitchAmount = 0.25f, bool syncOn = false)
    {
        extremeSm.setTargetValue (juce::jlimit (0.0f, 1.0f, extremeAmount));
        stutterSm.setTargetValue (stutterOn ? 1.0f : 0.0f);
        amount = juce::jlimit (0.0f, 1.0f, glitchAmount);
        sync = syncOn;
    }

    /** The host's position at the start of the next slice (for Sync). */
    void setTransport (bool hasPosition, double ppqNow, double bpmNow)
    {
        bpm = juce::jlimit (20.0, 999.0, bpmNow);
        hasPpq = hasPosition;
        if (hasPosition)
            ppq = ppqNow;
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
        lastPpq = ppq;
        ppq += bpm / 60.0 / sampleRate;

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
        // Digital breakup from 150 %, held at its 165 % amount (where it sounds best), then
        // handed over to the harmonic bloom, which takes 165 % .. 200 %.
        const auto h = smooth ((e - bloomStart) / (1.0f - bloomStart));
        const auto d = smooth ((std::min (e, bloomStart) - 0.5f) * 2.0f) * (1.0f - h);

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
        if (h > 0.0f)
            advanceBloomHeads();

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
            if (h > 0.0f)
                y = bloom (c, y, h);
            // Keep it about as loud as the room it came from, and tame the feedback.
            wet[c] = std::tanh (y * (1.0f / (1.0f + 0.45f * a)));
        }
    }

    // --- HARMONIC BLOOM (165 % .. 200 %) -------------------------------------------
    // Instead of breaking up further, the room starts to sing: a bank of resonators
    // tuned to the room's own notes (and their fifths and octaves) rings out of the
    // wet sound, a choir of grains a fifth up and an octave down thickens it, and a
    // warm, slightly asymmetric saturation glues it. No crushing, ring mod or gating.
    static constexpr float bloomStart = 0.65f;   // 165 % on the knob
    static constexpr int maxPartials = 8;

public:
    /** The room's resonant notes (Hz). Called when a room loads (engine, audio thread). */
    void setHarmonics (const float* hz, int count)
    {
        numPartials = 0;
        auto add = [this] (double f)
        {
            if (numPartials >= maxPartials || f < 40.0 || f > 0.42 * sampleRate)
                return;
            for (int k = 0; k < numPartials; ++k)
                if (std::abs (partialHz[k] - f) < f * 0.03)
                    return;   // already there
            partialHz[numPartials] = f;
            const auto q = 30.0 + 30.0 * (double) (numPartials % 3) / 2.0;   // Q 30..60
            const auto c = design::resonator (f, q, sampleRate);
            for (int ch = 0; ch < maxCh; ++ch) { res[ch][numPartials].c = c; res[ch][numPartials].reset(); }
            ++numPartials;
        };
        // the room's notes first, then their fifths and octaves while there's room
        for (int i = 0; i < count; ++i) add (hz[i]);
        for (int i = 0; i < count; ++i) add (hz[i] * 1.5);
        for (int i = 0; i < count; ++i) add (hz[i] * 2.0);
        if (numPartials == 0)
            for (auto f : { 110.0, 165.0, 220.0, 330.0, 440.0 })   // no room: an open fifth on A
                add (f);
    }

private:
    void advanceBloomHeads()
    {
        // a fifth up: the delay shrinks by half a sample per sample; an octave down: it grows by half
        fifthPhase += 0.5;
        if (fifthPhase >= shimmerWin) fifthPhase -= shimmerWin;
        lowPhase += 0.5;
        if (lowPhase >= shimmerWin) lowPhase -= shimmerWin;
    }

    float bloom (int c, float y, float h)
    {
        const auto pi = juce::MathConstants<double>::pi;
        // the grain choir
        const auto f0 = fifthPhase, f1 = std::fmod (fifthPhase + shimmerWin * 0.5, (double) shimmerWin);
        const auto l0 = lowPhase, l1 = std::fmod (lowPhase + shimmerWin * 0.5, (double) shimmerWin);
        const auto fifth = readHist (c, shimmerWin - f0) * (float) std::sin (pi * f0 / shimmerWin)
                         + readHist (c, shimmerWin - f1) * (float) std::sin (pi * f1 / shimmerWin);
        const auto low = readHist (c, l0 + 1.0) * (float) std::sin (pi * l0 / shimmerWin)
                       + readHist (c, l1 + 1.0) * (float) std::sin (pi * l1 / shimmerWin);
        // the room's notes ringing
        double ring = 0.0;
        const auto drive = (double) (y + 0.3f * fifth);
        for (int k = 0; k < numPartials; ++k)
            ring += res[c][k].process (drive) * (k < 3 ? 1.0 : 0.6);
        const auto sung = (float) ring * (2.2f / std::sqrt ((float) std::max (1, numPartials)));
        auto z = y * (1.0f - 0.45f * h) + h * (0.55f * fifth + 0.35f * low + 0.9f * sung);
        // warm saturation: soft, a little lopsided (even harmonics), level kept
        const auto drv = 1.0f + 1.5f * h;
        z = std::tanh (drv * z + 0.12f * h * z * z) / std::tanh (drv);
        return z;
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
            const auto synced = sync && hasPpq;
            if (stutterWait > 0)
                --stutterWait;
            // Synced, a stutter waits for the next 1/16 to start.
            const auto onGrid = ! synced || std::floor (ppq * 4.0) != std::floor (lastPpq * 4.0);
            if (st >= 0.999f && stutterWait <= 0 && onGrid)
            {
                // Grab a slice that just went by and start repeating it.
                if (sync)
                {
                    static constexpr double beats[] = { 0.125, 0.25, 0.25, 0.5 };   // 1/32, 1/16, 1/16, 1/8
                    sliceLen = (int) std::round (beats[rng.nextInt (4)] * 60.0 / bpm * sampleRate);
                }
                else
                {
                    static constexpr double lens[] = { 0.045, 0.06, 0.09, 0.125, 0.18 };
                    sliceLen = (int) (lens[rng.nextInt (5)] * sampleRate);
                }
                sliceLen = juce::jlimit (16, stutterBuf.getNumSamples(), sliceLen);
                for (int c = 0; c < ch; ++c)
                    for (int k = 0; k < sliceLen; ++k)
                        stutterBuf.setSample (c, k, readHist (c, (double) (sliceLen - k)));
                // More intensity: more repeats, more chopping.
                repeatsLeft = 2 + rng.nextInt (2 + juce::roundToInt (amount * 6.0f));
                chopped = rng.nextFloat() < 0.25f + 0.35f * amount;
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
                // More intensity: shorter gaps between stutters.
                const auto spread = 1.6 - 1.3 * (double) amount;
                if (sync)
                    stutterWait = (int) (60.0 / bpm * 0.25 * sampleRate * (1.0 + rng.nextInt (1 + juce::roundToInt (12.0 * spread))));
                else
                    stutterWait = (int) ((0.25 + rng.nextFloat() * 1.1) * spread * sampleRate);
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

    Biquad res[maxCh][maxPartials];
    double partialHz[maxPartials] {};
    int numPartials = 0;
    double fifthPhase = 0.0, lowPhase = 0.0;

    int holdCount = 0;
    float held[maxCh] {};
    double ringPhase = 0.0, ringHz = 120.0;
    float gate = 1.0f, gateSm = 1.0f;
    int gateCount = 0;

    bool stutterActive = false, chopped = false;
    float roomUnder = 1.0f;
    int stutterWait = 0, sliceLen = 1, slicePos = 0, repeatsLeft = 0;
    float amount = 0.25f;
    bool sync = false, hasPpq = false;
    double ppq = 0.0, lastPpq = 0.0, bpm = 120.0;
};

} // namespace room
