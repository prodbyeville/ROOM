#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>
#include <atomic>

// Zero-latency convolution for long room IRs, with steady CPU.
//
// The IR is split into three stages of uniform partitions (overlap-save):
//
//   head   samples 0 .. 2048        partitions of 128   on the audio thread, every call
//   mid    samples 2048 .. 16384    partitions of 1024  on a background thread
//   tail   samples 16384 .. end     partitions of 8192  on a background thread
//
// A background stage starts its work as soon as a partition of input is in
// and has a whole partition of time before its output is due, so the audio
// thread never does a big FFT at once (no spikes). All stages share one
// history of input spectra, so a new IR crossfades in with the full tail of
// everything played before it.

namespace room
{

class PartitionedConvolver
{
public:
    /** An IR, cut into partitions and transformed. Immutable once built;
        build it on any thread except the audio thread. */
    struct Kernel;
    static std::shared_ptr<const Kernel> makeKernel (const juce::AudioBuffer<float>& ir, int numChannels);

    PartitionedConvolver();
    ~PartitionedConvolver();

    /** maxIrSeconds bounds the history kept for the tail stage. */
    void prepare (double sampleRate, int numChannels, double maxIrSeconds = 10.0);
    void reset();

    /** Audio thread: switch to a new IR (crossfades stage by stage). Passing
        nullptr silences the output. Never blocks, never frees memory. */
    void setKernel (std::shared_ptr<const Kernel>);

    /** Audio thread: convolve in place. */
    void process (float* const* channels, int numChannels, int numSamples);

    static constexpr int numStages = 3;
    static constexpr int partitionSize[numStages] = { 128, 1024, 8192 };
    static constexpr int stageStart[numStages]    = { 0, 2048, 16384 };

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace room
