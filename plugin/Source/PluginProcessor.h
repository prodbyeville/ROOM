#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "dsp/RoomEngine.h"
#include "dsp/ToneShaper.h"
#include "dsp/ResonanceSuppressor.h"
#include "dsp/SpectrumTap.h"
#include "dsp/StereoWidth.h"
#include "dsp/RoomMic.h"
#include "dsp/Imprint.h"
#include "dsp/MudCut.h"
#include "presets/PresetLibrary.h"
#include "dsp/Lfo.h"
#include "Modulation.h"
#include "licensing/License.h"

namespace ParamIDs
{
    inline constexpr const char* intensity   = "intensity";
    inline constexpr const char* mix         = "mix";
    inline constexpr const char* balanceAuto = "balanceAuto";
    inline constexpr const char* balance     = "balance";      // old projects only (now tone + space)
    inline constexpr const char* toneAmount  = "toneAmount";   // how much of the room's tone (eq, rings)
    inline constexpr const char* spaceAmount = "spaceAmount";  // how much of the room's space (decay, width)
    inline constexpr const char* toneSpaceSync = "toneSpaceSync"; // retired (kept so old projects load); ignored
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
    inline constexpr const char* glitch      = "glitch";     // the stutter key (linked to Intensity)
    inline constexpr const char* glitchSync  = "glitchSync"; // stutter in time with the host
    inline constexpr const char* glitchAmount = "glitchAmount"; // how much it stutters
    // Bottom module
    inline constexpr const char* noiseType   = "noiseType";
    inline constexpr const char* noiseLevel  = "noiseLevel";
    inline constexpr const char* noiseFollow = "noiseFollow";
    inline constexpr const char* noiseStop   = "noiseStop";   // the noise goes quiet while nothing plays
    inline constexpr const char* rate        = "rate";       // clean sample-rate reduction, Hz
    inline constexpr const char* rateDry     = "rateDry";    // the D / R / N keys: which paths Rate reaches
    inline constexpr const char* rateReverb  = "rateReverb";
    inline constexpr const char* rateNoise   = "rateNoise";
    inline constexpr const char* smooth      = "smooth";     // the room-mic softening (intensity section)
    inline constexpr const char* pitch       = "pitch";      // -1 .. 1: an octave down / up blended into the room
    inline constexpr const char* mud         = "mud";        // Advanced: tames the boxy low mids
    inline constexpr const char* imprint     = "imprint";    // the sound takes on the room's timbre
    // Top bar
    inline constexpr const char* bypass      = "bypass";

    // LFOs: "lfo1Rate", "lfo1Sync", "lfo1Div", "lfo1Shape", "lfo1Retrig" (and lfo2...)
    inline juce::String lfoRate (int l)   { return "lfo" + juce::String (l + 1) + "Rate"; }
    inline juce::String lfoSync (int l)   { return "lfo" + juce::String (l + 1) + "Sync"; }
    inline juce::String lfoDiv (int l)    { return "lfo" + juce::String (l + 1) + "Div"; }
    inline juce::String lfoShape (int l)  { return "lfo" + juce::String (l + 1) + "Shape"; }
    inline juce::String lfoRetrig (int l) { return "lfo" + juce::String (l + 1) + "Retrig"; }
}

class RoomAudioProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorValueTreeState::Listener
{
public:
    /** Intensity values up to this build the room (2 = twice the measured room, shown as 100 %);
        above it, extreme mode. */
    static constexpr float maxRoomIntensity = 2.0f;
    static constexpr float characterTopIntensity = 3.5f;   // 175 %: the room falls apart below, shimmer / bloom above

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
    /** Rooms you pick, drop or record start at intensity 50 % (the room as measured). */
    void resetIntensityForNewRoom();
    /** Loads a random room from the room library (not the current one), as picking it in the
        browser would. Message thread. Returns false if there's no other room. */
    bool loadRandomRoom (juce::Random&);

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

    /** Randomize: what's ticked on its page and how wild (Randomizer.h). Saved with the
        project, never in presets. */
    juce::ValueTree randomizeSelection { "RandomizeSelection" };

    /** Remove every LFO assignment and put both LFOs back to their defaults. */
    void clearModulation();

    /** "new preset": every knob, the eq and the lfos back to their defaults
        (Output and Bypass untouched). The loaded room stays. */
    void resetToInit (bool keepPresetName = false);

    /** LFO -> knob assignments (edited by the UI, read by the audio thread). */
    room::Modulation modulation;

    /** Load "Init Room" into every new instance (tests switch it off). */
    static inline bool startWithDefaultStudio = true;

    //==========================================================================
    // Licensing (licensing/License.h). Decided when this instance is created:
    // without a license the audio passes through dry (latency-matched) and the
    // editor shows the sign-in page. Signing in unlocks every open ROOM; a
    // license the server revokes only locks the next time ROOM loads.
    static inline bool enforceLicense = true;   // the DSP tests switch it off
    bool isLicenseLocked() const { return licenseLocked.load(); }
    LicenseManager& getLicensing() { return *licensing; }
    /** Unlock now if the license has become active (called on license changes). */
    void licenseChanged();
    /** Lock this instance now ("deactivate this computer"). */
    void lockLicense() { if (enforceLicense) licenseLocked = true; }

    /** UI state saved with the project. */
    float editorScale = 1.0f;
    bool advancedOpen = false;
    bool tipsEnabled = true;   // hover tips on knobs and keys

private:
    juce::SharedResourcePointer<LicenseManager> licensing;
    std::atomic<bool> licenseLocked { false };
    struct LicenseWatch : juce::ChangeListener
    {
        explicit LicenseWatch (RoomAudioProcessor& p) : owner (p) {}
        void changeListenerCallback (juce::ChangeBroadcaster*) override { owner.licenseChanged(); }
        RoomAudioProcessor& owner;
    } licenseWatch { *this };

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
    juce::SharedResourcePointer<room::NoiseBank> noiseBank;
    room::NoisePlayer noise;
public:
    /** For the noise screen (read on the message thread): the follow gate (0..1) and the
        stop's fade (0..1), as the audio last saw them. Display only. */
    std::atomic<float> noiseGateShown { 0.0f }, noiseStopShown { 1.0f };
private:
    room::RateReducer noiseRate;     // Rate on the noise (the N key); dry and room are in the engine
    juce::AudioBuffer<float> noiseBuf;
    room::RoomMic roomMic;           // Smooth
    room::Imprint imprint;           // Imprint (before the room)
    room::MudCut mudCut;             // Mud (Advanced, after tape)
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
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* spaceAmountParam = nullptr;
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
    std::atomic<float>* glitchParam = nullptr;
    std::atomic<float>* glitchSyncParam = nullptr;
    std::atomic<float>* glitchAmountParam = nullptr;
    std::atomic<float>* noiseTypeParam = nullptr;
    std::atomic<float>* noiseLevelParam = nullptr;
    std::atomic<float>* noiseFollowParam = nullptr;
    std::atomic<float>* noiseStopParam = nullptr;
    int silentSamples = 0;   // how long nothing has come in (for the noise's stop)
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* rateDryParam = nullptr;
    std::atomic<float>* rateReverbParam = nullptr;
    std::atomic<float>* rateNoiseParam = nullptr;
    std::atomic<float>* smoothParam = nullptr;
    std::atomic<float>* imprintParam = nullptr;
    std::atomic<float>* pitchParam = nullptr;
    std::atomic<float>* mudParam = nullptr;
    std::atomic<float>* bypassParam = nullptr;

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
