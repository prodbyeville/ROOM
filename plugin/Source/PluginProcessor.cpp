#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "FactoryPresets.h"

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

    /** Wake up to pick up a finished recording. */
    void poke() { notify(); }

    void run() override
    {
        while (! threadShouldExit())
        {
            if (! hasWork())
                wait (owner.recordArmed.load() ? 30 : -1);   // while recording, check for the end often

            owner.finishRecording();

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
        return hasFile || rebuild.load() || owner.recordFinished.load();
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
    spaceParam       = apvts.getRawParameterValue (ParamIDs::space);
    preDelayParam    = apvts.getRawParameterValue (ParamIDs::preDelay);
    sizeParam        = apvts.getRawParameterValue (ParamIDs::size);
    dampingParam     = apvts.getRawParameterValue (ParamIDs::damping);
    duckParam        = apvts.getRawParameterValue (ParamIDs::duck);
    duckReleaseParam = apvts.getRawParameterValue (ParamIDs::duckRelease);
    freezeParam      = apvts.getRawParameterValue (ParamIDs::freeze);
    bypassParam      = apvts.getRawParameterValue (ParamIDs::bypass);
    for (int b = 0; b < room::tone::numEqBands; ++b)
    {
        eqFreqParam[b] = apvts.getRawParameterValue (ParamIDs::eqFreq (b));
        eqGainParam[b] = apvts.getRawParameterValue (ParamIDs::eqGain (b));
        eqQParam[b]    = apvts.getRawParameterValue (ParamIDs::eqQ (b));
    }

    // These reshape the room, so the IR is rebuilt when they move.
    for (auto* id : { ParamIDs::intensity, ParamIDs::tail, ParamIDs::size, ParamIDs::damping })
        apvts.addParameterListener (id, this);

    // Report the Auto EQ latency from the start (prepareToPlay refines it for
    // the actual sample rate), so hosts that ask early get the right answer.
    suppressor.prepare (44100.0, 2);
    setLatencySamples (suppressor.getLatency());

    worker = std::make_unique<Worker> (*this);
    snapshotPreset();
}

RoomAudioProcessor::~RoomAudioProcessor()
{
    for (auto* id : { ParamIDs::intensity, ParamIDs::tail, ParamIDs::size, ParamIDs::damping })
        apvts.removeParameterListener (id, this);
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
        ParameterID { ParamIDs::mix, 1 }, "Dry/Wet",
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
        ParameterID { ParamIDs::space, 1 }, "Space",
        NormalisableRange<float> (0.0f, 2.0f, 0.01f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            if (v < 0.005f) return String ("Mono");
            return String (juce::roundToInt (v * 100.0f)) + "%";
        })));

    // --- Advanced screen ------------------------------------------------------
    {
        NormalisableRange<float> r (0.0f, 500.0f, 0.1f);
        r.setSkewForCentre (80.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamIDs::preDelay, 1 }, "Pre-delay", r, 0.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return String (juce::roundToInt (v)) + " ms"; })));
    }
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::size, 1 }, "Size",
        NormalisableRange<float> (0.5f, 2.0f, 0.01f, 0.5f), 1.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::damping, 1 }, "Damping",
        NormalisableRange<float> (-1.0f, 1.0f, 0.01f), 0.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
        {
            const auto p = juce::roundToInt (v * 100.0f);
            return p == 0 ? String ("0") : (p > 0 ? "+" + String (p) + " dark" : String (p) + " bright");
        })));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { ParamIDs::duck, 1 }, "Ducking",
        NormalisableRange<float> (0.0f, 1.0f, 0.01f), 0.0f, pct));
    {
        NormalisableRange<float> r (50.0f, 1000.0f, 1.0f);
        r.setSkewForCentre (250.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamIDs::duckRelease, 1 }, "Duck Release", r, 250.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return String (juce::roundToInt (v)) + " ms"; })));
    }
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ParamIDs::freeze, 1 }, "Freeze", false));
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));

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
    stereoWidth.prepare (sampleRate);
    setLatencySamples (suppressor.getLatency());
    outputGain.reset (sampleRate, 0.05);
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));

    const auto chans = std::max (2, getTotalNumOutputChannels());
    dryDelay.setSize (chans, suppressor.getLatency() + 1);
    dryDelay.clear();
    dryDelayWrite = 0;
    dryBlock.setSize (chans, samplesPerBlock);
    if (! recordArmed.load())
        recordBuf.setSize (2, (int) (maxRecordSeconds * sampleRate));
    bypassMix.reset (sampleRate, 0.03);
    bypassMix.setCurrentAndTargetValue (bypassParam->load() > 0.5f ? 1.0f : 0.0f);
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

    // Record into ROOM: copy the incoming audio before any processing.
    if (recordArmed.load())
    {
        const auto cap = recordBuf.getNumSamples();
        auto pos = recordPos.load();
        if (recordStopRequest.exchange (false) || pos >= cap)
        {
            recordArmed = false;
            recordFinished = true;
        }
        else
        {
            const auto count = std::min (buffer.getNumSamples(), cap - pos);
            float peak = 0.0f;
            for (int ch = 0; ch < recordBuf.getNumChannels(); ++ch)
            {
                const auto src = std::min (ch, buffer.getNumChannels() - 1);
                recordBuf.copyFrom (ch, pos, buffer, src, 0, count);
                peak = std::max (peak, buffer.getMagnitude (src, 0, count));
            }
            recordPos = pos + count;
            recordLevel = std::max (peak, recordLevel.load() * 0.8f);
            if (pos + count >= cap)   // 30 s reached: stop by itself
            {
                recordArmed = false;
                recordFinished = true;
            }
        }
    }

    const auto balance = balanceAutoParam->load() > 0.5f ? detectedBalance.load()
                                                         : balanceParam->load();
    // Keep a copy of the input, delayed by the plugin's latency, for Bypass.
    const auto n = buffer.getNumSamples();
    const auto chansIo = std::min (buffer.getNumChannels(), dryDelay.getNumChannels());
    if (n <= dryBlock.getNumSamples())
    {
        const auto len = dryDelay.getNumSamples();
        for (int ch = 0; ch < chansIo; ++ch)
        {
            auto w = dryDelayWrite;
            for (int i = 0; i < n; ++i)
            {
                // len = latency + 1, so the oldest sample is exactly `latency` behind.
                dryDelay.setSample (ch, w, buffer.getSample (ch, i));
                dryBlock.setSample (ch, i, dryDelay.getSample (ch, (w + 1) % len));
                w = (w + 1) % len;
            }
        }
        dryDelayWrite = (dryDelayWrite + n) % len;
    }

    room::RoomFx fx;
    fx.preDelayMs = preDelayParam->load();
    fx.duck = duckParam->load();
    fx.duckReleaseMs = duckReleaseParam->load();
    fx.freeze = freezeParam->load() > 0.5f;

    // Room -> Tape -> EQ -> Auto EQ -> Space -> output gain
    engine.process (buffer, balance, mixParam->load(), 0.0f, fx);

    room::EqBand bands[room::tone::numEqBands];
    getEqBands (bands);
    toneShaper.process (buffer, tapeParam->load(), bands);
    suppressor.process (buffer, autoEqParam->load());
    stereoWidth.process (buffer, spaceParam->load());

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

    // Bypass: crossfade to the untouched (latency-matched) input.
    bypassMix.setTargetValue (bypassParam->load() > 0.5f ? 1.0f : 0.0f);
    if ((bypassMix.isSmoothing() || bypassMix.getTargetValue() > 0.5f) && n <= dryBlock.getNumSamples())
    {
        for (int i = 0; i < n; ++i)
        {
            const auto b = bypassMix.getNextValue();
            for (int ch = 0; ch < chansIo; ++ch)
            {
                auto* d = buffer.getWritePointer (ch);
                d[i] = (1.0f - b) * d[i] + b * dryBlock.getSample (ch, i);
            }
        }
    }

    spectrumTap.push (buffer);
}

