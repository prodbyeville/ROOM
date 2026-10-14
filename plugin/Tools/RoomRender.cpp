// room_render: runs the plugin's exact DSP offline, for testing without a DAW.
//
//   room_render SOURCE TARGET OUT [--intensity 1] [--mix 0.5] [--balance auto|0..1] [--tail 2.5]
//
// Audio is processed in 512-sample blocks, like a host would.

#include "dsp/RoomEngine.h"
#include "dsp/ToneShaper.h"
#include "dsp/ResonanceSuppressor.h"
#include "dsp/StereoWidth.h"
#include "presets/PresetFile.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <thread>

using namespace room;

static juce::AudioBuffer<float> readFile (juce::AudioFormatManager& fm, const juce::File& f, double& sr)
{
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr)
    {
        std::cerr << "Can't read " << f.getFullPathName() << "\n";
        std::exit (1);
    }
    juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
    r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
    sr = r->sampleRate;
    return b;
}

int main (int argc, char* argv[])
{
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    if (args.size() < 3)
    {
        std::cerr << "usage: room_render SOURCE TARGET OUT [--intensity X] [--mix X] [--balance auto|X] [--tail S]\n"
                     "                   [--tape X] [--auto X] [--space X] [--eq band:freq:gain:q,...] [--raw]\n"
                     "                   [--room-name R]   (name the room itself)\n"
                     "                   [--save-preset FILE.studio --name N --type T --styles a,b --made-for a,b --notes TEXT]\n"
                     "                   [--save-room FILE.room --room-type T --room-styles a,b --room-notes TEXT]\n";
        return 1;
    }

    auto option = [&] (const juce::String& name, const juce::String& def)
    {
        const auto i = args.indexOf (name);
        return i >= 0 && i + 1 < args.size() ? args[i + 1] : def;
    };
    const auto intensity = option ("--intensity", "1").getFloatValue();
    const auto mix = option ("--mix", "0.5").getFloatValue();
    const auto balanceArg = option ("--balance", "auto");
    const auto tail = option ("--tail", "2.5").getFloatValue();
    const auto tape = option ("--tape", "0").getFloatValue();
    const auto autoEq = option ("--auto", "0").getFloatValue();
    const auto space = option ("--space", "1").getFloatValue();

    // EQ bands: defaults, then any "--eq 3:1000:6:1,5:8000:-3:0.7" overrides (band is 1-5).
    EqBand bands[tone::numEqBands];
    for (int b = 0; b < tone::numEqBands; ++b)
        bands[b] = { tone::kBandTypes[b], tone::kBandFreqs[b], 0.0f, tone::kBandQs[b] };
    for (auto& spec : juce::StringArray::fromTokens (option ("--eq", ""), ",", ""))
    {
        const auto f = juce::StringArray::fromTokens (spec, ":", "");
        const auto b = f[0].getIntValue() - 1;
        if (f.size() == 4 && b >= 0 && b < tone::numEqBands)
            bands[b] = { tone::kBandTypes[b], f[1].getFloatValue(), f[2].getFloatValue(), f[3].getFloatValue() };
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    double sourceSr = 0, targetSr = 0;
    const auto source = readFile (fm, juce::File::getCurrentWorkingDirectory().getChildFile (args[0]), sourceSr);
    auto target = readFile (fm, juce::File::getCurrentWorkingDirectory().getChildFile (args[1]), targetSr);

    // --- analysis + synthesis (in the plugin this runs on a background thread)
    auto analysis = analyzeRoom (source, sourceSr);
    if (! analysis.valid)
    {
        std::cerr << "Room source too short to analyse\n";
        return 1;
    }
    const auto balance = balanceArg == "auto" ? analysis.balance : balanceArg.getFloatValue();

    std::cout << "Balance " << analysis.balance << (balanceArg == "auto" ? " (detected)" : " (overridden)")
              << ", median T60 " << analysis.medianT60 << "s, steady " << analysis.steady
              << ", spatial ev " << analysis.spatialEvidence << ", tonal ev " << analysis.tonalEvidence << "\n";
    std::cout << "T60s:";
    for (int b = 0; b < analysis.numBands(); ++b)
        std::cout << " " << juce::String (analysis.bandT60s[(size_t) b], 2) << (analysis.bandConfident[(size_t) b] ? "" : "?");
    std::cout << "\nPeaks:";
    for (auto& p : analysis.peaks)
        std::cout << " " << juce::String (p.freqHz, 1) << "Hz/+" << juce::String (p.gainDb, 1) << "dB/Q" << juce::String (p.q, 1);
    std::cout << "\n";

    // The room's own name (and, for the built-in content, where it came from).
    const auto roomName = option ("--room-name", "");
    if (roomName.isNotEmpty())
    {
        analysis.sourceName = roomName;
        analysis.origin = "Factory";
    }

    // Optional: write just the room as a ROOM.
    if (const auto roomPath = option ("--save-room", ""); roomPath.isNotEmpty())
    {
        presets::PresetMeta meta;
        meta.name = roomName.isNotEmpty() ? roomName : juce::File (roomPath).getFileNameWithoutExtension();
        meta.author = option ("--author", "Eville");
        meta.type = option ("--room-type", "Room");
        meta.styles = juce::StringArray::fromTokens (option ("--room-styles", ""), ",", "");
        meta.notes = option ("--room-notes", "");
        meta.created = juce::Time::getCurrentTime();
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (roomPath);
        if (! presets::save (presets::makeRoom (meta, analysis.toValueTree()), file))
        {
            std::cerr << "Couldn't write room " << file.getFullPathName() << "\n";
            return 1;
        }
        std::cout << "Saved room " << file.getFullPathName() << "\n";
    }

    // Optional: also write these settings + this room as a STUDIO.
    if (const auto presetPath = option ("--save-preset", ""); presetPath.isNotEmpty())
    {
        presets::PresetMeta meta;
        meta.name = option ("--name", juce::File (presetPath).getFileNameWithoutExtension());
        meta.author = option ("--author", "Eville");
        meta.type = option ("--type", "Room");
        meta.styles = juce::StringArray::fromTokens (option ("--styles", ""), ",", "");
        meta.notes = option ("--notes", "");
        meta.madeFor = juce::StringArray::fromTokens (option ("--made-for", ""), ",", "");
        meta.roomName = roomName;
        meta.created = juce::Time::getCurrentTime();

        std::map<juce::String, float> params {
            { "intensity", intensity }, { "mix", mix }, { "tail", tail }, { "tape", tape },
            { "autoEq", autoEq }, { "space", space },
            { "balanceAuto", balanceArg == "auto" ? 1.0f : 0.0f },
            { "balance", balanceArg == "auto" ? 0.5f : balanceArg.getFloatValue() } };
        for (int b = 0; b < tone::numEqBands; ++b)
        {
            const auto n = juce::String (b + 1);
            params["eq" + n + "Freq"] = bands[b].freqHz;
            params["eq" + n + "Gain"] = bands[b].gainDb;
            params["eq" + n + "Q"] = bands[b].q;
        }
        auto a = analysis;
        if (roomName.isEmpty())
            a.sourceName = meta.name;
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile (presetPath);
        if (! presets::save (presets::makePreset (meta, params, a.toValueTree()), file))
        {
            std::cerr << "Couldn't write preset " << file.getFullPathName() << "\n";
            return 1;
        }
        std::cout << "Saved preset " << file.getFullPathName() << "\n";
    }

    const int block = 512;
    const auto chans = 2;
    RoomEngine engine;
    engine.prepare (targetSr, block, chans);
    engine.submitRoom (synthesizeIR (analysis, targetSr, tail, intensity),
                       targetSr, designTone (analysis, targetSr, intensity));

    // Wait for the convolution to finish loading the IR (it loads on its own thread).
    juce::AudioBuffer<float> io (chans, block);
    for (int tries = 0; tries < 400; ++tries)
    {
        io.clear();
        engine.process (io, balance, mix, 0.0f);
        if (engine.isRoomActive() && tries > 40)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    }
    engine.reset();

    // Post-room tone shaping, as in the plugin.
    ToneShaper toneShaper;
    toneShaper.prepare (targetSr, chans);
    ResonanceSuppressor suppressor;
    suppressor.prepare (targetSr, chans);
    StereoWidth stereoWidth;
    stereoWidth.prepare (targetSr);
    const auto latency = suppressor.getLatency();

    // --- process the target plus the tail, block by block
    // (plus the Auto EQ latency, which is removed again afterwards)
    const auto inLen = target.getNumSamples();
    const auto total = inLen + (int) (tail * targetSr);
    const auto processed = total + latency;
    juce::AudioBuffer<float> outWithLatency (chans, processed);
    outWithLatency.clear();
    for (int pos = 0; pos < processed; pos += block)
    {
        const auto len = std::min (block, processed - pos);
        io.setSize (chans, len, false, false, true);
        io.clear();
        for (int ch = 0; ch < chans; ++ch)
        {
            const auto srcCh = std::min (ch, target.getNumChannels() - 1);
            if (pos < inLen)
                io.copyFrom (ch, 0, target, srcCh, pos, std::min (len, inLen - pos));
        }
        engine.process (io, balance, mix, 0.0f);
        toneShaper.process (io, tape, bands);
        suppressor.process (io, autoEq);
        stereoWidth.process (io, space);
        for (int ch = 0; ch < chans; ++ch)
            outWithLatency.copyFrom (ch, pos, io, ch, 0, len);
    }
    juce::AudioBuffer<float> out (chans, total);
    for (int ch = 0; ch < chans; ++ch)
        out.copyFrom (ch, 0, outWithLatency, ch, latency, total);

    // Trim trailing near-silence and keep peaks under -1 dBFS (as the prototype did).
    const auto peak = out.getMagnitude (0, total);
    int end = inLen;
    for (int i = total - 1; i >= inLen; --i)
        if (std::abs (out.getSample (0, i)) > peak * 1e-4f || std::abs (out.getSample (1, i)) > peak * 1e-4f)
        {
            end = i + 1;
            break;
        }
    const auto limit = juce::Decibels::decibelsToGain (-1.0f);
    if (peak > limit && ! args.contains ("--raw"))
        out.applyGain (limit / peak);

    juce::File outFile = juce::File::getCurrentWorkingDirectory().getChildFile (args[2]);
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (outFile.createOutputStream());
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (targetSr)
                                                   .withNumChannels (chans)
                                                   .withBitsPerSample (24));
    if (writer == nullptr)
    {
        std::cerr << "Can't write " << outFile.getFullPathName() << "\n";
        return 1;
    }
    writer->writeFromAudioSampleBuffer (out, 0, end);
    std::cout << "Wrote " << outFile.getFullPathName() << " (" << (double) end / targetSr << "s)\n";
    return 0;
}
