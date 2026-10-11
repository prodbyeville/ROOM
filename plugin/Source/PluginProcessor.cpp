#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Background worker: one thread that analyses dropped files and rebuilds the
// room (IR + filters) when Intensity, Tail or the sample rate change.

class RoomAudioProcessor::Worker : public juce::Thread
{
public:
    explicit Worker (RoomAudioProcessor& p) : juce::Thread ("ROOM worker"), owner (p)
    {
        startThread (juce::Thread::Priority::low);
    }

    ~Worker() override
    {
        signalThreadShouldExit();
        notify();
        stopThread (5000);
    }

    void requestFile (const juce::File& f)
    {
        {
            const juce::ScopedLock sl (lock);
            pendingFile = f;
            hasFile = true;
        }
        notify();
    }

    void requestRebuild()
    {
        rebuild = true;
        notify();
    }

    void run() override
    {
        while (! threadShouldExit())
        {
            if (! hasWork())
                wait (-1);

            juce::File file;
            bool doFile = false;
            {
                const juce::ScopedLock sl (lock);
                std::swap (doFile, hasFile);
                file = pendingFile;
            }

            if (doFile)
                owner.analyseFile (file);   // also rebuilds

            if (rebuild.load())
            {
                // Knob turns arrive in bursts: wait a moment so we build once.
                wait (60);
                if (threadShouldExit())
                    return;
                rebuild = false;
                owner.rebuildRoom();
            }
        }
    }

private:
    bool hasWork()
    {
        const juce::ScopedLock sl (lock);
        return hasFile || rebuild.load();
    }

    RoomAudioProcessor& owner;
    juce::CriticalSection lock;
    juce::File pendingFile;
    bool hasFile = false;
    std::atomic<bool> rebuild { false };
};

//==============================================================================

RoomAudioProcessor::RoomAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formatManager.registerBasicFormats();

    intensityParam   = apvts.getRawParameterValue (ParamIDs::intensity);
    mixParam         = apvts.getRawParameterValue (ParamIDs::mix);
    balanceAutoParam = apvts.getRawParameterValue (ParamIDs::balanceAuto);
    balanceParam     = apvts.getRawParameterValue (ParamIDs::balance);
    tailParam        = apvts.getRawParameterValue (ParamIDs::tail);
    outputParam      = apvts.getRawParameterValue (ParamIDs::output);
    tapeParam        = apvts.getRawParameterValue (ParamIDs::tape);
    autoEqParam      = apvts.getRawParameterValue (ParamIDs::autoEq);
    for (int b = 0; b < room::tone::numEqBands; ++b)
    {
        eqFreqParam[b] = apvts.getRawParameterValue (ParamIDs::eqFreq (b));
        eqGainParam[b] = apvts.getRawParameterValue (ParamIDs::eqGain (b));
        eqQParam[b]    = apvts.getRawParameterValue (ParamIDs::eqQ (b));
    }

    apvts.addParameterListener (ParamIDs::intensity, this);
    apvts.addParameterListener (ParamIDs::tail, this);

    // Report the Auto EQ latency from the start (prepareToPlay refines it for
    // the actual sample rate), so hosts that ask early get the right answer.
    suppressor.prepare (44100.0, 2);
    setLatencySamples (suppressor.getLatency());

    worker = std::make_unique<Worker> (*this);
}

RoomAudioProcessor::~RoomAudioProcessor()
{
    apvts.removeParameterListener (ParamIDs::intensity, this);
    apvts.removeParameterListener (ParamIDs::tail, this);
    worker.reset();
}

juce::AudioProcessorValueTreeState::ParameterLayout RoomAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto pct = AudioParameterFloatAttributes().withStringFromValueFunction (
        [] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + "%"; });

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::intensity, 1 }, "Intensity",
        NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f, pct));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::mix, 1 }, "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f, pct));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ParamIDs::balanceAuto, 1 }, "Balance Auto", true));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::balance, 1 }, "Balance",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            return String (juce::roundToInt ((1.0f - v) * 100.0f)) + " tone / "
                 + String (juce::roundToInt (v * 100.0f)) + " space";
        })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::tail, 1 }, "Tail",
        NormalisableRange<float> (0.3f, 6.0f, 0.01f, 0.5f), 2.5f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return String (v, 2) + " s"; })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::output, 1 }, "Output",
        NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; })));

    // --- Post-room tone shaping -------------------------------------------
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::tape, 1 }, "Tape",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f, pct));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::autoEq, 1 }, "Auto EQ",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f, pct));

    for (int b = 0; b < room::tone::numEqBands; ++b)
    {
        const auto name = "EQ " + String (b + 1);
        NormalisableRange<float> freqRange (20.0f, 20000.0f);
        freqRange.setSkewForCentre (630.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamIDs::eqFreq (b), 1 }, name + " Freq", freqRange, room::tone::kBandFreqs[b],
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
            {
                return v >= 1000.0f ? String (v / 1000.0f, 2) + " kHz" : String (juce::roundToInt (v)) + " Hz";
            })));
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamIDs::eqGain (b), 1 }, name + " Gain",
            NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; })));
        NormalisableRange<float> qRange (0.3f, 8.0f);
        qRange.setSkewForCentre (1.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamIDs::eqQ (b), 1 }, name + " Q", qRange, room::tone::kBandQs[b],
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return "Q " + String (v, 2); })));
    }

    return layout;
}

void RoomAudioProcessor::getEqBands (room::EqBand (&out)[room::tone::numEqBands]) const
{
    for (int b = 0; b < room::tone::numEqBands; ++b)
        out[b] = { room::tone::kBandTypes[b], eqFreqParam[b]->load(), eqGainParam[b]->load(), eqQParam[b]->load() };
}

//==============================================================================

