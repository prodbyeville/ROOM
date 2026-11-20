#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include "Biquad.h"

// The room's own recording artifacts, Intensity 100..180 % (`amount` 0..1 across it).
// On the wet (room) sound, before the shimmer / bloom of 180..200 % (WetFx.h).
// Each artifact eases in over its own part of the range; all are full at 180 %:
//
//   ringing notes    100..140 %  the room's resonators ring louder and longer (RoomEngine,
//                                tonal path: ringAmount())
//   flutter echo     110..160 %  a short, dark feedback echo between two parallel walls;
//                                its period comes from the room (12..45 ms)
//   close-wall comb  120..170 %  a 0.8..3 ms reflection off a wall right by the mic:
//                                hollow, boxy colour
//   pushed mic       140..180 %  a preamp driven too hard: a fast compressor pumping the
//                                room, a lopsided saturation and a low-mid lift
//
// The stage keeps the room's level (its output follows its input over the last 0.8 s, and
// the compressor's threshold follows the room's own average), so turning it up changes the
// character, not the loudness. At 0 it is exactly bypassed (bit-identical).
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
        bufLen = std::max (64, (int) (0.06 * sr) + 4);
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

    /** How much the room's notes ring (0..1) for an amount: 100..140 %. */
    static float ringAmount (float amount) { return smooth (amount / 0.5f); }

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

        const auto flutter = smooth ((c - 0.125f) / 0.625f);   // 110 .. 160 %
        const auto comb = smooth ((c - 0.25f) / 0.625f);       // 120 .. 170 %
        const auto push = smooth ((c - 0.5f) / 0.5f);          // 140 .. 180 %

        if (std::abs (push - liftFor) > 0.01f)
        {
            liftFor = push;
            const auto lc = design::peaking (200.0, 0.8, 3.0 * push, sampleRate);
            for (auto& f : lift) f.c = lc;
        }

        // Pushed mic: a fast compressor on the sum (both sides pump together). Its threshold
        // sits at the room's own average level, so it squeezes whatever sticks out at any level.
        float level = 0.0f, power = 0.0f;
        for (int k = 0; k < ch; ++k)
        {
            level = std::max (level, std::abs (wet[k]));
            power += wet[k] * wet[k] / (float) ch;
        }
        juce::ignoreUnused (level);
        // fast power (attack ~1 ms, release 120 ms) against the room's average (0.8 s)
        const auto att = 1.0f - std::exp (-1.0f / (0.001f * (float) sampleRate));
        const auto rel = 1.0f - std::exp (-1.0f / (0.12f * (float) sampleRate));
        const auto avg = 1.0f - std::exp (-1.0f / (0.8f * (float) sampleRate));
        compEnv += (power - compEnv) * (power > compEnv ? att : rel);
        avgPow += (power - avgPow) * avg;
        const auto threshold = std::max (1.0e-8f, 1.3f * 1.3f * avgPow);
        const auto ratio = 1.0f + 7.0f * push;
        const auto gr = compEnv > threshold ? std::pow (threshold / compEnv, 0.5f * (1.0f - 1.0f / ratio)) : 1.0f;

        const auto fb = 0.65f * flutter;
        // Level: the stage's output follows its input over the last ~0.8 s (two followers with the
        // same time, so the ratio is right from the start and the pumping inside it stays).
        const auto keep = 1.0f + c * (juce::jlimit (0.4f, 3.0f, std::sqrt ((avgPow + 1.0e-12f) / (outPow + 1.0e-12f))) - 1.0f);

        float outP = 0.0f;
        for (int k = 0; k < ch; ++k)
        {
            auto& b = buf[(size_t) k];
            const auto x = wet[k];

            // Flutter echo: a short feedback delay, darker each time round.
            const auto fl = read (b, flutterLen[k]);
            loopLp[k] += (fl - loopLp[k]) * 0.35f;

            // Close-wall comb: a single near reflection of what's in the line.
            const auto cm = read (b, combLen[k]);

            b[(size_t) write] = x + fb * loopLp[k];

            auto y = x + 0.85f * flutter * loopLp[k];
            y += 0.75f * comb * cm;

            if (push > 0.0f)
            {
                // Preamp: compressor makeup, low-mid lift, lopsided drive (DC blocked).
                y *= 1.0f + push * (gr * (1.0f + 0.4f * push) - 1.0f);
                y = (float) lift[k].process (y);
                const auto drive = 1.0f + 4.0f * push;
                const auto sat = std::tanh (drive * y + 0.25f * push * y * y) / drive;
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
    bool wasActive = false;
    juce::SmoothedValue<float> amountSm;
};

} // namespace room
