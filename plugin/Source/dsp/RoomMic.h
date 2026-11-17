#pragma once

#include "Biquad.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

// SMOOTH (the intensity section): the sound as a mic across the room hears it, after
// Shearwater's ROOM knob. On the whole sound (dry and room). At `amount`:
//
//   attack tuck   the hit is softened: where a fast envelope runs ahead of a slow one
//                 (a transient), the gain dips, up to -10 dB, then comes back in 30-80 ms;
//                 the body of the note is left alone, so it never pumps
//   distance      the top rolls off (a 12 dB/oct low-pass from 20 kHz down to ~6 kHz,
//                 a soft shelf of up to -4 dB from 3 kHz) and the low mids warm a little
//                 (+1.5 dB around 250 Hz), like air and distance
//   reflections   four early reflections (7-23 ms), low, so the attack smears a touch
//   level         a gentle make-up, so turning it up doesn't just get quieter
//
// 0 = off: nothing runs (bit-identical). Buffers are sized in prepare().
// First voicing: to be tuned against Ethan's reference recordings.

namespace room
{

class RoomMic
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        amount.reset (sr, 0.05);
        amount.setCurrentAndTargetValue (0.0f);
        fastA = coeff (0.0005); fastR = coeff (0.008);
        slowA = coeff (0.015);  slowR = coeff (0.12);
        tuckA = coeff (0.0005);
        delayLen = (int) std::ceil (0.025 * sr) + 2;
        for (auto& d : delay) d.assign ((size_t) delayLen, 0.0f);
        reset();
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
        {
            fast[ch] = slow[ch] = 0.0f;
            gain[ch] = 1.0f;
            for (auto& f : filters[ch]) f.reset();
            std::fill (delay[ch].begin(), delay[ch].end(), 0.0f);
        }
        write = 0;
        countdown = 0;
    }

    void setAmount (float a) { amount.setTargetValue (juce::jlimit (0.0f, 1.0f, a)); }
    bool isIdle() const { return amount.getTargetValue() <= 0.0f && ! amount.isSmoothing(); }

    void process (juce::AudioBuffer<float>& buffer, int numSamples)
    {
        const auto chans = std::min (2, buffer.getNumChannels());
        static constexpr float tapS[4] = { 0.0071f, 0.0113f, 0.0167f, 0.0229f };
        static constexpr float tapG[4] = { 0.16f, -0.12f, 0.10f, -0.07f };
        for (int i = 0; i < numSamples; ++i)
        {
            const auto s = amount.getNextValue();
            if (--countdown <= 0)
            {
                countdown = 32;
                updateFilters (s);
            }
            const auto releaseS = 0.03 + 0.05 * (double) s;
            const auto tuckR = coeff (releaseS);
            for (int ch = 0; ch < chans; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                const auto x = d[i];

                // attack tuck: fast vs slow envelope
                const auto ax = std::abs (x);
                fast[ch] = ax > fast[ch] ? fastA * fast[ch] + (1.0f - fastA) * ax : fastR * fast[ch] + (1.0f - fastR) * ax;
                slow[ch] = ax > slow[ch] ? slowA * slow[ch] + (1.0f - slowA) * ax : slowR * slow[ch] + (1.0f - slowR) * ax;
                const auto over = fast[ch] > 1.0e-6f ? juce::jmax (0.0f, 1.0f - slow[ch] / fast[ch]) : 0.0f;   // 0 steady .. 1 sharp hit
                const auto targetGain = juce::Decibels::decibelsToGain (-10.0f * s * over);
                gain[ch] = targetGain < gain[ch] ? tuckA * gain[ch] + (1.0f - tuckA) * targetGain
                                                 : (float) tuckR * gain[ch] + (1.0f - (float) tuckR) * targetGain;
                auto y = x * gain[ch];

                // early reflections
                delay[ch][(size_t) write] = y;
                float er = 0.0f;
                for (int t = 0; t < 4; ++t)
                {
                    auto r = write - (int) (tapS[t] * sampleRate);
                    if (r < 0) r += delayLen;
                    er += tapG[t] * delay[ch][(size_t) r];
                }
                y += s * er;

                // distance: low-pass, high shelf down, low-mid warmth
                double z = y;
                for (auto& f : filters[ch]) z = f.process (z);
                d[i] = (float) z * (1.0f + 0.25f * s);   // gentle make-up
            }
            write = (write + 1) % delayLen;
        }
    }

private:
    float coeff (double seconds) const { return (float) std::exp (-1.0 / (seconds * sampleRate)); }

    void updateFilters (float s)
    {
        const auto lp = juce::jlimit (1000.0, 0.45 * sampleRate, 20000.0 * std::pow (6000.0 / 20000.0, (double) s));
        const BiquadCoeffs c[3] = {
            design::lowpass (lp, 0.707, sampleRate),
            design::highShelf (3000.0, -4.0 * s, sampleRate),
            design::peaking (250.0, 0.8, 1.5 * s, sampleRate),
        };
        for (int ch = 0; ch < 2; ++ch)
            for (int k = 0; k < 3; ++k)
                filters[ch][k].c = c[k];
    }

    double sampleRate = 44100.0;
    juce::SmoothedValue<float> amount;
    float fastA = 0, fastR = 0, slowA = 0, slowR = 0, tuckA = 0;
    float fast[2] {}, slow[2] {}, gain[2] { 1.0f, 1.0f };
    Biquad filters[2][3];
    std::vector<float> delay[2];
    int delayLen = 1, write = 0, countdown = 0;
};

} // namespace room