juce::AudioProcessorParameter* RoomAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter (ParamIDs::bypass);
}

void RoomAudioProcessor::randomizeKnobs (juce::Random& r)
{
    auto set = [this] (const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            p->endChangeGesture();
        }
    };
    auto between = [&r] (float lo, float hi) { return lo + r.nextFloat() * (hi - lo); };
    auto maybe = [&r] (float chance) { return r.nextFloat() < chance; };

    // Musical ranges, not the full extremes.
    set (ParamIDs::intensity, between (0.5f, 1.6f));
    set (ParamIDs::balanceAuto, maybe (0.5f) ? 1.0f : 0.0f);
    set (ParamIDs::balance, between (0.1f, 0.9f));
    set (ParamIDs::tail, between (0.6f, 4.0f));
    set (ParamIDs::space, between (0.7f, 1.6f));
    set (ParamIDs::tape, maybe (0.5f) ? between (0.1f, 0.8f) : 0.0f);
    set (ParamIDs::autoEq, maybe (0.5f) ? between (0.1f, 0.6f) : 0.0f);
    for (int b = 0; b < room::tone::numEqBands; ++b)
        set (ParamIDs::eqGain (b), maybe (0.4f) ? between (-4.0f, 4.0f) : 0.0f);
    set (ParamIDs::preDelay, maybe (0.5f) ? between (5.0f, 120.0f) : 0.0f);
    set (ParamIDs::size, between (0.7f, 1.5f));
    set (ParamIDs::damping, between (-0.5f, 0.7f));
    set (ParamIDs::duck, maybe (0.3f) ? between (0.2f, 0.7f) : 0.0f);
    set (ParamIDs::duckRelease, between (120.0f, 600.0f));
}

