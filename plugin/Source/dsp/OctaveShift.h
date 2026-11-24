#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

// PITCH: an octave blended into the room. `amount` -1..1: the middle is the room as it is,
// right blends in the room an octave up, left an octave down; the ends are all octave.
// Two read heads per direction sweep through a short history at 2x (up) or 0.5x (down)
// speed and crossfade with sine windows (constant power), the same heads as the shimmer.
// At 0 it is exactly bypassed (bit-identical). No allocation in processSample().

namespace room
{

class OctaveShift
{
public:
    void prepare (double sr, int channels)
    {
        sampleRate = sr;
        chans = juce::jlimit (1, maxCh, channels);
        win = std::max (64, (int) (0.06 * sr));
        histLen = 2 * win + 8;
        for (auto& h : hist) h.assign ((size_t) histLen, 0.0f);
        amountSm.reset (sr, 0.05);
        amountSm.setCurrentAndTargetValue (0.0f);
        reset();
    }

    void reset()
    {
        for (auto& h : hist) std::fill (h.begin(), h.end(), 0.0f);
        write = 0;
        upPhase = downPhase = 0.0;
    }

    void setAmount (float a)
    {
        if (wasActive && isIdle())
        {
            reset();
            wasActive = false;
        }
        amountSm.setTargetValue (juce::jlimit (-1.0f, 1.0f, a));
    }

    bool isIdle() const { return amountSm.getCurrentValue() == 0.0f && ! amountSm.isSmoothing(); }

    void processSample (float* wet, int numCh)
    {
        const auto ch = std::min (numCh, chans);
        const auto a = amountSm.getNextValue();
        for (int c = 0; c < ch; ++c)
            hist[(size_t) c][(size_t) write] = wet[c];
        if (a != 0.0f)
        {
            wasActive = true;
            // up: the delay shrinks by one sample per sample (2x); down: it grows by half (0.5x)
            upPhase += 1.0;
            if (upPhase >= win) upPhase -= win;
            downPhase += 0.5;
            if (downPhase >= win) downPhase -= win;
            const auto amt = std::abs (a);
            const auto halfPi = juce::MathConstants<float>::halfPi;
            const auto dryG = std::cos (amt * halfPi), octG = std::sin (amt * halfPi);
            const auto up = a > 0.0f;
            const auto p0 = up ? upPhase : downPhase;
            const auto p1 = std::fmod (p0 + win * 0.5, (double) win);
            const auto pi = juce::MathConstants<double>::pi;
            const auto w0 = (float) std::sin (pi * p0 / win), w1 = (float) std::sin (pi * p1 / win);
            for (int c = 0; c < ch; ++c)
            {
                const auto o = up ? read (c, win - p0) * w0 + read (c, win - p1) * w1
                                  : read (c, p0 + 1.0) * w0 + read (c, p1 + 1.0) * w1;
                wet[c] = dryG * wet[c] + octG * o;
            }
        }
        write = (write + 1) % histLen;
    }

private:
    static constexpr int maxCh = 2;

    float read (int c, double samplesAgo) const
    {
        auto pos = (double) write - samplesAgo;
        while (pos < 0) pos += histLen;
        const auto i0 = (int) pos % histLen, i1 = (i0 + 1) % histLen;
        const auto f = (float) (pos - std::floor (pos));
        const auto& h = hist[(size_t) c];
        return h[(size_t) i0] + f * (h[(size_t) i1] - h[(size_t) i0]);
    }

    double sampleRate = 44100.0;
    int chans = 2, win = 64, histLen = 136, write = 0;
    std::vector<float> hist[maxCh];
    double upPhase = 0.0, downPhase = 0.0;
    bool wasActive = false;
    juce::SmoothedValue<float> amountSm;
};

} // namespace room
