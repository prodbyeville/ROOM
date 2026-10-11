#pragma once

#include "Biquad.h"
#include <juce_audio_basics/juce_audio_basics.h>

// SPACE: stereo width, applied last.
//
//   0 = mono, 1 = untouched (exact pass-through), 2 = extra wide.
//
// Mid/side: the side signal (the difference between left and right) is
// scaled by the knob. When widening past 100 %, the side signal below
// ~120 Hz is gradually faded out (fully mono at 200 %), so the low end gets
// tighter as the top gets wider. The overall level is trimmed so "wider"
// doesn't just mean "louder".

namespace room
{

class StereoWidth
{
public:
    void prepare (double sampleRate)
    {
        // 4th-order Butterworth high-pass: the side signal without its lows.
        sr = sampleRate;
        hp[0].c = design::highpass (kBassMonoHz, design::kButter4Q[0], sr);
        hp[1].c = design::highpass (kBassMonoHz, design::kButter4Q[1], sr);
        width.reset (sr, 0.05);
        width.setCurrentAndTargetValue (1.0f);
        reset();
    }

    void reset() { hp[0].reset(); hp[1].reset(); }

    /** Audio thread. amount: 0 (mono) .. 1 (untouched) .. 2 (wide). */
    void process (juce::AudioBuffer<float>& buffer, float amount)
    {
        width.setTargetValue (juce::jlimit (0.0f, 2.0f, amount));
        if (buffer.getNumChannels() < 2)
            return;

        const auto n = buffer.getNumSamples();
        // Exact pass-through at 100 % (also keeps the low-band filter quiet).
        if (! width.isSmoothing() && std::abs (width.getTargetValue() - 1.0f) < 1e-4f)
        {
            reset();
            return;
        }

        auto* l = buffer.getWritePointer (0);
        auto* r = buffer.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const auto w = width.getNextValue();
            const double mid = 0.5 * ((double) l[i] + r[i]);
            const double side = 0.5 * ((double) l[i] - r[i]);

            const auto sideNoLows = hp[1].process (hp[0].process (side));

            double s = w * side, trim = 1.0;
            if (w > 1.0f)
            {
                // Past 100 %, crossfade the side signal toward its high-passed
                // version: at 200 % everything below ~120 Hz is mono.
                const auto a = (double) w - 1.0;
                s = w * ((1.0 - a) * side + a * sideNoLows);
                trim = 1.0 / std::sqrt (1.0 + kSideShare * ((double) w * w - 1.0));
            }
            l[i] = (float) ((mid + s) * trim);
            r[i] = (float) ((mid - s) * trim);
        }
    }

private:
    static constexpr double kBassMonoHz = 120.0;
    // Rough share of energy in the side signal for typical stereo material;
    // sets how much the level is trimmed when widening.
    static constexpr double kSideShare = 0.3;

    double sr = 48000.0;
    Biquad hp[2];
    juce::SmoothedValue<float> width;
};

} // namespace room
