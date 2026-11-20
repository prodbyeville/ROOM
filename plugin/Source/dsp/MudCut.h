#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include "Biquad.h"

// MUD (Advanced, the finish box): tames the boxy low mids (about 280..450 Hz).
// A wide cut at 350 Hz: `amount` 0..1 sets a steady cut of up to 5 dB, plus up to 4 dB more
// while that band builds up against the rest of the sound (a dynamic cut, so it only digs
// in when it's actually boxy). At 0 it is exactly bypassed. No allocation in process().

namespace room
{

class MudCut
{
public:
    void prepare (double sr)
    {
        sampleRate = sr;
        band.c = design::resonator (350.0, 1.6, sr);
        amountSm.reset (sr, 0.05);
        amountSm.setCurrentAndTargetValue (0.0f);
        reset();
    }

    void reset()
    {
        band.reset();
        for (auto& f : cut) f.reset();
        bandPow = totalPow = 1.0e-9f;
        gainDb = 0.0f;
    }

    void setAmount (float a)
    {
        if (wasActive && isIdle())
        {
            reset();
            wasActive = false;
        }
        amountSm.setTargetValue (juce::jlimit (0.0f, 1.0f, a));
    }

    bool isIdle() const { return amountSm.getCurrentValue() <= 0.0f && ! amountSm.isSmoothing(); }

    /** How far it is cutting right now (dB, <= 0), for tests and the UI. */
    float currentCutDb() const { return gainDb; }

    void process (juce::AudioBuffer<float>& buffer, int numSamples)
    {
        if (isIdle())
            return;
        wasActive = true;
        const auto chans = std::min (buffer.getNumChannels(), maxCh);
        const auto coeff = 1.0f - std::exp (-1.0f / (0.1f * (float) sampleRate));
        for (int start = 0; start < numSamples; start += chunk)
        {
            const auto len = std::min (chunk, numSamples - start);
            const auto amt = amountSm.getCurrentValue();
            amountSm.skip (len);

            // how boxy it is: the 350 Hz band's share of the whole (about 0.12 for typical music)
            const auto share = bandPow / std::max (1.0e-9f, totalPow);
            const auto excess = juce::jlimit (0.0f, 1.0f, (share - 0.12f) / 0.2f);
            const auto want = -amt * (5.0f + 4.0f * excess);
            gainDb += (want - gainDb) * 0.2f;
            const auto c = design::peaking (350.0, 1.1, gainDb, sampleRate);
            for (auto& f : cut) f.c = c;

            for (int i = start; i < start + len; ++i)
            {
                float mono = 0.0f;
                for (int ch = 0; ch < chans; ++ch)
                    mono += buffer.getSample (ch, i);
                mono /= (float) std::max (1, chans);
                const auto b = (float) band.process (mono);
                bandPow += (b * b - bandPow) * coeff;
                totalPow += (mono * mono - totalPow) * coeff;
                for (int ch = 0; ch < chans; ++ch)
                    buffer.setSample (ch, i, (float) cut[ch].process (buffer.getSample (ch, i)));
            }
        }
    }

private:
    static constexpr int maxCh = 2;
    static constexpr int chunk = 32;
    double sampleRate = 44100.0;
    Biquad band, cut[maxCh];
    float bandPow = 1.0e-9f, totalPow = 1.0e-9f, gainDb = 0.0f;
    bool wasActive = false;
    juce::SmoothedValue<float> amountSm;
};

} // namespace room
