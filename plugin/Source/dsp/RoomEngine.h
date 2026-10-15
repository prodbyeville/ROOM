#pragma once

#include "RoomSynth.h"
#include "PartitionedConvolver.h"
#include <juce_dsp/juce_dsp.h>

// REAL-TIME PROCESSING CHAIN (C++ port of room_engine/process.py)
//
//   input
//     -> tone-match EQ -> resonators          (tonal path)
//     -> pre-delay -> synthetic IR            (spatial path, fed by tonal)
//     -> blend tonal / spatial by balance     (= the "wet" room sound)
//     -> freeze (loop the wet sound)          (Advanced)
//     -> ducking (wet dips while you play)    (Advanced)
//     -> wet/dry mix
//     -> output gain
//
// New rooms are built on a background thread and handed over with
// submitRoom(). The audio thread picks them up without ever waiting.

namespace room
{

/** The Advanced-screen settings that act in real time. */
struct RoomFx
{
    float preDelayMs = 0.0f;       // 0 .. 500
    float duck = 0.0f;             // 0 .. 1
    float duckReleaseMs = 250.0f;  // 50 .. 1000
    bool freeze = false;
};

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
    void process (juce::AudioBuffer<float>& buffer, float balance, float mix, float outputGainDb,
                  const RoomFx& fx = {});

    static constexpr double maxPreDelayS = 0.5;
    static constexpr double freezeLengthS = 2.0;

    /** True once a submitted room is actually running on the audio thread. */
    bool isRoomActive() const { return roomActive.load(); }

private:
    void applyPending();

    static constexpr int maxChannels = 2;

    PartitionedConvolver convolution;   // zero latency, steady CPU (see PartitionedConvolver.h)
    static constexpr double maxIrSeconds = 14.0;

    // Tonal path state, per channel
    ToneSettings tone;
    Biquad eq[maxChannels][ToneSettings::maxEq];
    Biquad res[maxChannels][ToneSettings::maxResonators];

    juce::AudioBuffer<float> tonalBuf, spatialBuf;
    juce::SmoothedValue<float> balanceSm, mixSm, gainSm, roomFade;

    // Pre-delay: a delay line in front of the convolution
    juce::AudioBuffer<float> preDelayBuf;
    int preDelayWrite = 0;
    juce::SmoothedValue<float> preDelaySm;       // in samples

    // Ducking: envelope of the dry input
    float duckEnv = 0.0f;

    // Freeze: the last couple of seconds of the wet sound, looped
    juce::AudioBuffer<float> wetHistory, frozen;
    int historyWrite = 0;
    bool frozenActive = false;
    juce::SmoothedValue<float> freezeMix;
    struct Grain { int start = 0; int pos = 0; };
    Grain grains[2];
    int grainLen = 1;
    juce::Random rng;
    std::vector<float> grainWindow;
    float freezeGrainSample (int ch);
    int nextGrainStart();
    int freezeStart = 0;   // where the held part of the frozen audio begins

    // Handover from the background thread
    juce::SpinLock pendingLock;
    bool hasPending = false;
    std::shared_ptr<const PartitionedConvolver::Kernel> pendingKernel;
    ToneSettings pendingTone;
    std::atomic<bool> pendingClear { false };

    std::atomic<bool> roomActive { false };
    bool roomLoaded = false;   // audio-thread copy of roomActive
    double sampleRate = 44100.0;
    int numChans = 2;
};

} // namespace room