double RoomAudioProcessor::getTailLengthSeconds() const
{
    return tailParam->load() * std::max (1.0f, intensityParam->load()) + preDelayParam->load() * 0.001;
}

void RoomAudioProcessor::parameterChanged (const juce::String&, float)
{
    worker->requestRebuild();
}

//==============================================================================
// Room handling

void RoomAudioProcessor::loadRoomFile (const juce::File& file)
{
    // A dropped sound replaces the preset's room: it's a new, unsaved sound.
    currentPresetKey = {};
    currentPresetName = file.getFileNameWithoutExtension();
    snapshotPreset();
    {
        const juce::ScopedLock sl (analysisLock);
        statusMessage = "Analyzing " + file.getFileName() + "...";
    }
    status = Status::analyzing;
    ++analysisVersion;
    worker->requestFile (file);
}

void RoomAudioProcessor::startRecording()
{
    if (recordArmed.load() || recordBuf.getNumSamples() == 0)
        return;
    currentPresetKey = {};
    currentPresetName = "Recorded room " + juce::String (recordCount + 1);
    snapshotPreset();
    {
        const juce::ScopedLock sl (analysisLock);
        statusMessage = "Recording...";
    }
    recordPos = 0;
    recordLevel = 0.0f;
    recordStopRequest = false;
    recordFinished = false;
    recordArmed = true;
    status = Status::recording;
    ++analysisVersion;
    worker->poke();
}

void RoomAudioProcessor::stopRecording()
{
    if (! recordArmed.load())
        return;
    recordStopRequest = true;
    recordStopTime = juce::Time::getMillisecondCounter();
    worker->poke();
}

double RoomAudioProcessor::getRecordedSeconds() const
{
    const auto sr = currentSampleRate.load();
    return sr > 0.0 ? recordPos.load() / sr : 0.0;
}

void RoomAudioProcessor::finishRecording()
{
    // If the host isn't running audio, nobody will see the stop request:
    // finish it here after a moment instead.
    if (recordArmed.load() && recordStopRequest.load()
        && juce::Time::getMillisecondCounter() - recordStopTime > 300)
    {
        recordArmed = false;
        recordStopRequest = false;
        recordFinished = true;
    }

    if (! recordFinished.exchange (false))
        return;

    const auto n = recordPos.load();
    juce::AudioBuffer<float> take (recordBuf.getNumChannels(), std::max (1, n));
    for (int ch = 0; ch < take.getNumChannels(); ++ch)
        take.copyFrom (ch, 0, recordBuf, ch, 0, std::max (1, n));

    ++recordCount;
    {
        const juce::ScopedLock sl (analysisLock);
        statusMessage = "Analyzing recording...";
    }
    status = Status::analyzing;
    ++analysisVersion;
    analyseAudio (take, currentSampleRate.load(), "Recorded room " + juce::String (recordCount));
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
    analyseAudio (audio, reader->sampleRate, file.getFileName());
}

