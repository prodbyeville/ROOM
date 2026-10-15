#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "dsp/RoomEngine.h"
#include "dsp/ToneShaper.h"
#include "dsp/ResonanceSuppressor.h"
#include "dsp/SpectrumTap.h"
#include "dsp/StereoWidth.h"
#include "presets/PresetLibrary.h"
#include "dsp/Lfo.h"
#include "Modulation.h"

namespace ParamIDs
{
    inline constexpr const char* intensity   = "intensity";
    inline constexpr const char* mix         = "mix";
    inline constexpr const char* balanceAuto = "balanceAuto";
    inline constexpr const char* balance     = "balance";
    inline constexpr const char* tail        = "tail";
    inline constexpr const char* output      = "output";
    inline constexpr const char* tape        = "tape";
    inline constexpr const char* autoEq      = "autoEq";
    inline constexpr const char* space       = "space";
    // Advanced screen
    inline constexpr const char* preDelay    = "preDelay";
    inline constexpr const char* size        = "size";
    inline constexpr const char* damping     = "damping";
    inline constexpr const char* duck        = "duck";
    inline constexpr const char* duckRelease = "duckRelease";
    inline constexpr const char* freeze      = "freeze";
    // Top bar
    inline constexpr const char* bypass      = "bypass";

    // LFOs: "lfo1Rate", "lfo1Sync", "lfo1Div", "lfo1Shape", "lfo1Retrig" (and lfo2...)
    inline juce::String lfoRate (int l)   { return "lfo" + juce::String (l + 1) + "Rate"; }
    inline juce::String lfoSync (int l)   { return "lfo" + juce::String (l + 1) + "Sync"; }
    inline juce::String lfoDiv (int l)    { return "lfo" + juce::String (l + 1) + "Div"; }
    inline juce::String lfoShape (int l)  { return "lfo" + juce::String (l + 1) + "Shape"; }
    inline juce::String lfoRetrig (int l) { return "lfo" + juce::String (l + 1) + "Retrig"; }

    // EQ band parameters: "eq1Freq", "eq1Gain", "eq1Q" ... "eq5Q"
    inline juce::String eqFreq (int band) { return "eq" + juce::String (band + 1) + "Freq"; }
    inline juce::String eqGain (int band) { return "eq" + juce::String (band + 1) + "Gain"; }
    inline juce::String eqQ    (int band) { return "eq" + juce::String (band + 1) + "Q"; }
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
    juce::AudioProcessorParameter* getBypassParameter() const override;

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

    // Record into ROOM: whatever comes into this track becomes the room.
    static constexpr double maxRecordSeconds = 30.0;
    void startRecording();
    void stopRecording();
    bool isRecording() const { return recordArmed.load(); }
    double getRecordedSeconds() const;
    float getRecordLevel() const { return recordLevel.load(); }
    void clearRoom();

    enum class Status { empty, recording, analyzing, ready, failed };
    Status getStatus() const { return status.load(); }
    juce::String getStatusMessage() const;

    /** A copy of the current analysis (safe from any non-audio thread). */
    room::RoomAnalysis getAnalysis() const;

    /** Goes up by one every time the analysis changes, so the UI knows to redraw. */
    int getAnalysisVersion() const { return analysisVersion.load(); }

    /** Peak level of the incoming audio, for the room screen (any thread). */
    float getInputLevel() const { return inputLevel.load(); }

    /** Balance detected from the loaded room (used when Auto is on). */
    float getDetectedBalance() const { return detectedBalance.load(); }

    /** Current EQ band settings from the parameters (any thread). */
    void getEqBands (room::EqBand (&out)[room::tone::numEqBands]) const;

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioFormatManager formatManager;

    // Read by the editor for its displays.
    room::SpectrumTap spectrumTap;
    const room::ResonanceSuppressor& getSuppressor() const { return suppressor; }
    double getCurrentSampleRate() const { return currentSampleRate.load(); }

    //==========================================================================
    // ROOMS (the room only) and STUDIOS (room + knobs + LFOs). Message thread.

    /** STUDIOS: full presets. */
    room::presets::PresetLibrary& getLibrary();
    /** ROOMS: just rooms. */
    room::presets::PresetLibrary& getRoomLibrary();

    /** Use a different base folder (tests): ROOMS and STUDIOS go inside it. */
    void setPresetRoot (const juce::File& base);

    /** The current room as a ROOM file. */
    juce::ValueTree captureRoom (const room::presets::PresetMeta&) const;

    /** Swap in a ROOM: only the room changes, every knob and LFO stays. */
    void applyRoom (const juce::ValueTree& roomTree, const juce::String& key);

    juce::String getCurrentRoomKey() const { return currentRoomKey; }
    void setCurrentRoomKey (const juce::String& key) { currentRoomKey = key; }

    /** The current sound as a preset: every parameter except Output, plus the room. */
    juce::ValueTree capturePreset (const room::presets::PresetMeta&) const;

    /** Restores a preset: every parameter it has (Output untouched) and its room. */
    void applyPreset (const juce::ValueTree& preset, const juce::String& key);

    /** Remember that the current sound is now saved as this preset. */
    void setCurrentPreset (const juce::String& key, const juce::String& name);

    /** Same, but keeps the "modified" state (used when a preset is only renamed or retagged). */
    void relabelCurrentPreset (const juce::String& key, const juce::String& name) { currentPresetKey = key; currentPresetName = name; }

    juce::String getCurrentPresetKey() const { return currentPresetKey; }
    juce::String getCurrentPresetName() const { return currentPresetName; }

