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
        // Let a build in progress finish: a killed thread can take the host down with it.
        stopThread (20000);
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
                // While recording or while an LFO moves Intensity/Tail/Size/Damping, check often.
                wait (owner.recordArmed.load() ? 30 : owner.modulatesRebuildTargets() ? 50 : -1);

            owner.finishRecording();
            if (owner.rebuildTargetsMoved())
                owner.rebuildRoom();

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

    for (int t = 0; t < room::Modulation::numTargets(); ++t)
    {
        targetParams[(size_t) t] = apvts.getParameter (room::Modulation::targetIds()[t]);
        jassert (targetParams[(size_t) t] != nullptr);
        effValue[(size_t) t] = apvts.getRawParameterValue (room::Modulation::targetIds()[t])->load();
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

    // A brand-new ROOM starts with a room in it ("Init Room"), so sound passes
    // through the moment it's inserted. Loading a project replaces it (an
    // empty saved state stays empty: setStateInformation clears the room).
    if (startWithDefaultStudio)
        for (int i = 0; i < FactoryPresets::namedResourceListSize; ++i)
            if (juce::String (FactoryPresets::getNamedResourceOriginalFilename (FactoryPresets::namedResourceList[i])) == "Init Room.studio")
            {
                int size = 0;
                if (auto* data = FactoryPresets::getNamedResource (FactoryPresets::namedResourceList[i], size))
                    if (auto tree = room::presets::parse (juce::String::fromUTF8 (data, size)); tree.isValid())
                        applyPreset (tree, "Factory/Init Room");
                break;
            }
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

    // --- LFOs -----------------------------------------------------------------
    for (int l = 0; l < room::Modulation::numLfos; ++l)
    {
        const auto name = "LFO " + String (l + 1);
        NormalisableRange<float> rate (0.01f, 20.0f, 0.001f);
        rate.setSkewForCentre (1.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { ParamIDs::lfoRate (l), 1 }, name + " Rate", rate, l == 0 ? 0.5f : 2.0f,
            AudioParameterFloatAttributes().withStringFromValueFunction (
                [] (float v, int) { return v < 1.0f ? String (v, 2) + " Hz" : String (v, 1) + " Hz"; })));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::lfoSync (l), 1 }, name + " Sync", false));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::lfoDiv (l), 1 }, name + " Division",
                                                            room::Lfo::divisionNames(), 3));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { ParamIDs::lfoShape (l), 1 }, name + " Shape",
                                                            room::Lfo::shapeNames(), 0));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { ParamIDs::lfoRetrig (l), 1 }, name + " Retrigger", false));
    }

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

    // Input level for the room screen (peak, falling slowly).
    {
        float peak = 0.0f;
        for (int ch = 0; ch < std::min (numIn, buffer.getNumChannels()); ++ch)
            peak = std::max (peak, buffer.getMagnitude (ch, 0, buffer.getNumSamples()));
        inputLevel = std::max (peak, inputLevel.load() * 0.9f);
    }

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

    // Host transport, for synced LFOs.
    blockPlaying = false;
    blockHasPpq = false;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
        {
            blockPlaying = pos->getIsPlaying();
            if (const auto bpm = pos->getBpm()) blockBpm = *bpm;
            if (const auto ppq = pos->getPpqPosition()) { blockPpq = *ppq; blockHasPpq = true; }
        }
    for (int l = 0; l < room::Modulation::numLfos; ++l)
        if (blockPlaying && ! wasPlaying && apvts.getRawParameterValue (ParamIDs::lfoRetrig (l))->load() > 0.5f)
            lfos[l].reset();
    wasPlaying = blockPlaying;

    // With LFOs assigned, run the chain in small slices so modulation is smooth.
    const auto modActive = modulation.anyAssigned();
    const auto slice = modActive ? 64 : n;
    for (int start = 0; start < n; start += slice)
    {
        const auto len = std::min (slice, n - start);
        juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, len);
        updateModulation (start, len);

        using M = room::Modulation;
        auto eff = [this] (const char* id, std::atomic<float>* raw) { return effective (M::targetIndex (id), raw->load()); };

        room::RoomFx fx;
        fx.preDelayMs = eff (ParamIDs::preDelay, preDelayParam);
        fx.duck = eff (ParamIDs::duck, duckParam);
        fx.duckReleaseMs = eff (ParamIDs::duckRelease, duckReleaseParam);
        fx.freeze = freezeParam->load() > 0.5f;

        const auto baseBalance = balanceAutoParam->load() > 0.5f ? detectedBalance.load() : balanceParam->load();
        const auto balanceNow = juce::jlimit (0.0f, 1.0f, baseBalance + modOffset[(size_t) M::targetIndex (ParamIDs::balance)]);

        // Room -> Tape -> EQ -> Auto EQ -> Space -> output gain
        engine.process (part, balanceNow, eff (ParamIDs::mix, mixParam), 0.0f, fx);

        room::EqBand bands[room::tone::numEqBands];
        getEqBands (bands);
        toneShaper.process (part, eff (ParamIDs::tape, tapeParam), bands);
        suppressor.process (part, eff (ParamIDs::autoEq, autoEqParam));
        stereoWidth.process (part, eff (ParamIDs::space, spaceParam));

        outputGain.setTargetValue (juce::Decibels::decibelsToGain (eff (ParamIDs::output, outputParam)));
        const auto outChans = std::min (part.getNumChannels(), 2);
        if (outputGain.isSmoothing())
        {
            for (int i = 0; i < len; ++i)
            {
                const auto g = outputGain.getNextValue();
                for (int ch = 0; ch < outChans; ++ch)
                    part.getWritePointer (ch)[i] *= g;
            }
        }
        else
        {
            for (int ch = 0; ch < outChans; ++ch)
                part.applyGain (ch, 0, len, outputGain.getTargetValue());
        }
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

