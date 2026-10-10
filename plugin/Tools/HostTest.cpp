// room_hosttest: loads the built ROOM.vst3 like a DAW would and checks the
// parts pluginval can't reach without a room loaded:
//
//   1. a room restored from saved state is rebuilt and actually processes audio
//   2. the output matches room_render (the same DSP run directly)
//   3. hammering Intensity / Tail while audio runs doesn't crash or blow up
//   4. saving and reloading the state keeps the room
//
//   room_hosttest PLUGIN.vst3 ROOM_SOURCE TARGET OUT.wav ["command to run while the UI is open"]

#include "dsp/RoomAnalysis.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <iostream>
#include <thread>

static int fail (const juce::String& msg)
{
    std::cerr << "FAIL: " << msg << "\n";
    return 1;
}

static juce::AudioBuffer<float> readFile (const juce::File& f, double& sr)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr)
        return {};
    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    sr = r->sampleRate;
    return b;
}

/** When JUCE hosts a VST3, getStateInformation() wraps the plugin's own state
    in a <VST3PluginState><IComponent>base64</IComponent></VST3PluginState>
    envelope. These two helpers unwrap it and wrap it back up. */
static std::unique_ptr<juce::XmlElement> unwrapPluginState (const juce::MemoryBlock& hostState)
{
    auto outer = juce::AudioProcessor::getXmlFromBinary (hostState.getData(), (int) hostState.getSize());
    if (outer == nullptr)
        return {};
    auto* comp = outer->getChildByName ("IComponent");
    if (comp == nullptr)
        return {};
    juce::MemoryBlock inner;
    inner.fromBase64Encoding (comp->getAllSubText());
    return juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize());
}

static juce::MemoryBlock wrapPluginState (const juce::MemoryBlock& hostState, const juce::XmlElement& pluginXml)
{
    auto outer = juce::AudioProcessor::getXmlFromBinary (hostState.getData(), (int) hostState.getSize());
    juce::MemoryBlock inner, result;
    juce::AudioProcessor::copyXmlToBinary (pluginXml, inner);
    auto* comp = outer->getChildByName ("IComponent");
    comp->deleteAllTextElements();
    comp->addTextElement (inner.toBase64Encoding());
    juce::AudioProcessor::copyXmlToBinary (*outer, result);
    return result;
}

/** Run audio through the plugin in 512-sample blocks. */
static juce::AudioBuffer<float> run (juce::AudioPluginInstance& p, const juce::AudioBuffer<float>& in,
                                     int totalLength, std::function<void (int)> perBlock = {})
{
    const int block = 512;
    juce::AudioBuffer<float> out (2, totalLength), io (2, block);
    juce::MidiBuffer midi;
    out.clear();
    int blockIndex = 0;
    for (int pos = 0; pos < totalLength; pos += block, ++blockIndex)
    {
        const auto len = std::min (block, totalLength - pos);
        io.setSize (2, len, false, false, true);
        io.clear();
        for (int ch = 0; ch < 2; ++ch)
            if (pos < in.getNumSamples())
                io.copyFrom (ch, 0, in, std::min (ch, in.getNumChannels() - 1), pos,
                             std::min (len, in.getNumSamples() - pos));
        if (perBlock)
            perBlock (blockIndex);
        p.processBlock (io, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, io, ch, 0, len);
    }
    return out;
}

static juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& name)
{
    for (auto* prm : p.getParameters())
        if (prm->getName (64) == name)
            return prm;
    return nullptr;
}

