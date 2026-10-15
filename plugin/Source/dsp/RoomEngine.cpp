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
    constexpr double kFreezeFadeS = 0.15;
    constexpr double kGrainS = 0.4;         // freeze loop grain length
    constexpr double kFreezeHoldS = 1.0;    // freeze holds the most recent second
    constexpr float kDuckDepth = 8.0f;      // how hard full Ducking pulls the room down
    constexpr double kDuckAttackS = 0.005;
}

RoomEngine::RoomEngine() = default;

void RoomEngine::prepare (double sr, int maxBlockSize, int numChannels)
{
    sampleRate = sr;
    numChans = juce::jlimit (1, maxChannels, numChannels);

    convolution.prepare (sr, numChans, maxIrSeconds);
    tonalBuf.setSize (numChans, maxBlockSize);
    spatialBuf.setSize (numChans, maxBlockSize);

    balanceSm.reset (sr, kSmoothingS);
    mixSm.reset (sr, kSmoothingS);
    gainSm.reset (sr, kSmoothingS);
    roomFade.reset (sr, 0.2);

    preDelayBuf.setSize (numChans, (int) (maxPreDelayS * sr) + maxBlockSize + 4);
    preDelaySm.reset (sr, 0.1);

    const auto histLen = (int) (freezeLengthS * sr);
    wetHistory.setSize (numChans, histLen);
    frozen.setSize (numChans, histLen);
    freezeMix.reset (sr, kFreezeFadeS);
    grainLen = std::max (16, (int) (kGrainS * sr));
    grainWindow.resize ((size_t) grainLen);
    for (int i = 0; i < grainLen; ++i)   // sine window: two at 50 % overlap keep the power constant
        grainWindow[(size_t) i] = (float) std::sin (juce::MathConstants<double>::pi * (i + 0.5) / grainLen);
    reset();
}

void RoomEngine::reset()
{
    convolution.reset();
    for (auto& chan : eq) for (auto& f : chan) f.reset();
    for (auto& chan : res) for (auto& f : chan) f.reset();
    preDelayBuf.clear();
    preDelayWrite = 0;
    wetHistory.clear();
    historyWrite = 0;
    duckEnv = 0.0f;
    frozenActive = false;
    freezeMix.setCurrentAndTargetValue (0.0f);
}

int RoomEngine::nextGrainStart()
{
    const auto range = std::max (1, frozen.getNumSamples() - grainLen - freezeStart);
    return juce::jlimit (0, std::max (0, frozen.getNumSamples() - grainLen), freezeStart + rng.nextInt (range));
}

float RoomEngine::freezeGrainSample (int ch)
{
    // Two grains, half a grain apart, each reading from a random place in the
    // frozen audio. A reverb tail is noise-like, so this sounds continuous.
    // Grains never cross the end of the frozen audio (where the newest sample
    // meets the oldest), so there is no seam to click on.
    float out = 0.0f;
    for (auto& g : grains)
        out += frozen.getSample (ch, g.start + g.pos) * grainWindow[(size_t) g.pos];
    return out;
}

