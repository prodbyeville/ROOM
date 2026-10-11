#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Hands the plugin's final output (mono) from the audio thread to the UI
// without locks. The audio thread only copies samples; the UI does the FFT.

namespace room
{

class SpectrumTap
{
public:
    static constexpr int capacity = 1 << 15;

    /** Audio thread. Drops samples if the UI isn't reading (e.g. editor closed). */
    void push (const juce::AudioBuffer<float>& buffer)
    {
        const auto n = buffer.getNumSamples();
        const auto chans = buffer.getNumChannels();
        if (chans == 0 || n == 0)
            return;

        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        auto write = [&] (int dest, int count, int srcOffset)
        {
            for (int i = 0; i < count; ++i)
            {
                float v = 0.0f;
                for (int ch = 0; ch < chans; ++ch)
                    v += buffer.getSample (ch, srcOffset + i);
                data[(size_t) (dest + i)] = v / (float) chans;
            }
        };
        write (s1, n1, 0);
        write (s2, n2, n1);
        fifo.finishedWrite (n1 + n2);
    }

    /** UI thread. Reads up to maxSamples into dest, returns how many were read. */
    int pull (float* dest, int maxSamples)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (maxSamples, s1, n1, s2, n2);
        std::copy_n (data.begin() + s1, n1, dest);
        std::copy_n (data.begin() + s2, n2, dest + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

private:
    juce::AbstractFifo fifo { capacity };
    std::array<float, capacity> data {};
};

} // namespace room