//==============================================================================
// LFOs

void RoomAudioProcessor::updateModulation (int samplesIntoBlock, int numSamples)
{
    using M = room::Modulation;
    const auto sr = std::max (1.0, currentSampleRate.load());

    float values[M::numLfos];
    for (int l = 0; l < M::numLfos; ++l)
    {
        const auto shape = (room::Lfo::Shape) juce::roundToInt (apvts.getRawParameterValue (ParamIDs::lfoShape (l))->load());
        const auto synced = apvts.getRawParameterValue (ParamIDs::lfoSync (l))->load() > 0.5f;
        const auto beats = room::Lfo::divisionBeats (juce::roundToInt (apvts.getRawParameterValue (ParamIDs::lfoDiv (l))->load()));

        if (synced && blockPlaying && blockHasPpq)
            lfos[l].syncTo (blockPpq + samplesIntoBlock / sr * blockBpm / 60.0, beats);   // locked to the song
        values[l] = lfos[l].value (shape);
        if (! (synced && blockPlaying && blockHasPpq))
        {
            const auto hz = synced ? blockBpm / 60.0 / beats
                                   : (double) apvts.getRawParameterValue (ParamIDs::lfoRate (l))->load();
            lfos[l].advance (numSamples, hz, sr);
        }
        modulation.liveValue[(size_t) l] = values[l];
        modulation.livePhase[(size_t) l] = (float) lfos[l].getPhase();
    }

    modOffset.fill (0.0f);
    for (int l = 0; l < M::numLfos; ++l)
        for (auto& slot : modulation.slots[(size_t) l])
            if (const auto t = slot.target.load(); t >= 0 && t < (int) modOffset.size())
                modOffset[(size_t) t] += slot.depth.load() * values[l];

    for (int t = 0; t < M::numTargets(); ++t)
    {
        modulation.liveOffset[(size_t) t] = modOffset[(size_t) t];
        if (M::isRebuildTarget (t))
            effValue[(size_t) t] = effective (t, apvts.getRawParameterValue (M::targetIds()[t])->load());
    }
}