void RoomEngine::submitRoom (juce::AudioBuffer<float>&& ir, double irRate, const ToneSettings& newTone)
{
    // All the heavy work happens here, off the audio thread: match the
    // sample rate, cap the length, and transform the IR into partitions.
    const auto sr = sampleRate;
    if (std::abs (irRate - sr) > 0.5 && ir.getNumSamples() > 0)
    {
        const auto ratio = irRate / sr;
        juce::AudioBuffer<float> resampled (ir.getNumChannels(), std::max (1, (int) std::floor ((ir.getNumSamples() - 4) / ratio)));
        for (int c = 0; c < ir.getNumChannels(); ++c)
        {
            juce::LagrangeInterpolator interp;
            interp.process (ratio, ir.getReadPointer (c), resampled.getWritePointer (c), resampled.getNumSamples());
        }
        ir = std::move (resampled);
    }
    const auto maxLen = (int) (maxIrSeconds * sr);
    if (ir.getNumSamples() > maxLen)
        ir.setSize (ir.getNumChannels(), maxLen, true);
    auto kernel = PartitionedConvolver::makeKernel (ir, numChans);

    const juce::SpinLock::ScopedLockType lock (pendingLock);
    pendingKernel = std::move (kernel);   // any old, unused pending kernel is freed here, off the audio thread
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

    // Wait-free: the kernel is ready; the convolver crossfades to it stage by stage.
    convolution.setKernel (std::move (pendingKernel));
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

void RoomEngine::process (juce::AudioBuffer<float>& buffer, float balance, float mix, float outputGainDb,
                          const RoomFx& fx)
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

    // --- 2. spatial path: pre-delay, then the synthetic IR --------------------
    preDelaySm.setTargetValue ((float) (juce::jlimit (0.0, maxPreDelayS * 1000.0, (double) fx.preDelayMs) * 0.001 * sampleRate));
    {
        const auto len = preDelayBuf.getNumSamples();
        for (int i = 0; i < n; ++i)
        {
            const auto d = preDelaySm.getNextValue();
            const auto readPos = (double) preDelayWrite - d;
            auto r0 = (int) std::floor (readPos);
            const auto frac = (float) (readPos - r0);
            r0 = ((r0 % len) + len) % len;
            const auto r1 = (r0 + 1) % len;
            for (int ch = 0; ch < chans; ++ch)
            {
                preDelayBuf.setSample (ch, preDelayWrite, tonalBuf.getSample (ch, i));
                // read between two samples so changing the time glides instead of clicking
                const auto a = preDelayBuf.getSample (ch, r0), b = preDelayBuf.getSample (ch, r1);
                spatialBuf.setSample (ch, i, d < 0.5f ? tonalBuf.getSample (ch, i) : a + frac * (b - a));
            }
            preDelayWrite = (preDelayWrite + 1) % len;
        }
    }
    convolution.process (spatialBuf.getArrayOfWritePointers(), chans, n);

    // --- Freeze: on the way in, grab the last couple of seconds of wet sound ---
    if (fx.freeze && ! frozenActive)
    {
        const auto len = wetHistory.getNumSamples();
        for (int ch = 0; ch < chans; ++ch)
            for (int k = 0; k < len; ++k)   // oldest first
                frozen.setSample (ch, k, wetHistory.getSample (ch, (historyWrite + k) % len));
        // Hold what's ringing now: only the most recent second, and skip any
        // silence at its start (e.g. freezing right after a short hit).
        freezeStart = std::max (0, len - (int) (kFreezeHoldS * sampleRate));
        {
            const auto chunk = std::max (1, (int) (0.05 * sampleRate));
            float loudest = 0.0f;
            for (int k = freezeStart; k + chunk <= len; k += chunk)
                loudest = std::max (loudest, frozen.getRMSLevel (0, k, chunk));
            while (freezeStart + chunk <= len - grainLen && frozen.getRMSLevel (0, freezeStart, chunk) < loudest * 0.03f)
                freezeStart += chunk;
        }
        grains[0] = { nextGrainStart(), 0 };
        grains[1] = { nextGrainStart(), grainLen / 2 };
        frozenActive = true;
    }
    freezeMix.setTargetValue (fx.freeze ? 1.0f : 0.0f);

    // Ducking: release time sets how fast the room swells back.
    const auto attack = (float) std::exp (-1.0 / (kDuckAttackS * sampleRate));
    const auto release = (float) std::exp (-1.0 / (juce::jlimit (0.05, 1.0, fx.duckReleaseMs * 0.001) * sampleRate));
    const auto duckAmount = juce::jlimit (0.0f, 1.0f, fx.duck);

    // --- 3 + 4. blend by balance (= wet), freeze, duck, then wet/dry ----------
    const auto histLen = wetHistory.getNumSamples();
    for (int i = 0; i < n; ++i)
    {
        const auto b = balanceSm.getNextValue();
        const auto m = mixSm.getNextValue() * roomFade.getNextValue();
        const auto g = gainSm.getNextValue();
        const auto fz = freezeMix.getNextValue();

        float level = 0.0f;
        for (int ch = 0; ch < chans; ++ch)
            level = std::max (level, std::abs (buffer.getSample (ch, i)));
        duckEnv = level > duckEnv ? attack * duckEnv + (1.0f - attack) * level
                                  : release * duckEnv + (1.0f - release) * level;
        const auto duckGain = 1.0f / (1.0f + kDuckDepth * duckAmount * duckEnv);

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* io = buffer.getWritePointer (ch);
            auto wet = (1.0f - b) * tonalBuf.getSample (ch, i)
                     + b * kSpatialGain * spatialBuf.getSample (ch, i);
            if (! frozenActive)
                wetHistory.setSample (ch, historyWrite, wet);
            else
                wet = (1.0f - fz) * wet + fz * freezeGrainSample (ch);
            io[i] = ((1.0f - m) * io[i] + m * wet * duckGain) * g;
        }

        if (frozenActive)
        {
            for (auto& gr : grains)
                if (++gr.pos >= grainLen)
                    gr = { nextGrainStart(), 0 };
            // Fully faded back out: forget the frozen loop.
            if (! fx.freeze && ! freezeMix.isSmoothing())
                frozenActive = false;
        }
        else
            historyWrite = (historyWrite + 1) % histLen;
    }
}

} // namespace room
