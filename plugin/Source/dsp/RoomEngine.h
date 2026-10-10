#pragma once

#include "RoomSynth.h"
#include <juce_dsp/juce_dsp.h>

// REAL-TIME PROCESSING CHAIN (C++ port of room_engine/process.py)
//
//   input
//     -> tone-match EQ -> resonators          (tonal path)
//     -> convolve with synthetic IR           (spatial path, fed by tonal)
//     -> blend tonal / spatial by balance
//     -> wet/dry mix
//     -> output gain
//
// New rooms are built on a background thread and handed over with
// submitRoom(). The audio thread picks them up without ever waiting.

namespace room
{

class RoomEngine
{
public:
    RoomEngine();

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    /** Call from any thread EXCEPT the audio thread. Takes ownership of ir. */
    void submitRoom (juce::AudioBuffer<float>&& ir, double irSampleRate, const ToneSettings& tone);

    /** Forget the current room: the engine passes audio through untouched. */
    void clearRoom();

    /** Audio thread. balance: 0 tonal .. 1 spatial, mix: 0 dry .. 1 wet. */
    void process (juce::AudioBuffer<float>& buffer, float balance, float mix, float outputGainDb);

    /** True once a submitted room is actually running on the audio thread. */
    bool isRoomActive() const { return roomActive.load(); }

private:
    void applyPending();

    static constexpr int maxChannels = 2;

    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 512 } };

    // Tonal path state, per channel
    ToneSettings tone;
    Biquad eq[maxChannels][ToneSettings::maxEq];
    Biquad res[maxChannels][ToneSettings::maxResonators];

    juce::AudioBuffer<float> tonalBuf, spatialBuf;
    juce::SmoothedValue<float> balanceSm, mixSm, gainSm, roomFade;

    // Handover from the background thread
    juce::SpinLock pendingLock;
    bool hasPending = false;
    juce::AudioBuffer<float> pendingIR;
    double pendingIRRate = 44100.0;
    ToneSettings pendingTone;
    std::atomic<bool> pendingClear { false };

    std::atomic<bool> roomActive { false };
    bool roomLoaded = false;   // audio-thread copy of roomActive
    double sampleRate = 44100.0;
    int numChans = 2;
};

} // namespace room