    /** True if any knob moved since the preset was loaded or saved. */
    bool isPresetModified() const;

    /** What Randomize touches (right-click the Randomize button to choose). */
    enum RandomizeGroup
    {
        randomRoom     = 1 << 0,   // Intensity, Balance, Tail, Space
        randomAdvanced = 1 << 1,   // Pre-delay, Size, Damping, Ducking, Release
        randomEq       = 1 << 2,   // EQ band gains
        randomFinish   = 1 << 3,   // Tape, Auto EQ
        randomLfos     = 1 << 4,   // LFO shape / rate / sync / division (not what they're assigned to)
        randomDefault  = randomRoom | randomAdvanced | randomEq | randomFinish,
        randomAll      = randomDefault | randomLfos
    };
    int randomizeMask = randomDefault;   // saved with the project

    /** Randomize the knobs in randomizeMask within musical ranges (message thread).
        Never touches the room itself, Dry/Wet, Output, Bypass or Freeze. */
    void randomizeKnobs (juce::Random&);

    /** Remove every LFO assignment and put both LFOs back to their defaults. */
    void clearModulation();

    /** "new preset": every knob, the eq and the lfos back to their defaults
        (Output and Bypass untouched). The loaded room stays. */
    void resetToInit();

    /** LFO -> knob assignments (edited by the UI, read by the audio thread). */
    room::Modulation modulation;

    /** UI state saved with the project. */
    float editorScale = 1.0f;
    bool advancedOpen = false;
    bool tipsEnabled = true;   // hover tips on knobs and keys

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged (const juce::String& id, float) override;

    // Background work: analysing files and rebuilding the IR never happens
    // on the audio thread.
    class Worker;
    friend class Worker;
    std::unique_ptr<Worker> worker;

    void analyseFile (const juce::File&);   // worker thread
    void analyseAudio (const juce::AudioBuffer<float>&, double sampleRate, const juce::String& name,
                       const juce::String& origin);   // worker thread
    void finishRecording();                 // worker thread
    void rebuildRoom();                     // worker thread
    void setAnalysis (room::RoomAnalysis);  // any non-audio thread

    room::RoomEngine engine;
    room::ToneShaper toneShaper;
    room::ResonanceSuppressor suppressor;
    room::StereoWidth stereoWidth;
    juce::SmoothedValue<float> outputGain;

    // Bypass: a latency-matched copy of the input, crossfaded in.
    juce::AudioBuffer<float> dryDelay, dryBlock;
    int dryDelayWrite = 0;
    juce::SmoothedValue<float> bypassMix;

    mutable juce::CriticalSection analysisLock;
    room::RoomAnalysis analysis;
    juce::String statusMessage;
    std::atomic<int> analysisVersion { 0 };
    std::atomic<Status> status { Status::empty };
    std::atomic<float> detectedBalance { 0.5f };
    std::atomic<float> inputLevel { 0.0f };
    std::atomic<double> currentSampleRate { 0.0 };

    std::atomic<float>* intensityParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* balanceAutoParam = nullptr;
    std::atomic<float>* balanceParam = nullptr;
    std::atomic<float>* tailParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* tapeParam = nullptr;
    std::atomic<float>* autoEqParam = nullptr;
    std::atomic<float>* spaceParam = nullptr;
    std::atomic<float>* preDelayParam = nullptr;
    std::atomic<float>* sizeParam = nullptr;
    std::atomic<float>* dampingParam = nullptr;
    std::atomic<float>* duckParam = nullptr;
    std::atomic<float>* duckReleaseParam = nullptr;
    std::atomic<float>* freezeParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* eqFreqParam[room::tone::numEqBands] {};
    std::atomic<float>* eqGainParam[room::tone::numEqBands] {};
    std::atomic<float>* eqQParam[room::tone::numEqBands] {};

    // LFOs
    void updateModulation (int samplesIntoBlock, int numSamples);
    float effective (int target, float baseValue) const;
    float effectiveForRebuild (const char* paramId) const;
    bool rebuildTargetsMoved() const;   // worker thread
    bool modulatesRebuildTargets() const;
    room::Lfo lfos[room::Modulation::numLfos];
    std::array<juce::RangedAudioParameter*, 32> targetParams {};
    std::array<float, 32> modOffset {};
    std::array<std::atomic<float>, 32> effValue {};       // effective values (rebuild targets read these)
    std::array<float, 32> lastBuiltValue {};               // worker thread
    bool wasPlaying = false;
    double blockPpq = 0.0, blockBpm = 120.0;
    bool blockPlaying = false, blockHasPpq = false;

    // Recording (buffer allocated in prepareToPlay; audio thread writes)
    juce::AudioBuffer<float> recordBuf;
    std::atomic<int> recordPos { 0 };
    std::atomic<bool> recordArmed { false }, recordStopRequest { false }, recordFinished { false };
    std::atomic<float> recordLevel { 0.0f };
    juce::uint32 recordStopTime = 0;
    int recordCount = 0;

    // Presets
    std::map<juce::String, float> currentParamValues() const;
    void snapshotPreset();

    void ensureLibraries();
    juce::File presetBase = room::presets::PresetLibrary::defaultBase();
    std::unique_ptr<room::presets::PresetLibrary> library, roomLibrary;
    juce::String currentPresetKey, currentPresetName, currentRoomKey;
    std::map<juce::String, float> presetSnapshot;
    juce::String modulationSnapshot, roomSnapshot;
    bool restoredAsModified = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomAudioProcessor)
};