void RoomAudioProcessor::analyseAudio (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::String& name)
{
    auto result = room::analyzeRoom (audio, sampleRate);
    if (! result.valid)
    {
        {
            const juce::ScopedLock sl (analysisLock);
            statusMessage = name + " is too short to analyze";
        }
        status = Status::failed;
        ++analysisVersion;
        return;
    }

    result.sourceName = name;
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
    const auto size = sizeParam->load();
    const auto damping = dampingParam->load();

    // IR length follows the decays: no point in a 6 s buffer for a 0.3 s room.
    float longest = 0.05f;
    for (int b = 0; b < a.numBands(); ++b)
        longest = std::max (longest, room::bandDecay (a.bandT60s[(size_t) b], a.bandCentersHz[(size_t) b],
                                                      intensity, size, damping));
    const auto irSeconds = std::min (tail, longest + 0.1f);

    engine.submitRoom (room::synthesizeIR (a, sr, irSeconds, intensity, 0, size, damping), sr,
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
    state.setProperty ("presetKey", currentPresetKey, nullptr);
    state.setProperty ("presetName", currentPresetName, nullptr);
    state.setProperty ("presetModified", isPresetModified(), nullptr);
    state.setProperty ("editorScale", editorScale, nullptr);
    state.setProperty ("advancedOpen", advancedOpen, nullptr);
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

    editorScale = juce::jlimit (0.75f, 1.75f, (float) state.getProperty ("editorScale", 1.0f));
    advancedOpen = (bool) state.getProperty ("advancedOpen", false);
    currentPresetKey = state["presetKey"].toString();
    currentPresetName = state["presetName"].toString();
    snapshotPreset();
    restoredAsModified = (bool) state["presetModified"];
}

//==============================================================================
// ROOMS presets

room::presets::PresetLibrary& RoomAudioProcessor::getLibrary()
{
    if (library == nullptr)
    {
        std::vector<juce::String> factory;
        for (int i = 0; i < FactoryPresets::namedResourceListSize; ++i)
        {
            int size = 0;
            if (auto* data = FactoryPresets::getNamedResource (FactoryPresets::namedResourceList[i], size))
                factory.push_back (juce::String::fromUTF8 (data, size));
        }
        library = std::make_unique<room::presets::PresetLibrary> (presetRoot, std::move (factory));
    }
    return *library;
}

void RoomAudioProcessor::setPresetRoot (const juce::File& root)
{
    presetRoot = root;
    library.reset();
}

std::map<juce::String, float> RoomAudioProcessor::currentParamValues() const
{
    std::map<juce::String, float> values;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (room::presets::isPresetParameter (rp->paramID))
                values[rp->paramID] = rp->convertFrom0to1 (rp->getValue());
    return values;
}

juce::ValueTree RoomAudioProcessor::capturePreset (const room::presets::PresetMeta& meta) const
{
    const auto a = getAnalysis();
    return room::presets::makePreset (meta, currentParamValues(),
                                      a.valid ? a.toValueTree() : juce::ValueTree());
}

void RoomAudioProcessor::applyPreset (const juce::ValueTree& preset, const juce::String& key)
{
    const auto values = room::presets::readParams (preset);

    for (auto* p : getParameters())
    {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p);
        if (rp == nullptr || ! room::presets::isPresetParameter (rp->paramID))
            continue;
        // Parameters missing from an older preset go back to their defaults.
        const auto it = values.find (rp->paramID);
        const auto norm = it != values.end() ? rp->convertTo0to1 (it->second) : rp->getDefaultValue();
        rp->beginChangeGesture();
        rp->setValueNotifyingHost (norm);
        rp->endChangeGesture();
    }

    auto a = room::RoomAnalysis::fromValueTree (preset.getChildWithName ("ROOM_ANALYSIS"));
    if (a.valid)
    {
        if (a.sourceName.isEmpty())
            a.sourceName = room::presets::readMeta (preset).name;
        setAnalysis (std::move (a));
        worker->requestRebuild();
    }
    else
    {
        clearRoom();
    }

    setCurrentPreset (key, room::presets::readMeta (preset).name);
}

void RoomAudioProcessor::setCurrentPreset (const juce::String& key, const juce::String& name)
{
    currentPresetKey = key;
    currentPresetName = name;
    snapshotPreset();
}

void RoomAudioProcessor::snapshotPreset()
{
    presetSnapshot = currentParamValues();
    restoredAsModified = false;
}

bool RoomAudioProcessor::isPresetModified() const
{
    if (restoredAsModified)
        return true;
    const auto now = currentParamValues();
    for (auto& [id, v] : now)
    {
        const auto it = presetSnapshot.find (id);
        if (it == presetSnapshot.end() || std::abs (it->second - v) > 1e-4f * std::max (1.0f, std::abs (v)))
            return true;
    }
    return false;
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