bool RoomAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    // mono -> mono, mono -> stereo, stereo -> stereo
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void RoomAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    toneShaper.prepare (sampleRate, getTotalNumOutputChannels());
    suppressor.prepare (sampleRate, getTotalNumOutputChannels());
    setLatencySamples (suppressor.getLatency());
    outputGain.reset (sampleRate, 0.05);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    currentSampleRate = sampleRate;
    worker->requestRebuild();   // the IR has to be rebuilt at this sample rate
}

void RoomAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numIn = getTotalNumInputChannels();
    const auto numOut = getTotalNumOutputChannels();

    // Mono in, stereo out: start both sides from the mono input.
    if (numIn == 1 && numOut > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, buffer.getNumSamples());
    for (auto ch = std::max (numIn, 2); ch < numOut; ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    const auto balance = balanceAutoParam->load() > 0.5f ? detectedBalance.load()
                                                         : balanceParam->load();
    // Room -> Tape -> EQ -> Auto EQ -> output gain
    engine.process (buffer, balance, mixParam->load(), 0.0f);

    room::EqBand bands[room::tone::numEqBands];
    getEqBands (bands);
    toneShaper.process (buffer, tapeParam->load(), bands);
    suppressor.process (buffer, autoEqParam->load());

    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    const auto outChans = std::min (buffer.getNumChannels(), 2);
    if (outputGain.isSmoothing())
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const auto g = outputGain.getNextValue();
            for (int ch = 0; ch < outChans; ++ch)
                buffer.getWritePointer (ch)[i] *= g;
        }
    }
    else
    {
        for (int ch = 0; ch < outChans; ++ch)
            buffer.applyGain (ch, 0, buffer.getNumSamples(), outputGain.getTargetValue());
    }

    spectrumTap.push (buffer);
}

double RoomAudioProcessor::getTailLengthSeconds() const
{
    return tailParam->load() * std::max (1.0f, intensityParam->load());
}

void RoomAudioProcessor::parameterChanged (const juce::String&, float)
{
    worker->requestRebuild();
}

//==============================================================================
// Room handling

void RoomAudioProcessor::loadRoomFile (const juce::File& file)
{
    {
        const juce::ScopedLock sl (analysisLock);
        statusMessage = "Analyzing " + file.getFileName() + "...";
    }
    status = Status::analyzing;
    ++analysisVersion;
    worker->requestFile (file);
}

void RoomAudioProcessor::clearRoom()
{
    setAnalysis ({});
    engine.clearRoom();
}

void RoomAudioProcessor::analyseFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate <= 0)
    {
        {
            const juce::ScopedLock sl (analysisLock);
            statusMessage = "Couldn't read " + file.getFileName();
        }
        status = Status::failed;
        ++analysisVersion;
        return;
    }

    // The analysis only looks at the first 60 seconds.
    const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (60.0 * reader->sampleRate));
    juce::AudioBuffer<float> audio ((int) std::min (2u, reader->numChannels), len);
    reader->read (&audio, 0, len, 0, true, audio.getNumChannels() > 1);

    auto result = room::analyzeRoom (audio, reader->sampleRate);
    if (! result.valid)
    {
        {
            const juce::ScopedLock sl (analysisLock);
            statusMessage = file.getFileName() + " is too short to analyze";
        }
        status = Status::failed;
        ++analysisVersion;
        return;
    }

    result.sourceName = file.getFileName();
    setAnalysis (std::move (result));
    rebuildRoom();
}

void RoomAudioProcessor::setAnalysis (room::RoomAnalysis a)
{
    const auto valid = a.valid;
    {
        const juce::ScopedLock sl (analysisLock);
        analysis = std::move (a);
        statusMessage = valid ? analysis.sourceName : juce::String();
        detectedBalance = analysis.balance;
    }
    status = valid ? Status::ready : Status::empty;
    ++analysisVersion;
}

void RoomAudioProcessor::rebuildRoom()
{
    const auto sr = currentSampleRate.load();
    const auto a = getAnalysis();
    if (! a.valid || sr <= 0.0)
        return;

    const auto intensity = intensityParam->load();
    const auto tail = tailParam->load();

    // IR length follows the decays: no point in a 6 s buffer for a 0.3 s room.
    float longest = 0.05f;
    for (auto t : a.bandT60s)
        longest = std::max (longest, t * intensity);
    const auto irSeconds = std::min (tail, longest + 0.1f);

    engine.submitRoom (room::synthesizeIR (a, sr, irSeconds, intensity), sr,
                       room::designTone (a, sr, intensity));
}

room::RoomAnalysis RoomAudioProcessor::getAnalysis() const
{
    const juce::ScopedLock sl (analysisLock);
    return analysis;
}

juce::String RoomAudioProcessor::getStatusMessage() const
{
    const juce::ScopedLock sl (analysisLock);
    return statusMessage;
}

//==============================================================================
// Session state: the parameters plus the analysed numbers (not the audio file).

void RoomAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree state ("ROOM_STATE");
    state.appendChild (apvts.copyState(), nullptr);
    state.appendChild (getAnalysis().toValueTree(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, dest);
}

void RoomAudioProcessor::setStateInformation (const void* data, int size)
{
    const auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr)
        return;

    const auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType ("ROOM_STATE"))
        return;

    const auto params = state.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    auto restored = room::RoomAnalysis::fromValueTree (state.getChildWithName ("ROOM_ANALYSIS"));
    if (restored.valid)
    {
        setAnalysis (std::move (restored));
        worker->requestRebuild();
    }
    else
    {
        clearRoom();
    }
}

//==============================================================================

juce::AudioProcessorEditor* RoomAudioProcessor::createEditor()
{
    return new RoomAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RoomAudioProcessor();
}