float RoomAudioProcessor::effective (int target, float base) const
{
    if (target < 0)
        return base;
    const auto off = modOffset[(size_t) target];
    if (juce::exactlyEqual (off, 0.0f))
        return base;
    auto* p = targetParams[(size_t) target];
    return p->convertFrom0to1 (juce::jlimit (0.0f, 1.0f, p->convertTo0to1 (base) + off));
}

bool RoomAudioProcessor::modulatesRebuildTargets() const
{
    using M = room::Modulation;
    for (int l = 0; l < M::numLfos; ++l)
        for (auto& slot : modulation.slots[(size_t) l])
            if (const auto t = slot.target.load(); t >= 0 && M::isRebuildTarget (t))
                return true;
    return false;
}

float RoomAudioProcessor::effectiveForRebuild (const char* paramId) const
{
    // Not modulated: the knob itself. Modulated: what the audio thread last computed.
    const auto t = room::Modulation::targetIndex (paramId);
    const auto modulated = modulation.isAssigned (0, t) || modulation.isAssigned (1, t);
    return modulated ? effValue[(size_t) t].load() : apvts.getRawParameterValue (paramId)->load();
}

bool RoomAudioProcessor::rebuildTargetsMoved() const
{
    if (! modulatesRebuildTargets())
        return false;
    using M = room::Modulation;
    for (auto* id : { ParamIDs::intensity, ParamIDs::tail, ParamIDs::size, ParamIDs::damping })
    {
        const auto t = M::targetIndex (id);
        const auto now = effectiveForRebuild (id);
        if (std::abs (now - lastBuiltValue[(size_t) t]) > 0.01f * std::max (0.1f, std::abs (now)))
            return true;
    }
    return false;
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
    const auto mask = randomizeMask;
    if (mask & randomRoom)
    {
        set (ParamIDs::intensity, between (0.5f, 1.6f));
        set (ParamIDs::balanceAuto, maybe (0.5f) ? 1.0f : 0.0f);
        set (ParamIDs::balance, between (0.1f, 0.9f));
        set (ParamIDs::tail, between (0.6f, 4.0f));
        set (ParamIDs::space, between (0.7f, 1.6f));
    }
    if (mask & randomFinish)
    {
        set (ParamIDs::tape, maybe (0.5f) ? between (0.1f, 0.8f) : 0.0f);
        set (ParamIDs::autoEq, maybe (0.5f) ? between (0.1f, 0.6f) : 0.0f);
    }
    if (mask & randomEq)
        for (int b = 0; b < room::tone::numEqBands; ++b)
            set (ParamIDs::eqGain (b), maybe (0.4f) ? between (-4.0f, 4.0f) : 0.0f);
    if (mask & randomAdvanced)
    {
        set (ParamIDs::preDelay, maybe (0.5f) ? between (5.0f, 120.0f) : 0.0f);
        set (ParamIDs::size, between (0.7f, 1.5f));
        set (ParamIDs::damping, between (-0.5f, 0.7f));
        set (ParamIDs::duck, maybe (0.3f) ? between (0.2f, 0.7f) : 0.0f);
        set (ParamIDs::duckRelease, between (120.0f, 600.0f));
    }
    if (mask & randomLfos)
    {
        const auto numShapes = room::Lfo::shapeNames().size();
        const auto numDivs = room::Lfo::divisionNames().size();
        for (int l = 0; l < room::Modulation::numLfos; ++l)
        {
            set (ParamIDs::lfoShape (l), (float) r.nextInt (numShapes));
            const auto synced = maybe (0.5f);
            set (ParamIDs::lfoSync (l), synced ? 1.0f : 0.0f);
            if (synced)
                set (ParamIDs::lfoDiv (l), (float) r.nextInt (numDivs));
            else
                set (ParamIDs::lfoRate (l), std::exp (between (std::log (0.05f), std::log (6.0f))));
        }
    }
}

