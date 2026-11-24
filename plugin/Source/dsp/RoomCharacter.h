#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include "Biquad.h"

// The room falls apart, Intensity 100..175 % (`amount` 0..1 across it), on the wet (room)
// sound before the shimmer / bloom of 175..200 % (WetFx.h). Each artifact comes in over its
// own part of the range and is pushed well past "subtle":
//
//   ringing walls    100..130 %  the room's notes ring 12x louder and 4x longer, feed back
//                                into themselves like metal and wobble in pitch (RoomEngine,
//                                tonal path: ringAmount())
//   flutter slap     105..140 %  two parallel walls: a bright feedback echo (up to 0.88) that
//                                bounces left <-> right; its period from the room (12..90 ms)
//   wall comb        110..145 %  a near reflection whose distance drifts: a boxy flanging room
//   pushed mic       125..175 %  a preamp driven far too hard: a pumping compressor, a low-mid
//                                lift, heavy lopsided drive that clips flat at the top
//
// Levels follow the input over 0.8 s (+1.5 dB allowed at the top). Every feedback path is
// bounded (tanh). At 0 it is exactly bypassed (bit-identical).
// Everything is preallocated in prepare(); processSample() never allocates.

namespace room
{

class RoomCharacter
{
public:
    void prepare (double sr, int channels)
    {
        sampleRate = sr;
        chans = juce::jlimit (1, maxCh, channels);
        bufLen = std::max (64, (int) (0.12 * sr) + 4);
        for (auto& b : buf) b.assign ((size_t) bufLen, 0.0f);
        amountSm.reset (sr, 0.08);
        amountSm.setCurrentAndTargetValue (0.0f);
        setRoom (25.0f, 1.6f);
        reset();
    }

    void reset()
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        write = 0;
        for (int c = 0; c < maxCh; ++c)
        {
            loopLp[c] = 0.0f;
            dcIn[c] = dcOut[c] = 0.0f;
            lift[c].reset();
        }
        compEnv = 0.0f;
        avgPow = outPow = 0.0f;
        sweepPhase = 0.0;
    }

    /** From the room (ToneSettings, at a room change; audio thread). */
    void setRoom (float flutterMs, float combMs)
    {
        flutterLen[0] = juce::jlimit (8.0, (double) bufLen - 3.0, flutterMs * 0.001 * sampleRate);
        flutterLen[1] = juce::jlimit (8.0, (double) bufLen - 3.0, flutterLen[0] * 1.071);   // slightly wider walls on the right
        combLen[0] = juce::jlimit (2.0, (double) bufLen - 3.0, combMs * 0.001 * sampleRate);
        combLen[1] = juce::jlimit (2.0, (double) bufLen - 3.0, combLen[0] * 0.93);
    }

    void setAmount (float amount)
    {
        if (wasActive && isIdle())   // finished fading out: start clean next time
        {
            reset();
            wasActive = false;
        }
        amountSm.setTargetValue (juce::jlimit (0.0f, 1.0f, amount));
    }

    bool isIdle() const { return amountSm.getCurrentValue() <= 0.0f && ! amountSm.isSmoothing(); }

    /** How much the room's notes ring (0..1) for an amount: 100..130 %. */
    static float ringAmount (float amount) { return smooth (amount / 0.4f); }