/** Feed silence until the background thread has built the room. */
static void waitForRoom (juce::AudioPluginInstance& p)
{
    juce::AudioBuffer<float> silence (2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 300; ++i)
    {
        silence.clear();
        p.processBlock (silence, midi);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
    p.reset();
}

int main (int argc, char* argv[])
{
    if (argc < 5)
        return fail ("usage: room_hosttest PLUGIN.vst3 ROOM_SOURCE TARGET OUT.wav");

    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto pluginFile = cwd.getChildFile (argv[1]);

    // --- load the VST3 -------------------------------------------------------
    juce::AudioPluginFormatManager pfm;
    juce::addDefaultFormatsToManager (pfm);
    juce::OwnedArray<juce::PluginDescription> types;
    for (auto* format : pfm.getFormats())
        format->findAllTypesForFile (types, pluginFile.getFullPathName());
    if (types.isEmpty())
        return fail ("couldn't find a plugin in " + pluginFile.getFullPathName());

    double targetSr = 0, sourceSr = 0;
    const auto target = readFile (cwd.getChildFile (argv[3]), targetSr);
    const auto source = readFile (cwd.getChildFile (argv[2]), sourceSr);
    if (target.getNumSamples() == 0 || source.getNumSamples() == 0)
        return fail ("couldn't read the audio files");

    juce::String err;
    auto plugin = pfm.createPluginInstance (*types[0], targetSr, 512, err);
    if (plugin == nullptr)
        return fail ("couldn't open plugin: " + err);
    std::cout << "Loaded " << plugin->getName() << "\n";

    plugin->enableAllBuses();
    plugin->prepareToPlay (targetSr, 512);

    // --- 1. give it a room through saved state --------------------------------
    // Build a state blob the same way the plugin saves one: its own parameter
    // state plus the analysed room numbers.
    juce::MemoryBlock emptyState;
    plugin->getStateInformation (emptyState);
    auto xml = unwrapPluginState (emptyState);
    if (xml == nullptr || ! xml->hasTagName ("ROOM_STATE"))
        return fail ("plugin state isn't the expected ROOM_STATE XML");
    auto state = juce::ValueTree::fromXml (*xml);
    auto analysis = room::analyzeRoom (source, sourceSr);
    analysis.sourceName = cwd.getChildFile (argv[2]).getFileName();
    state.removeChild (state.getChildWithName ("ROOM_ANALYSIS"), nullptr);
    state.appendChild (analysis.toValueTree(), nullptr);

    const auto withRoom = wrapPluginState (emptyState, *state.createXml());
    plugin->setStateInformation (withRoom.getData(), (int) withRoom.getSize());
    waitForRoom (*plugin);

    const auto tailSamples = (int) (2.5 * targetSr);
    const auto total = target.getNumSamples() + tailSamples;
    auto out = run (*plugin, target, total);

    // --- 2. it must actually do something, and stay sane -----------------------
    double diff = 0.0, tailEnergy = 0.0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < total; ++i)
        {
            const auto dry = i < target.getNumSamples() ? target.getSample (std::min (ch, target.getNumChannels() - 1), i) : 0.0f;
            const auto y = out.getSample (ch, i);
            if (! std::isfinite (y))
                return fail ("non-finite output");
            diff += std::abs (y - dry);
            if (i >= target.getNumSamples())
                tailEnergy += (double) y * y;
        }
    std::cout << "Mean |wet - dry| " << diff / (2.0 * total) << ", tail energy " << tailEnergy
              << ", peak " << out.getMagnitude (0, total) << "\n";
    if (diff / (2.0 * total) < 1e-4)
        return fail ("plugin output is identical to the input (room not active?)");
    if (out.getMagnitude (0, total) > 8.0f)
        return fail ("output level is unreasonably high");

    // Write it so it can be compared with room_render.
    {
        auto outFile = cwd.getChildFile (argv[4]);
        outFile.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (outFile.createOutputStream());
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                       .withSampleRate (targetSr)
                                                       .withNumChannels (2)
                                                       .withBitsPerSample (24));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (out, 0, total);
    }

    // --- 3. stress: move Intensity and Tail every few blocks while playing -----
    auto* intensity = findParam (*plugin, "Intensity");
    auto* tail = findParam (*plugin, "Tail");
    if (intensity == nullptr || tail == nullptr)
        return fail ("missing Intensity/Tail parameters");

    juce::Random rng (42);
    for (int round = 0; round < 3; ++round)
    {
        auto stress = run (*plugin, target, total, [&] (int b)
        {
            if (b % 3 == 0) intensity->setValueNotifyingHost (rng.nextFloat());
            if (b % 7 == 0) tail->setValueNotifyingHost (rng.nextFloat());
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        });
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < total; ++i)
                if (! std::isfinite (stress.getSample (ch, i)) || std::abs (stress.getSample (ch, i)) > 50.0f)
                    return fail ("output blew up while moving parameters");
    }
    std::cout << "Parameter stress: ok\n";

    // --- 4. save, reload into a fresh instance, room must survive ---------------
    juce::MemoryBlock saved;
    plugin->getStateInformation (saved);
    if (auto savedXml = unwrapPluginState (saved))
    {
        if (! savedXml->getChildByName ("ROOM_ANALYSIS") || ! (bool) savedXml->getChildByName ("ROOM_ANALYSIS")->getBoolAttribute ("valid"))
            return fail ("saved state doesn't contain the room");
    }
    else
        return fail ("saved state unreadable");
    auto plugin2 = pfm.createPluginInstance (*types[0], targetSr, 512, err);
    plugin2->enableAllBuses();
    plugin2->prepareToPlay (targetSr, 512);
    plugin2->setStateInformation (saved.getData(), (int) saved.getSize());
    waitForRoom (*plugin2);
    auto out2 = run (*plugin2, target, total);
    double diff2 = 0.0;
    for (int i = 0; i < target.getNumSamples(); ++i)
        diff2 += std::abs (out2.getSample (0, i) - target.getSample (0, i));
    if (diff2 / target.getNumSamples() < 1e-4)
        return fail ("room was lost after save + reload");
    std::cout << "Save + reload: ok\n";

    // Optional: show the plugin window on screen for a few seconds and run a
    // command (e.g. a screen capture) while it's open.
    if (argc > 5)
    {
        if (auto* editor = plugin->createEditorIfNeeded())
        {
            juce::DocumentWindow window ("ROOM", juce::Colours::black, 0);
            window.setUsingNativeTitleBar (false);
            window.setTitleBarHeight (0);
            window.setContentNonOwned (editor, true);
            window.setTopLeftPosition (0, 0);
            window.setVisible (true);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (800);
            std::system (argv[5]);
            window.clearContentComponent();
            delete editor;
        }
    }

    plugin->releaseResources();
    plugin2->releaseResources();
    std::cout << "ALL HOST TESTS PASSED\n";
    return 0;
}
