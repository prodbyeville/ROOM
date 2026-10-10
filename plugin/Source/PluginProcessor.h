#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "dsp/RoomEngine.h"

namespace ParamIDs
{
    inline constexpr const char* intensity   = "intensity";
    inline constexpr const char* mix         = "mix";
    inline constexpr const char* balanceAuto = "balanceAuto";
    inline constexpr const char* balance     = "balance";
    inline constexpr const char* tail        = "tail";
    inline constexpr const char* output      = "output";
}

class RoomAudioProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener
{
public:
    RoomAudioProcessor();
    ~RoomAudioProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    // Room loading (call from the message thread)
    void loadRoomFile (const juce::File&);
    void clearRoom();

    enum class Status { empty, analyzing, ready, failed };
    Status getStatus() const { return status.load(); }
    juce::String getStatusMessage() const;

    /** A copy of the current analysis (safe from any non-audio thread). */
    room::RoomAnalysis getAnalysis() const;

    /** Goes up by one every time the analysis changes, so the UI knows to redraw. */
    int getAnalysisVersion() const { return analysisVersion.load(); }

    /** Balance detected from the loaded room (used when Auto is on). */
    float getDetectedBalance() const { return detectedBalance.load(); }

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioFormatManager formatManager;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float) override;

    // Background work: analysing files and rebuilding the IR never happens
    // on the audio thread.
    class Worker;
    friend class Worker;
    std::unique_ptr<Worker> worker;

    void analyseFile (const juce::File&);   // worker thread
    void rebuildRoom();                     // worker thread
    void setAnalysis (room::RoomAnalysis);  // any non-audio thread

    room::RoomEngine engine;

    mutable juce::CriticalSection analysisLock;
    room::RoomAnalysis analysis;
    juce::String statusMessage;
    std::atomic<int> analysisVersion { 0 };
    std::atomic<Status> status { Status::empty };
    std::atomic<float> detectedBalance { 0.5f };
    std::atomic<double> currentSampleRate { 0.0 };

    std::atomic<float>* intensityParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* balanceAutoParam = nullptr;
    std::atomic<float>* balanceParam = nullptr;
    std::atomic<float>* tailParam = nullptr;
    std::atomic<float>* outputParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomAudioProcessor)
};
