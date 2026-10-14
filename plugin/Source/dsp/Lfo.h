#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// One LFO. Output is bipolar, -1 .. +1.
//   Rate: free (Hz) or synced to the host tempo (a note length).
//   When synced and the host is playing, the phase is locked to the song
//   position, so it lines up with the beat every time you press play.

namespace room
{

class Lfo
{
public:
    enum class Shape { sine, triangle, sawUp, sawDown, square, sampleHold, smoothRandom };

    static const juce::StringArray& shapeNames()
    {
        static const juce::StringArray s { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S & H", "Smooth Random" };
        return s;
    }

    /** Synced note lengths and their length in quarter notes (beats). */
    static const juce::StringArray& divisionNames()
    {
        static const juce::StringArray s { "1/32", "1/16", "1/8", "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars" };
        return s;
    }
    static double divisionBeats (int index)
    {
        static const double b[] = { 0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 };
        return b[juce::jlimit (0, 8, index)];
    }

    void reset (double newPhase = 0.0)
    {
        phase = newPhase;
        held = next = rng.nextFloat() * 2.0f - 1.0f;
        next = rng.nextFloat() * 2.0f - 1.0f;
    }

    /** Current value for this shape at the current phase. */
    float value (Shape shape) const
    {
        const auto p = (float) phase;
        switch (shape)
        {
            case Shape::sine:         return std::sin (juce::MathConstants<float>::twoPi * p);
            case Shape::triangle:     return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;
            case Shape::sawUp:        return 2.0f * p - 1.0f;
            case Shape::sawDown:      return 1.0f - 2.0f * p;
            case Shape::square:       return p < 0.5f ? 1.0f : -1.0f;
            case Shape::sampleHold:   return held;
            case Shape::smoothRandom:
            {
                const auto t = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * p);   // cosine ease
                return held + (next - held) * t;
            }
        }
        return 0.0f;
    }

    /** Move forward by `samples` at `hz` cycles per second. */
    void advance (int samples, double hz, double sampleRate)
    {
        phase += hz * samples / sampleRate;
        while (phase >= 1.0)
        {
            phase -= 1.0;
            newCycle();
        }
    }

    /** Lock to the host's song position (quarter notes). */
    void syncTo (double ppq, double beatsPerCycle)
    {
        const auto cycles = ppq / beatsPerCycle;
        const auto cycleIndex = (juce::int64) std::floor (cycles);
        if (cycleIndex != lastCycle)
        {
            lastCycle = cycleIndex;
            newCycle();
        }
        phase = cycles - std::floor (cycles);
    }

    double getPhase() const { return phase; }

private:
    void newCycle()
    {
        held = next;
        next = rng.nextFloat() * 2.0f - 1.0f;
    }

    double phase = 0.0;
    float held = 0.0f, next = 0.0f;
    juce::int64 lastCycle = -1;
    juce::Random rng { 12345 };
};

} // namespace room
