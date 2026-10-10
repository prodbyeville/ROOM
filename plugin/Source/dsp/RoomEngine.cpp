#include "RoomEngine.h"

namespace room
{

namespace
{
    // Level trim for the reverb path relative to the tonal path (+4 dB).
    // Measured against the Python renders: without it the reverb sat about
    // 4 dB under the tonal path on the test rooms.
    constexpr float kSpatialGain = 1.6f;
    constexpr double kSmoothingS = 0.05;
}

RoomEngine::RoomEngine() = default;

void RoomEngine::prepare (double sr, int maxBlockSize, int numChannels)
{
    sampleRate = sr;
    numChans = juce::jlimit (1, maxChannels, numChannels);

    convolution.prepare ({ sr, (juce::uint32) maxBlockSize, (juce::uint32) numChans });
    tonalBuf.setSize (numChans, maxBlockSize);
    spatialBuf.setSize (numChans, maxBlockSize);

    balanceSm.reset (sr, kSmoothingS);
    mixSm.reset (sr, kSmoothingS);
    gainSm.reset (sr, kSmoothingS);
    roomFade.reset (sr, 0.2);
    reset();
}

void RoomEngine::reset()
{
    convolution.reset();
    for (auto& chan : eq) for (auto& f : chan) f.reset();
    for (auto& chan : res) for (auto& f : chan) f.reset();
}

void RoomEngine::submitRoom (juce::AudioBuffer<float>&& ir, double irRate, const ToneSettings& newTone)
{
    const juce::SpinLock::ScopedLockType lock (pendingLock);
    pendingIR = std::move (ir);   // any old, unused pending IR is freed here, off the audio thread
    pendingIRRate = irRate;
    pendingTone = newTone;
    hasPending = true;
    pendingClear = false;
}

void RoomEngine::clearRoom()
{
    pendingClear = true;
}

void RoomEngine::applyPending()
{
    if (pendingClear.exchange (false))
    {
        roomLoaded = false;
        roomActive = false;
    }

    const juce::SpinLock::ScopedTryLockType lock (pendingLock);
    if (! lock.isLocked() || ! hasPending)
        return;

    // Wait-free: the convolution takes ownership and prepares the IR on its
    // own background thread, then crossfades to it.
    convolution.loadImpulseResponse (std::move (pendingIR), pendingIRRate,
                                     numChans > 1 ? juce::dsp::Convolution::Stereo::yes
                                                  : juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::no,
                                     juce::dsp::Convolution::Normalise::no);
    tone = pendingTone;
    hasPending = false;

    if (! roomLoaded)
    {
        // Fade the effect in rather than jumping from dry to wet.
        roomFade.setCurrentAndTargetValue (0.0f);
        roomFade.setTargetValue (1.0f);
        for (auto& chan : eq) for (auto& f : chan) f.reset();
        for (auto& chan : res) for (auto& f : chan) f.reset();
    }
    roomLoaded = true;
    roomActive = true;
}

void RoomEngine::process (juce::AudioBuffer<float>& buffer, float balance, float mix, float outputGainDb)
{
    applyPending();

    const auto n = buffer.getNumSamples();
    const auto chans = std::min (buffer.getNumChannels(), numChans);
    balanceSm.setTargetValue (balance);
    mixSm.setTargetValue (mix);
    gainSm.setTargetValue (juce::Decibels::decibelsToGain (outputGainDb));

    if (! roomLoaded || n > tonalBuf.getNumSamples())
    {
        // No room loaded yet: pass through (with output gain).
        for (int i = 0; i < n; ++i)
        {
            const auto g = gainSm.getNextValue();
            for (int ch = 0; ch < chans; ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
        return;
    }

    // --- 1. tonal path: EQ in series, then resonators added in parallel ------
    for (int ch = 0; ch < chans; ++ch)
    {
        const auto* in = buffer.getReadPointer (ch);
        auto* out = tonalBuf.getWritePointer (ch);
        for (int k = 0; k < tone.numEq; ++k)         eq[ch][k].c = tone.eq[k];
        for (int k = 0; k < tone.numResonators; ++k) res[ch][k].c = tone.resonator[k];

        for (int i = 0; i < n; ++i)
        {
            double x = in[i];
            for (int k = 0; k < tone.numEq; ++k)
                x = eq[ch][k].process (x);
            double y = x;
            for (int k = 0; k < tone.numResonators; ++k)
                y += tone.resonatorExtra[k] * res[ch][k].process (x);
            out[i] = (float) (y * tone.tonalGain);
        }
    }

    // --- 2. spatial path: the tonal signal through the synthetic IR -----------
    for (int ch = 0; ch < chans; ++ch)
        spatialBuf.copyFrom (ch, 0, tonalBuf, ch, 0, n);
    {
        juce::dsp::AudioBlock<float> block (spatialBuf.getArrayOfWritePointers(), (size_t) chans, (size_t) n);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // --- 3 + 4. blend by balance, then wet/dry --------------------------------
    for (int i = 0; i < n; ++i)
    {
        const auto b = balanceSm.getNextValue();
        const auto m = mixSm.getNextValue() * roomFade.getNextValue();
        const auto g = gainSm.getNextValue();
        for (int ch = 0; ch < chans; ++ch)
        {
            auto* io = buffer.getWritePointer (ch);
            const auto wet = (1.0f - b) * tonalBuf.getSample (ch, i)
                           + b * kSpatialGain * spatialBuf.getSample (ch, i);
            io[i] = ((1.0f - m) * io[i] + m * wet) * g;
        }
    }
}

} // namespace room