void RoomAudioProcessor::resetToInit()
{
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
            if (withId->paramID != ParamIDs::bypass && withId->paramID != ParamIDs::output)
                p->setValueNotifyingHost (p->getDefaultValue());
    clearModulation();
    setCurrentPreset ({}, {});
}

void RoomAudioProcessor::clearModulation()
{
    modulation.clear();
    for (int l = 0; l < room::Modulation::numLfos; ++l)
        for (const auto& id : { ParamIDs::lfoRate (l), ParamIDs::lfoSync (l), ParamIDs::lfoDiv (l),
                                ParamIDs::lfoShape (l), ParamIDs::lfoRetrig (l) })
            if (auto* p = apvts.getParameter (id))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->getDefaultValue());
                p->endChangeGesture();
            }
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
    // A dropped sound replaces the room. The STUDIO stays loaded and shows as
    // modified once the new room is in.
    currentRoomKey = {};
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
    currentRoomKey = {};
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
    analyseAudio (take, currentSampleRate.load(), "Recorded room " + juce::String (recordCount), "Recorded");
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
    analyseAudio (audio, reader->sampleRate, file.getFileNameWithoutExtension(), "File");
}

void RoomAudioProcessor::analyseAudio (const juce::AudioBuffer<float>& audio, double sampleRate, const juce::String& name,
                                       const juce::String& origin)
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
    result.origin = origin;
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

    // Knob values, or where an LFO has moved them to.
    const auto intensity = effectiveForRebuild (ParamIDs::intensity);
    const auto tail = effectiveForRebuild (ParamIDs::tail);
    const auto size = effectiveForRebuild (ParamIDs::size);
    const auto damping = effectiveForRebuild (ParamIDs::damping);
    using M = room::Modulation;
    lastBuiltValue[(size_t) M::targetIndex (ParamIDs::intensity)] = intensity;
    lastBuiltValue[(size_t) M::targetIndex (ParamIDs::tail)] = tail;
    lastBuiltValue[(size_t) M::targetIndex (ParamIDs::size)] = size;
    lastBuiltValue[(size_t) M::targetIndex (ParamIDs::damping)] = damping;

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
    state.setProperty ("tipsEnabled", tipsEnabled, nullptr);
    state.setProperty ("randomizeMask", randomizeMask, nullptr);
    state.setProperty ("roomKey", currentRoomKey, nullptr);
    state.appendChild (apvts.copyState(), nullptr);
    state.appendChild (getAnalysis().toValueTree(), nullptr);
    state.appendChild (modulation.toValueTree(), nullptr);
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

    modulation.fromValueTree (state.getChildWithName ("MODULATION"));
    editorScale = juce::jlimit (0.75f, 1.75f, (float) state.getProperty ("editorScale", 1.0f));
    advancedOpen = (bool) state.getProperty ("advancedOpen", false);
    randomizeMask = (int) state.getProperty ("randomizeMask", (int) randomDefault) & randomAll;
    tipsEnabled = (bool) state.getProperty ("tipsEnabled", true);
    currentRoomKey = state["roomKey"].toString();
    currentPresetKey = state["presetKey"].toString();
    currentPresetName = state["presetName"].toString();
    snapshotPreset();
    restoredAsModified = (bool) state["presetModified"];
}

//==============================================================================
// ROOMS presets