    void processSample (float* wet, int numCh)
    {
        const auto ch = std::min (numCh, chans);
        const auto c = amountSm.getNextValue();
        if (c <= 0.0f)
        {
            if (wasActive)
            {
                reset();
                wasActive = false;
            }
            return;
        }
        wasActive = true;

        const auto flutter = smooth ((c - 0.067f) / 0.47f);   // 105 .. 140 %
        const auto comb = smooth ((c - 0.13f) / 0.47f);       // 110 .. 145 %
        const auto push = smooth ((c - 0.33f) / 0.67f);       // 125 .. 175 %

        if (std::abs (push - liftFor) > 0.01f)
        {
            liftFor = push;
            const auto lc = design::peaking (220.0, 0.8, 5.0 * push, sampleRate);
            for (auto& f : lift) f.c = lc;
        }

        // Pushed mic: a fast compressor on the sum (both sides pump together). Its threshold
        // sits at the room's own average level, so it squeezes whatever sticks out at any level.
        float power = 0.0f;
        for (int k = 0; k < ch; ++k)
            power += wet[k] * wet[k] / (float) ch;
        // fast power (attack ~1 ms, release 40 ms: it pumps) against the room's average (0.8 s)
        const auto att = 1.0f - std::exp (-1.0f / (0.001f * (float) sampleRate));
        const auto rel = 1.0f - std::exp (-1.0f / (0.04f * (float) sampleRate));
        const auto avg = 1.0f - std::exp (-1.0f / (0.8f * (float) sampleRate));
        compEnv += (power - compEnv) * (power > compEnv ? att : rel);
        avgPow += (power - avgPow) * avg;
        const auto threshold = std::max (1.0e-8f, 0.8f * avgPow);
        const auto ratio = 1.0f + 15.0f * push;
        const auto gr = compEnv > threshold ? std::pow (threshold / compEnv, 0.5f * (1.0f - 1.0f / ratio)) : 1.0f;

        // Flutter: two parallel walls slapping the sound back and forth (left <-> right)
        const auto fb = 0.88f * flutter;
        // Level: the stage's output follows its input over the last ~0.8 s (two followers with the
        // same time, so the ratio is right from the start and the pumping inside it stays),
        // and lets the falling-apart room come up to 1.5 dB louder at the top.
        const auto keep = 1.0f + c * (juce::jlimit (0.3f, 4.0f, std::sqrt ((avgPow + 1.0e-12f) / (outPow + 1.0e-12f))) * (1.0f + 0.19f * c) - 1.0f);

        // the near wall drifts: the comb sweeps slowly (a boxy flanging room)
        sweepPhase += 0.27 / sampleRate;
        if (sweepPhase >= 1.0) sweepPhase -= 1.0;
        const auto sweep = 1.0 + 0.45 * comb * std::sin (juce::MathConstants<double>::twoPi * sweepPhase);

        float outP = 0.0f;
        float loops[maxCh] {};
        for (int k = 0; k < ch; ++k)
        {
            const auto fl = read (buf[(size_t) k], flutterLen[k]);
            loopLp[k] += (fl - loopLp[k]) * 0.6f;
            loops[k] = loopLp[k];
        }
        for (int k = 0; k < ch; ++k)
        {
            auto& b = buf[(size_t) k];
            const auto x = wet[k];
            const auto other = ch > 1 ? loops[1 - k] : loops[k];

            // Close-wall comb: a near reflection of what's in the line, its distance drifting
            const auto cm = read (b, juce::jlimit (2.0, (double) bufLen - 3.0, combLen[k] * sweep));

            b[(size_t) write] = x + fb * std::tanh (other);   // bounded however hard it's pushed

            auto y = x + 1.1f * flutter * loops[k];
            y += 0.95f * comb * cm;

            if (push > 0.0f)
            {
                // Preamp: compressor makeup, low-mid lift, a hard lopsided drive (DC blocked).
                y *= 1.0f + push * (gr * (1.0f + 1.0f * push) - 1.0f);
                y = (float) lift[k].process (y);
                const auto rms = std::sqrt (avgPow) + 1.0e-6f;
                const auto drive = (1.0f + 11.0f * push) / (4.0f * rms);   // drive relative to the room's level
                const auto u = drive * y;
                auto sat = std::tanh (u + 0.35f * push * u * u) * (4.0f * rms) / (1.0f + 2.0f * push);
                if (push > 0.6f)   // the top: the preamp clips flat on one side
                    sat = juce::jlimit (-2.2f * rms, 1.4f * rms * (3.0f - 2.0f * push) + 0.6f * rms, sat);
                y = y + push * (sat - y);
                dcOut[k] = y - dcIn[k] + 0.9995f * dcOut[k];
                dcIn[k] = y;
                y = dcOut[k];
            }
            outP += y * y / (float) ch;
            wet[k] = y * keep;
        }
        write = (write + 1) % bufLen;
        outPow += (outP - outPow) * avg;
    }

private:
    static constexpr int maxCh = 2;
    static float smooth (float x) { x = juce::jlimit (0.0f, 1.0f, x); return x * x * (3.0f - 2.0f * x); }

    float read (const std::vector<float>& b, double samplesAgo) const
    {
        auto pos = (double) write - samplesAgo;
        while (pos < 0) pos += bufLen;
        const auto i0 = (int) pos % bufLen, i1 = (i0 + 1) % bufLen;
        const auto f = (float) (pos - std::floor (pos));
        return b[(size_t) i0] + f * (b[(size_t) i1] - b[(size_t) i0]);
    }

    double sampleRate = 44100.0;
    int chans = 2, bufLen = 64, write = 0;
    std::vector<float> buf[maxCh];
    double flutterLen[maxCh] { 1000.0, 1000.0 }, combLen[maxCh] { 70.0, 70.0 };
    float loopLp[maxCh] {}, dcIn[maxCh] {}, dcOut[maxCh] {};
    Biquad lift[maxCh];
    float liftFor = -1.0f;
    float compEnv = 0.0f, avgPow = 0.0f, outPow = 0.0f;
    double sweepPhase = 0.0;
    bool wasActive = false;
    juce::SmoothedValue<float> amountSm;
};

} // namespace room