void RoomAudioProcessor::ensureLibraries()
{
    if (library != nullptr && roomLibrary != nullptr)
        return;
    using room::presets::PresetLibrary;
    using room::presets::Kind;

    // Built-in content: .room files are factory ROOMS, .studio files factory STUDIOS.
    std::vector<juce::String> studios, rooms;
    for (int i = 0; i < FactoryPresets::namedResourceListSize; ++i)
    {
        int size = 0;
        const auto* name = FactoryPresets::namedResourceList[i];
        if (auto* data = FactoryPresets::getNamedResource (name, size))
        {
            const auto file = juce::String (FactoryPresets::getNamedResourceOriginalFilename (name));
            (file.endsWithIgnoreCase (room::presets::roomExtension) ? rooms : studios)
                .push_back (juce::String::fromUTF8 (data, size));
        }
    }

    const auto roomsRoot = PresetLibrary::rootFor (presetBase, Kind::room);
    const auto studiosRoot = PresetLibrary::rootFor (presetBase, Kind::studio);
    PresetLibrary::migrateLegacy (roomsRoot, studiosRoot);
    library = std::make_unique<PresetLibrary> (studiosRoot, std::move (studios), Kind::studio);
    roomLibrary = std::make_unique<PresetLibrary> (roomsRoot, std::move (rooms), Kind::room);
}

room::presets::PresetLibrary& RoomAudioProcessor::getLibrary()
{
    ensureLibraries();
    return *library;
}

room::presets::PresetLibrary& RoomAudioProcessor::getRoomLibrary()
{
    ensureLibraries();
    return *roomLibrary;
}

void RoomAudioProcessor::setPresetRoot (const juce::File& base)
{
    presetBase = base;
    library.reset();
    roomLibrary.reset();
}

juce::ValueTree RoomAudioProcessor::captureRoom (const room::presets::PresetMeta& meta) const
{
    const auto a = getAnalysis();
    return a.valid ? room::presets::makeRoom (meta, a.toValueTree()) : juce::ValueTree();
}

void RoomAudioProcessor::applyRoom (const juce::ValueTree& roomTree, const juce::String& key)
{
    auto a = room::RoomAnalysis::fromValueTree (roomTree.getChildWithName ("ROOM_ANALYSIS"));
    if (! a.valid)
        return;
    a.sourceName = room::presets::readMeta (roomTree).name;   // the room's own name
    setAnalysis (std::move (a));
    worker->requestRebuild();
    currentRoomKey = key;   // the STUDIO now shows as modified (its room changed)
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
    auto m = meta;
    m.roomName = a.valid ? a.sourceName : juce::String();
    auto preset = room::presets::makePreset (m, currentParamValues(),
                                             a.valid ? a.toValueTree() : juce::ValueTree());
    preset.appendChild (modulation.toValueTree(), nullptr);
    return preset;
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

    // LFO assignments (presets from before LFOs existed simply have none).
    modulation.fromValueTree (preset.getChildWithName ("MODULATION"));

    auto a = room::RoomAnalysis::fromValueTree (preset.getChildWithName ("ROOM_ANALYSIS"));
    currentRoomKey = {};
    if (a.valid)
    {
        const auto meta = room::presets::readMeta (preset);
        if (meta.roomName.isNotEmpty())
        {
            a.sourceName = meta.roomName;
            // If that room is in ROOMS (same pack first, then Factory, then User), it's the current ROOM.
            const auto file = room::presets::toFileName (meta.roomName);
            for (auto pack : { key.upToFirstOccurrenceOf ("/", false, false), juce::String ("Factory"), juce::String ("User") })
                if (currentRoomKey.isEmpty() && getRoomLibrary().find (pack + "/" + file) != nullptr)
                    currentRoomKey = pack + "/" + file;
        }
        else if (a.sourceName.isEmpty())
            a.sourceName = meta.name;
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
    modulationSnapshot = modulation.toValueTree().toXmlString();
    roomSnapshot = getAnalysis().toValueTree().toXmlString();
    restoredAsModified = false;
}

bool RoomAudioProcessor::isPresetModified() const
{
    if (restoredAsModified || modulation.toValueTree().toXmlString() != modulationSnapshot)
        return true;
    if (getAnalysis().toValueTree().toXmlString() != roomSnapshot)
        return true;   // a different room was loaded, dropped or recorded
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
