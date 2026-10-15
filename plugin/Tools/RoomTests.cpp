// room_tests: ROOMS presets, Space, and state restore, tested against the real
// processor (no DAW needed).
//
//   room_tests                                           run all tests
//   room_tests --screenshot OUT.png MODE ROOM_SRC TARGET  save a UI snapshot
//        MODE = main | browser | save | rooms | roomsave | advanced | record | lfo | info | menu   [SCALE, e.g. 0.75]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/PartitionedConvolver.h"
#include "dsp/ResonanceSuppressor.h"
#include "presets/PresetLibrary.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <thread>

using namespace room::presets;

namespace
{
    int failures = 0, checks = 0;

    void check (bool ok, const juce::String& what)
    {
        ++checks;
        if (! ok)
        {
            ++failures;
            std::cout << "  FAIL: " << what << "\n";
        }
    }

    float paramValue (RoomAudioProcessor& p, const juce::String& id)
    {
        auto* rp = p.apvts.getParameter (id);
        return rp->convertFrom0to1 (rp->getValue());
    }

    void setParam (RoomAudioProcessor& p, const juce::String& id, float v)
    {
        auto* rp = p.apvts.getParameter (id);
        rp->setValueNotifyingHost (rp->convertTo0to1 (v));
    }

    juce::AudioBuffer<float> readAudio (const juce::File& f, double& sr)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr)
            return {};
        juce::AudioBuffer<float> b ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&b, 0, b.getNumSamples(), 0, true, true);
        sr = r->sampleRate;
        return b;
    }

    /** Give the background analysis thread time to finish. */
    void waitForAnalysis (RoomAudioProcessor& p)
    {
        for (int i = 0; i < 500 && p.getStatus() == RoomAudioProcessor::Status::analyzing; ++i)
            juce::Thread::sleep (10);
        juce::Thread::sleep (100);
    }

    juce::String analysisXml (RoomAudioProcessor& p)
    {
        auto t = p.getAnalysis().toValueTree();
        t.removeProperty ("sourceName", nullptr);
        return t.toXmlString();
    }

    //==========================================================================

    void testLibrary (const juce::File& root)
    {
        std::cout << "Library\n";
        RoomAudioProcessor proc;
        proc.setPresetRoot (root);
        auto& lib = proc.getLibrary();

        check (lib.countInPack (PresetLibrary::factoryPack) >= 10, "factory presets are present");
        check (lib.getUnreadableCount() == 0, "all factory presets parse");
        check (lib.getPacks()[0] == "Factory" && lib.getPacks()[1] == "User", "pack order: Factory, User, ...");
        const auto studios = root.getChildFile ("STUDIOS");
        check (studios.getChildFile ("User").isDirectory(), "User pack folder is created");

        const auto factoryKey = lib.getEntries().front().key;
        check (! lib.remove (factoryKey), "factory presets can't be deleted");
        juce::String err, key;
        PresetMeta m;
        m.name = "Nope";
        check (! lib.updateMeta (factoryKey, m, "User", key, err), "factory presets can't be edited");

        // Sorting
        PresetLibrary::Query q;
        auto names = [&] (const std::vector<int>& idx)
        {
            juce::StringArray s;
            for (auto i : idx) s.add (lib.getEntries()[(size_t) i].meta.name);
            return s;
        };
        auto sorted = names (lib.query (q));
        auto expect = sorted;
        expect.sortNatural();
        check (sorted == expect, "sort by name ascending");
        q.ascending = false;
        auto desc = names (lib.query (q));
        check (desc[0] == expect[expect.size() - 1], "sort by name descending");

        q = {};
        q.sortBy = PresetLibrary::SortColumn::type;
        const auto byType = lib.query (q);
        bool typeOrdered = true;
        for (size_t i = 1; i < byType.size(); ++i)
            typeOrdered = typeOrdered && lib.getEntries()[(size_t) byType[i - 1]].meta.type
                                             .compareNatural (lib.getEntries()[(size_t) byType[i]].meta.type) <= 0;
        check (typeOrdered, "sort by type");

        // Filters
        q = {};
        q.types = { "Hall" };
        const auto halls = lib.query (q);
        check (! halls.empty(), "type filter finds halls");
        for (auto i : halls)
            check (lib.getEntries()[(size_t) i].meta.type == "Hall", "type filter only returns halls");
        q = {};
        q.styles = { "Metallic", "Experimental" };
        for (auto i : lib.query (q))
            check (lib.getEntries()[(size_t) i].meta.styles.contains ("Metallic")
                       && lib.getEntries()[(size_t) i].meta.styles.contains ("Experimental"),
                   "style filter requires all chosen styles");
        q = {};
        q.search = "bottle";
        const auto found = names (lib.query (q));
        check (found.size() >= 1 && found.joinIntoString (",").containsIgnoreCase ("bottle"), "search by name");

        // Favorites (including factory ones) survive a fresh library
        lib.setFavorite (factoryKey, true);
        check (lib.countFavorites() == 1, "favorite counted");
        q = {};
        q.favoritesOnly = true;
        check (lib.query (q).size() == 1, "favorites filter");
        q = {};
        q.sortBy = PresetLibrary::SortColumn::favorite;
        check (lib.getEntries()[(size_t) lib.query (q).front()].key == factoryKey, "sort by favorite puts hearts first");
        proc.setPresetRoot (root);   // a brand-new library reading from disk
        check (proc.getLibrary().find (factoryKey) != nullptr && proc.getLibrary().find (factoryKey)->favorite,
               "favorites persist on disk");
        proc.getLibrary().setFavorite (factoryKey, false);

        // Damaged files are skipped, not fatal
        studios.getChildFile ("User").getChildFile ("broken.studio").replaceWithText ("<ROOM_PRESET this is not xml");
        studios.getChildFile ("User").getChildFile ("wrong.studio").replaceWithText ("<SOMETHING_ELSE/>");
        proc.getLibrary().rescan();
        check (proc.getLibrary().getUnreadableCount() == 2, "damaged / foreign files are counted as unreadable");
        studios.getChildFile ("User").getChildFile ("broken.studio").deleteFile();
        studios.getChildFile ("User").getChildFile ("wrong.studio").deleteFile();

        // Pack names
        check (! PresetLibrary::isValidPackName ("Factory"), "can't create a pack called Factory");
        check (! PresetLibrary::isValidPackName ("a/b"), "pack names can't contain slashes");
        check (PresetLibrary::isValidPackName ("Eville"), "normal pack names are fine");
        check (toFileName ("  Big: Room?. ") == "Big Room", "file names are made safe");
    }

    //==========================================================================

    void testSaveLoad (const juce::File& root, const juce::File& roomSource)
    {
        std::cout << "Save / load\n";
        RoomAudioProcessor proc;
        proc.setPresetRoot (root);
        proc.setPlayConfigDetails (2, 2, 48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        auto& lib = proc.getLibrary();

        // Load a factory preset: params + room come back, Output untouched.
        setParam (proc, ParamIDs::output, -3.0f);
        const auto* factory = lib.find ("Factory/Big Hall");
        check (factory != nullptr, "Factory/Big Hall exists");
        if (factory == nullptr)
            return;
        const auto factoryTree = lib.loadPreset (*factory);
        proc.applyPreset (factoryTree, factory->key);
        for (auto& [id, v] : readParams (factoryTree))
            check (std::abs (paramValue (proc, id) - v) < 0.011f * std::max (1.0f, std::abs (v)), "factory param " + id + " applied");
        check (std::abs (paramValue (proc, ParamIDs::output) + 3.0f) < 0.05f, "Output is not touched by presets");
        check (proc.getAnalysis().valid, "factory room loaded");
        check (proc.getCurrentPresetName() == "Big Hall" && ! proc.isPresetModified(), "current preset set, not modified");
        setParam (proc, ParamIDs::tape, 0.7f);
        check (proc.isPresetModified(), "moving a knob marks the preset modified");

        // Build a custom sound: a dropped room + every knob changed, then save it.
        proc.loadRoomFile (roomSource);
        waitForAnalysis (proc);
        check (proc.getAnalysis().valid, "dropped room analysed");
        check (proc.getCurrentPresetKey() == "Factory/Big Hall" && proc.isPresetModified(),
               "dropping a sound swaps the room: the STUDIO stays, marked modified");
        check (proc.getAnalysis().sourceName == roomSource.getFileNameWithoutExtension() && proc.getAnalysis().origin == "File",
               "a dropped room is named after the file");

        juce::Random rng (7);
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (rp->paramID != ParamIDs::output)
                    rp->setValueNotifyingHost (rng.nextFloat());
        std::map<juce::String, float> saved;
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                saved[rp->paramID] = rp->convertFrom0to1 (rp->getValue());
        const auto savedRoom = analysisXml (proc);

        PresetMeta meta;
        meta.name = "My Test Room";
        meta.author = "Tester";
        meta.type = "Chamber";
        meta.styles = { "Warm", "Wide" };
        meta.notes = "made by room_tests";
        juce::String key, err;
        check (lib.save (proc.capturePreset (meta), "User", false, key, err), "save: " + err);
        check (key == "User/My Test Room", "saved key");
        check (root.getChildFile ("STUDIOS/User/My Test Room.studio").existsAsFile(), "file written");
        check (! lib.save (proc.capturePreset (meta), "User", false, key, err) && err.contains ("already exists"),
               "saving over an existing name needs overwrite");
        check (lib.save (proc.capturePreset (meta), "User", true, key, err), "overwrite works");

        // Scramble everything, then load the preset back.
        setParam (proc, ParamIDs::output, 4.0f);
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
                if (rp->paramID != ParamIDs::output)
                    rp->setValueNotifyingHost (rng.nextFloat());
        proc.applyPreset (lib.loadPreset (*lib.find (key)), key);
        waitForAnalysis (proc);

        int mismatches = 0;
        for (auto* p : proc.getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                const auto now = rp->convertFrom0to1 (rp->getValue());
                const auto want = saved[rp->paramID];
                if (isPresetParameter (rp->paramID) && std::abs (now - want) > 1e-4f * std::max (1.0f, std::abs (want)))
                {
                    ++mismatches;
                    std::cout << "    " << rp->paramID << " " << now << " vs " << want << "\n";
                }
            }
        check (mismatches == 0, "every knob restored exactly");
        check (std::abs (paramValue (proc, ParamIDs::output) - 4.0f) < 0.05f, "Output left alone on load");
        check (analysisXml (proc) == savedRoom, "room analysis restored exactly");
        check (proc.getCurrentPresetKey() == key && ! proc.isPresetModified(), "loaded preset is current and clean");

        const auto* e = lib.find (key);
        check (e != nullptr && e->meta.type == "Chamber" && e->meta.styles == juce::StringArray { "Warm", "Wide" }
                   && e->meta.author == "Tester" && e->meta.notes == "made by room_tests",
               "tags and notes stored");

        // Edit tags + move to a new pack; favorite follows.
        lib.setFavorite (key, true);
        check (lib.createPack ("Eville", err), "create pack");
        meta.name = "Renamed Room";
        meta.type = "Hall";
        meta.styles = { "Dark" };
        juce::String newKey;
        check (lib.updateMeta (key, meta, "Eville", newKey, err), "edit + move: " + err);
        check (newKey == "Eville/Renamed Room" && lib.find (key) == nullptr, "moved to new pack");
        check (lib.find (newKey) != nullptr && lib.find (newKey)->favorite, "favorite follows a rename");
        check (lib.find (newKey)->meta.type == "Hall" && lib.find (newKey)->meta.styles == juce::StringArray { "Dark" }, "edited tags");
        check (lib.loadPreset (*lib.find (newKey)).getChildWithName ("PARAMS").getNumChildren() > 10, "sound kept after edit");

        // Import (name clash gets a new name) and export
        const auto exportDir = root.getSiblingFile ("rooms_export");
        exportDir.deleteRecursively();
        check (lib.exportPack ("Eville", exportDir), "export pack");
        check (exportDir.getChildFile ("Eville/Renamed Room.studio").existsAsFile(), "exported file exists");
        check (lib.exportPack ("Factory", exportDir) && exportDir.getChildFile ("Factory/Big Hall.studio").existsAsFile(),
               "factory pack can be exported");
        const auto imported = lib.importFiles ({ exportDir.getChildFile ("Eville/Renamed Room.studio") }, "Eville");
        check (imported == 1 && lib.countInPack ("Eville") == 2, "import with a name clash keeps both");
        exportDir.deleteRecursively();

        // Delete
        check (lib.remove (newKey) && lib.find (newKey) == nullptr, "delete");
        check (lib.countFavorites() == 0, "deleting removes the favorite");

        // Session state keeps the preset name + modified flag
        proc.applyPreset (lib.loadPreset (*lib.find ("Factory/Glass Bottle")), "Factory/Glass Bottle");
        setParam (proc, ParamIDs::mix, 0.9f);
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        RoomAudioProcessor proc2;
        proc2.setPresetRoot (root);
        proc2.setStateInformation (state.getData(), (int) state.getSize());
        check (proc2.getCurrentPresetName() == "Glass Bottle" && proc2.getCurrentPresetKey() == "Factory/Glass Bottle",
               "project reopens with the preset name");
        check (proc2.isPresetModified(), "project reopens remembering it was modified");
        check (std::abs (paramValue (proc2, ParamIDs::mix) - 0.9f) < 0.011f, "project keeps the tweaked knob");
    }

    //==========================================================================

    void testSpace()
    {
        std::cout << "Space\n";
        const double sr = 48000.0;
        const int n = 48000;
        juce::Random rng (3);

        // Stereo test signals: noise with some stereo width, and the same
        // plus a loud 50 Hz anti-phase hum (stereo bass) for the bass-mono check.
        juce::AudioBuffer<float> noise (2, n), withHum (2, n);
        for (int i = 0; i < n; ++i)
        {
            const auto common = rng.nextFloat() * 2.0f - 1.0f;
            const auto diff = (rng.nextFloat() * 2.0f - 1.0f) * 0.5f;
            const auto hum = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 50.0f * (float) i / (float) sr);
            noise.setSample (0, i, 0.4f * (common + diff));
            noise.setSample (1, i, 0.4f * (common - diff));
            withHum.setSample (0, i, noise.getSample (0, i) + hum);
            withHum.setSample (1, i, noise.getSample (1, i) - hum);
        }
        auto& in = noise;

        auto run = [&] (float w, const juce::AudioBuffer<float>& src)
        {
            const auto& inBuf = src;
            auto b = inBuf;
            room::StereoWidth sw2;
            sw2.prepare (sr);
            juce::AudioBuffer<float> warm (2, 4800);
            warm.clear();
            sw2.process (warm, w);   // smoothing reaches w
            sw2.reset();
            sw2.process (b, w);
            return b;
        };
        auto rmsOf = [] (const juce::AudioBuffer<float>& b, int ch, int start)
        {
            double s = 0;
            for (int i = start; i < b.getNumSamples(); ++i) s += (double) b.getSample (ch, i) * b.getSample (ch, i);
            return std::sqrt (s / (b.getNumSamples() - start));
        };
        auto sideRms = [] (const juce::AudioBuffer<float>& b, int start)
        {
            double s = 0;
            for (int i = start; i < b.getNumSamples(); ++i)
            {
                const auto d = 0.5 * (b.getSample (0, i) - b.getSample (1, i));
                s += d * d;
            }
            return std::sqrt (s / (b.getNumSamples() - start));
        };

        const auto mono = run (0.0f, withHum);
        float maxDiff = 0;
        for (int i = 2400; i < n; ++i)
            maxDiff = std::max (maxDiff, std::abs (mono.getSample (0, i) - mono.getSample (1, i)));
        check (maxDiff < 1e-4f, "Space 0% = mono (left == right)");

        const auto same = run (1.0f, withHum);
        float maxDev = 0;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                maxDev = std::max (maxDev, std::abs (same.getSample (ch, i) - withHum.getSample (ch, i)));
        check (maxDev < 1e-6f, "Space 100% = untouched");

        const auto wide = run (2.0f, noise);
        const auto wideHum = run (2.0f, withHum);
        check (sideRms (wide, 2400) > 1.4 * sideRms (in, 2400), "Space 200% widens (side boosted)");

        // The 50 Hz anti-phase hum should be (nearly) gone from the sides at 200 %.
        auto lowSide = [&] (const juce::AudioBuffer<float>& b)
        {
            // Correlate the side signal with the 50 Hz reference.
            double acc = 0;
            for (int i = 2400; i < n; ++i)
                acc += 0.5 * (b.getSample (0, i) - b.getSample (1, i))
                       * std::sin (2.0 * juce::MathConstants<double>::pi * 50.0 * i / sr);
            return std::abs (acc) / (n - 2400);
        };
        check (lowSide (wideHum) < 0.15 * lowSide (withHum), "Space 200% makes the low end mono");
        check (lowSide (run (1.5f, withHum)) < lowSide (withHum), "Space 150% already narrows the low end a bit");

        const auto levelIn = 0.5 * (rmsOf (in, 0, 2400) + rmsOf (in, 1, 2400));
        const auto levelWide = 0.5 * (rmsOf (wide, 0, 2400) + rmsOf (wide, 1, 2400));
        const auto db = 20.0 * std::log10 (levelWide / levelIn);
        std::cout << "    level change at 200%: " << juce::String (db, 2) << " dB\n";
        check (std::abs (db) < 1.5, "Space 200% keeps the level within 1.5 dB");
    }

    //==========================================================================
    // Advanced screen DSP + top bar (Bypass, Randomize)

    struct Rig
    {
        RoomAudioProcessor proc;
        static constexpr double sr = 48000.0;

        explicit Rig (const juce::File& root)
        {
            proc.setPresetRoot (root);
            proc.setPlayConfigDetails (2, 2, sr, 512);
            proc.prepareToPlay (sr, 512);
            auto& lib = proc.getLibrary();
            proc.applyPreset (lib.loadPreset (*lib.find ("Factory/Big Hall")), "Factory/Big Hall");
            // Hear only the reverb, untouched by the finishing section.
            for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> {
                     { ParamIDs::mix, 1.0f }, { ParamIDs::balanceAuto, 0.0f }, { ParamIDs::balance, 1.0f },
                     { ParamIDs::tape, 0.0f }, { ParamIDs::autoEq, 0.0f }, { ParamIDs::space, 1.0f },
                     { ParamIDs::tail, 6.0f } })
                setParam (proc, id, v);
            settle();
        }

        /** Let the background rebuild finish and the convolution swap in the new IR. */
        void settle()
        {
            juce::AudioBuffer<float> silence (2, 512);
            juce::MidiBuffer midi;
            for (int i = 0; i < 120; ++i)
            {
                silence.clear();
                proc.processBlock (silence, midi);
                juce::Thread::sleep (4);
            }
            proc.reset();
        }

        /** Runs input (mono, duplicated to stereo) plus `extra` seconds of silence.
            Returns the left channel with the plugin's latency removed. */
        std::vector<float> run (const std::vector<float>& input, double extraS,
                                std::function<void (int sample)> beforeBlock = {})
        {
            const auto latency = proc.getLatencySamples();
            const auto total = (int) input.size() + (int) (extraS * sr) + latency;
            std::vector<float> out;
            out.reserve ((size_t) total);
            juce::AudioBuffer<float> io (2, 512);
            juce::MidiBuffer midi;
            for (int pos = 0; pos < total; pos += 512)
            {
                const auto len = std::min (512, total - pos);
                io.setSize (2, len, false, false, true);
                for (int i = 0; i < len; ++i)
                {
                    const auto idx = (size_t) (pos + i);
                    const auto x = idx < input.size() ? input[idx] : 0.0f;
                    io.setSample (0, i, x);
                    io.setSample (1, i, x);
                }
                if (beforeBlock)
                    beforeBlock (pos);
                proc.processBlock (io, midi);
                for (int i = 0; i < len; ++i)
                    out.push_back (io.getSample (0, i));
            }
            out.erase (out.begin(), out.begin() + latency);
            return out;
        }
    };

    std::vector<float> impulse() { std::vector<float> x (480, 0.0f); x[0] = 0.5f; return x; }

    double rmsDb (const std::vector<float>& x, double fromS, double toS)
    {
        const auto a = (size_t) (fromS * Rig::sr), b = std::min (x.size(), (size_t) (toS * Rig::sr));
        double s = 1e-20;
        for (auto i = a; i < b; ++i) s += (double) x[i] * x[i];
        return 10.0 * std::log10 (s / (double) std::max<size_t> (1, b - a));
    }

    /** Time (s) for the energy-decay curve to fall from -5 to -25 dB, x3 = T60. */
    double t60Of (std::vector<float> x)
    {
        std::vector<double> edc (x.size());
        double acc = 0.0;
        for (size_t i = x.size(); i-- > 0;) { acc += (double) x[i] * x[i]; edc[i] = acc; }
        const auto e0 = edc[0];
        double t5 = -1, t25 = -1;
        for (size_t i = 0; i < edc.size(); ++i)
        {
            const auto db = 10.0 * std::log10 (edc[i] / e0 + 1e-30);
            if (t5 < 0 && db <= -5.0) t5 = (double) i / Rig::sr;
            if (t25 < 0 && db <= -25.0) { t25 = (double) i / Rig::sr; break; }
        }
        return (t25 - t5) * 3.0;
    }

    /** One frequency band of a signal (4th-order band-pass), for the damping test. */
    std::vector<float> bandOf (const std::vector<float>& x, double lo, double hi)
    {
        room::BandFilter f;
        f.design (lo, hi, Rig::sr);
        std::vector<float> y (x.size());
        for (size_t i = 0; i < x.size(); ++i)
            y[i] = (float) f.process (x[i]);
        return y;
    }

    void testAdvanced (const juce::File& root)
    {
        std::cout << "Advanced\n";

        // --- Pre-delay shifts the tail ---
        {
            Rig rig (root);
            auto onset = [&] (float ms)
            {
                setParam (rig.proc, ParamIDs::preDelay, ms);
                rig.settle();
                const auto y = rig.run (impulse(), 1.0);
                float peak = 0;
                for (auto v : y) peak = std::max (peak, std::abs (v));
                for (size_t i = 0; i < y.size(); ++i)
                    if (std::abs (y[i]) > peak * 0.05f)
                        return (double) i / Rig::sr * 1000.0;
                return -1.0;
            };
            const auto o0 = onset (0.0f), o100 = onset (100.0f);
            std::cout << "    onset " << juce::String (o0, 1) << " ms -> " << juce::String (o100, 1) << " ms with 100 ms pre-delay\n";
            check (std::abs ((o100 - o0) - 100.0) < 6.0, "pre-delay 100 ms moves the tail by ~100 ms");
        }

        // --- Size stretches the decay ---
        {
            Rig rig (root);
            auto t60At = [&] (float size)
            {
                setParam (rig.proc, ParamIDs::size, size);
                rig.settle();
                return t60Of (rig.run (impulse(), 5.0));
            };
            const auto t1 = t60At (1.0f), t2 = t60At (2.0f), th = t60At (0.5f);
            std::cout << "    T60 size 50/100/200%: " << juce::String (th, 2) << " / " << juce::String (t1, 2) << " / " << juce::String (t2, 2) << " s\n";
            check (t2 / t1 > 1.6 && t2 / t1 < 2.4, "Size 200% roughly doubles the decay");
            check (th / t1 > 0.35 && th / t1 < 0.65, "Size 50% roughly halves the decay");
        }

        // --- Damping darkens the tail (highs die faster than lows) ---
        {
            Rig rig (root);
            auto ratioAt = [&] (float d)
            {
                setParam (rig.proc, ParamIDs::damping, d);
                rig.settle();
                const auto y = rig.run (impulse(), 5.0);
                return t60Of (bandOf (y, 4000.0, 8000.0)) / t60Of (bandOf (y, 300.0, 600.0));
            };
            const auto r0 = ratioAt (0.0f), rDark = ratioAt (1.0f), rBright = ratioAt (-1.0f);
            std::cout << "    high/low decay ratio: bright " << juce::String (rBright, 2) << ", neutral " << juce::String (r0, 2)
                      << ", dark " << juce::String (rDark, 2) << "\n";
            check (rDark < r0 * 0.75, "Damping + makes the highs die faster");
            check (rBright > r0 * 1.2, "Damping - makes the highs last longer");
        }

        // --- Ducking: room dips while you play, comes back after ---
        {
            Rig rig (root);
            std::vector<float> tone ((size_t) (1.5 * Rig::sr));
            for (size_t i = 0; i < tone.size(); ++i)
                tone[i] = 0.4f * std::sin (2.0f * juce::MathConstants<float>::pi * 220.0f * (float) i / (float) Rig::sr);
            auto levels = [&] (float duck)
            {
                setParam (rig.proc, ParamIDs::duck, duck);
                setParam (rig.proc, ParamIDs::duckRelease, 150.0f);
                rig.settle();
                const auto y = rig.run (tone, 2.0);
                return std::pair { rmsDb (y, 0.8, 1.4), rmsDb (y, 2.0, 2.4) };   // while playing, well after
            };
            const auto [playOff, afterOff] = levels (0.0f);
            const auto [playOn, afterOn] = levels (1.0f);
            std::cout << "    while playing " << juce::String (playOff - playOn, 1) << " dB lower, afterwards "
                      << juce::String (afterOff - afterOn, 1) << " dB lower\n";
            check (playOff - playOn > 6.0, "Ducking 100% pulls the room down while you play");
            check (afterOff - afterOn < 2.0, "the room comes back after you stop");
        }

        // --- Freeze: the tail holds with no input ---
        {
            Rig rig (root);
            juce::Random r (5);
            std::vector<float> burst ((size_t) (0.4 * Rig::sr));
            for (auto& v : burst) v = (r.nextFloat() * 2.0f - 1.0f) * 0.3f;
            auto run = [&] (bool freeze)
            {
                setParam (rig.proc, ParamIDs::freeze, 0.0f);
                rig.settle();
                const auto freezeAt = (int) (0.6 * Rig::sr);
                return rig.run (burst, 5.0, [&] (int pos)
                {
                    if (freeze && pos >= freezeAt && pos < freezeAt + 512)
                        setParam (rig.proc, ParamIDs::freeze, 1.0f);
                });
            };
            const auto held = run (true), free = run (false);
            const auto heldLate = rmsDb (held, 4.0, 5.0), freeLate = rmsDb (free, 4.0, 5.0), early = rmsDb (held, 0.4, 0.6);
            std::cout << "    4-5 s after: frozen " << juce::String (heldLate - early, 1) << " dB, free "
                      << juce::String (freeLate - early, 1) << " dB (vs the tail at 0.5 s)\n";
            check (heldLate - early > -12.0, "Freeze holds the tail");
            check (freeLate - early < -40.0, "without Freeze the tail dies away");
            // No clicks. Freeze a steady 200 Hz tone: the held sound is then
            // (almost) a 200 Hz wave, so any seam shows up as a sample-to-sample
            // jump far steeper than a 200 Hz wave can make. Several tries,
            // because the loop picks random positions.
            std::vector<float> tone ((size_t) (1.5 * Rig::sr));
            for (size_t i = 0; i < tone.size(); ++i)
                tone[i] = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 200.0f * (float) i / (float) Rig::sr);
            int clicky = 0;
            float worstRatio = 0.0f;
            for (int attempt = 0; attempt < 6; ++attempt)
            {
                setParam (rig.proc, ParamIDs::freeze, 0.0f);
                rig.settle();
                const auto freezeAt = (int) (1.0 * Rig::sr);
                const auto y = rig.run (tone, 4.0, [&] (int pos)
                {
                    if (pos >= freezeAt && pos < freezeAt + 512)
                        setParam (rig.proc, ParamIDs::freeze, 1.0f);
                });
                float maxJump = 0, peak = 0;
                for (size_t i = (size_t) (1.8 * Rig::sr); i < (size_t) (5.0 * Rig::sr); ++i)
                {
                    maxJump = std::max (maxJump, std::abs (y[i] - y[i - 1]));
                    peak = std::max (peak, std::abs (y[i]));
                }
                const auto smoothest = 2.0f * juce::MathConstants<float>::pi * 200.0f / (float) Rig::sr * peak;
                worstRatio = std::max (worstRatio, maxJump / std::max (1e-9f, smoothest));
                if (maxJump > 3.0f * smoothest || peak < 1e-4f)
                    ++clicky;
            }
            std::cout << "    freeze loop: steepest step " << juce::String (worstRatio, 2) << "x a clean 200 Hz wave (6 tries)\n";
            check (clicky == 0, "Freeze loop has no clicks (6 tries)");
            setParam (rig.proc, ParamIDs::freeze, 0.0f);
        }

        // --- Bypass = input, delayed by the reported latency ---
        {
            Rig rig (root);
            setParam (rig.proc, ParamIDs::bypass, 1.0f);
            rig.settle();
            std::vector<float> x (24000);
            juce::Random r (9);
            for (auto& v : x) v = r.nextFloat() - 0.5f;
            const auto y = rig.run (x, 0.1);
            float worst = 0;
            for (size_t i = 0; i < x.size(); ++i) worst = std::max (worst, std::abs (y[i] - x[i]));
            check (worst < 1e-5f, "Bypass passes the input untouched (latency matched)");
            check (rig.proc.getBypassParameter() == rig.proc.apvts.getParameter (ParamIDs::bypass), "host bypass uses ROOM's Bypass");
        }

        // --- Randomize: moves the sound knobs, keeps room / dry-wet / output / bypass ---
        {
            Rig rig (root);
            setParam (rig.proc, ParamIDs::mix, 0.37f);
            setParam (rig.proc, ParamIDs::output, -4.0f);
            const auto room = analysisXml (rig.proc);
            const auto before = paramValue (rig.proc, ParamIDs::intensity) + paramValue (rig.proc, ParamIDs::tail)
                              + paramValue (rig.proc, ParamIDs::size);
            juce::Random r (11);
            rig.proc.randomizeKnobs (r);
            const auto after = paramValue (rig.proc, ParamIDs::intensity) + paramValue (rig.proc, ParamIDs::tail)
                             + paramValue (rig.proc, ParamIDs::size);
            check (std::abs (after - before) > 0.01f, "Randomize changes the knobs");
            check (std::abs (paramValue (rig.proc, ParamIDs::mix) - 0.37f) < 0.011f, "Randomize keeps Dry/Wet");
            check (std::abs (paramValue (rig.proc, ParamIDs::output) + 4.0f) < 0.05f, "Randomize keeps Output");
            check (paramValue (rig.proc, ParamIDs::bypass) < 0.5f && paramValue (rig.proc, ParamIDs::freeze) < 0.5f,
                   "Randomize never bypasses or freezes");
            check (analysisXml (rig.proc) == room, "Randomize keeps the room");
            for (int k = 0; k < 50; ++k)
            {
                rig.proc.randomizeKnobs (r);
                check (paramValue (rig.proc, ParamIDs::tail) <= 4.01f && paramValue (rig.proc, ParamIDs::intensity) <= 1.61f,
                       "Randomize stays in musical ranges");
            }
        }

        // --- Old presets (no Advanced values) load with Advanced at neutral ---
        {
            Rig rig (root);
            setParam (rig.proc, ParamIDs::size, 1.8f);
            setParam (rig.proc, ParamIDs::preDelay, 200.0f);
            auto& lib = rig.proc.getLibrary();
            rig.proc.applyPreset (lib.loadPreset (*lib.find ("Factory/Small Room")), "Factory/Small Room");
            check (std::abs (paramValue (rig.proc, ParamIDs::size) - 1.0f) < 0.011f
                       && paramValue (rig.proc, ParamIDs::preDelay) < 0.5f,
                   "presets without Advanced settings reset them to neutral");
        }
    }

    //==========================================================================
    // Record into ROOM

    void waitUntil (std::function<bool()> cond, int maxMs = 8000)
    {
        for (int t = 0; t < maxMs && ! cond(); t += 10)
            juce::Thread::sleep (10);
    }

    void testRecord (const juce::File& root, const juce::File& roomSource)
    {
        std::cout << "Record\n";
        Rig rig (root);
        auto& p = rig.proc;
        double sr = 0;
        const auto clap = readAudio (roomSource, sr);
        check (std::abs (sr - Rig::sr) < 1.0, "test source is 48 kHz");

        // Play the clap into the track while recording, then stop.
        p.startRecording();
        check (p.isRecording() && p.getStatus() == RoomAudioProcessor::Status::recording, "recording starts");
        check (p.getCurrentRoomKey().isEmpty(), "a recording is a new, unsaved room");
        juce::AudioBuffer<float> io (2, 512);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < clap.getNumSamples(); pos += 512)
        {
            io.clear();
            const auto len = std::min (512, clap.getNumSamples() - pos);
            for (int ch = 0; ch < 2; ++ch)
                io.copyFrom (ch, 0, clap, std::min (ch, clap.getNumChannels() - 1), pos, len);
            p.processBlock (io, midi);
        }
        p.stopRecording();
        io.clear();
        p.processBlock (io, midi);   // the audio thread sees the stop
        waitUntil ([&] { return p.getStatus() == RoomAudioProcessor::Status::ready; });

        check (! p.isRecording(), "recording stops");
        check (p.getStatus() == RoomAudioProcessor::Status::ready, "the recording becomes the room");
        const auto recorded = p.getAnalysis();
        const auto direct = room::analyzeRoom (clap, sr);
        std::cout << "    recorded " << juce::String (p.getRecordedSeconds(), 2) << " s: balance " << juce::String (recorded.balance, 3)
                  << " vs file " << juce::String (direct.balance, 3) << ", decay " << juce::String (recorded.medianT60, 3)
                  << " vs " << juce::String (direct.medianT60, 3) << " s\n";
        check (std::abs (recorded.balance - direct.balance) < 0.02f
                   && std::abs (recorded.medianT60 - direct.medianT60) < 0.05f * std::max (0.1f, direct.medianT60),
               "a recorded room matches the same sound loaded as a file");
        check (recorded.sourceName.startsWith ("Recorded room"), "recorded room is named");

        // Stops by itself at 30 s.
        p.startRecording();
        juce::Random r (3);
        for (int pos = 0; pos < (int) (31.0 * Rig::sr); pos += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                    io.setSample (ch, i, (r.nextFloat() - 0.5f) * 0.2f);
            p.processBlock (io, midi);
        }
        check (! p.isRecording(), "recording stops by itself at 30 s");
        check (std::abs (p.getRecordedSeconds() - RoomAudioProcessor::maxRecordSeconds) < 0.02, "30 s were kept");
        waitUntil ([&] { return p.getStatus() == RoomAudioProcessor::Status::ready; });
        check (p.getStatus() == RoomAudioProcessor::Status::ready, "a 30 s recording is analysed");

        // If the host isn't running audio, stopping still finishes cleanly.
        p.startRecording();
        p.stopRecording();
        waitUntil ([&] { return ! p.isRecording() && p.getStatus() != RoomAudioProcessor::Status::analyzing; }, 3000);
        check (! p.isRecording(), "stop works even with no audio running");
        check (p.getStatus() == RoomAudioProcessor::Status::failed, "an empty recording is reported, not loaded");
    }

    //==========================================================================
    // LFOs

    struct MockPlayHead : public juce::AudioPlayHead
    {
        double bpm = 120.0, ppq = 0.0;
        bool playing = true;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (bpm);
            p.setPpqPosition (ppq);
            p.setIsPlaying (playing);
            return p;
        }
    };

    /** Count the up-crossings of a signal's (20 ms) loudness around its mean. */
    int envelopeCycles (const std::vector<float>& y, double fromS, double toS)
    {
        const auto win = (size_t) (0.02 * Rig::sr);
        std::vector<double> env;
        for (auto i = (size_t) (fromS * Rig::sr); i + win < std::min (y.size(), (size_t) (toS * Rig::sr)); i += win)
        {
            double s = 0;
            for (size_t k = 0; k < win; ++k) s += (double) y[i + k] * y[i + k];
            env.push_back (std::sqrt (s / win));
        }
        double mean = 0;
        for (auto e : env) mean += e;
        mean /= std::max<size_t> (1, env.size());
        int ups = 0;
        for (size_t i = 1; i < env.size(); ++i)
            if (env[i - 1] < mean && env[i] >= mean) ++ups;
        return ups;
    }

    /** How far a signal's (20 ms) loudness swings, in dB. */
    double envelopeSwingDb (const std::vector<float>& y, double fromS, double toS)
    {
        const auto win = (size_t) (0.02 * Rig::sr);
        double lo = 1e9, hi = 0;
        for (auto i = (size_t) (fromS * Rig::sr); i + win < std::min (y.size(), (size_t) (toS * Rig::sr)); i += win)
        {
            double s = 0;
            for (size_t k = 0; k < win; ++k) s += (double) y[i + k] * y[i + k];
            const auto e = std::sqrt (s / win);
            lo = std::min (lo, e);
            hi = std::max (hi, e);
        }
        return 20.0 * std::log10 (hi / std::max (1e-9, lo));
    }

    void testLfo (const juce::File& root)
    {
        std::cout << "LFOs\n";
        Rig rig (root);
        auto& p = rig.proc;
        setParam (p, ParamIDs::mix, 0.0f);   // dry only, so the LFO on Output is all we hear
        rig.settle();

        std::vector<float> tone ((size_t) (3.0 * Rig::sr));
        for (size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.3f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / (float) Rig::sr);

        // Free-running 2 Hz sine on Output.
        setParam (p, ParamIDs::lfoRate (0), 2.0f);
        setParam (p, ParamIDs::lfoSync (0), 0.0f);
        setParam (p, ParamIDs::lfoShape (0), 0.0f);
        const auto outBefore = paramValue (p, ParamIDs::output);
        p.setCurrentPreset ({}, "lfo test");   // reference point: everything as it is now
        check (! p.isPresetModified(), "clean before assigning");
        p.modulation.assign (0, room::Modulation::targetIndex (ParamIDs::output), 0.3f);
        check (p.isPresetModified(), "assigning an LFO marks the preset modified");
        auto y = rig.run (tone, 0.0);
        const auto cycles = envelopeCycles (y, 0.5, 2.5);
        std::cout << "    2 Hz LFO on Out: " << cycles << " cycles in 2 s, swing " << juce::String (envelopeSwingDb (y, 0.5, 2.5), 1) << " dB\n";
        check (cycles >= 3 && cycles <= 5, "LFO moves the knob at its rate");
        check (std::abs (paramValue (p, ParamIDs::output) - outBefore) < 1e-4f, "the knob itself (and its automation) doesn't move");

        // Synced: 1/4 note at 120 bpm = 2 Hz, following the song position.
        MockPlayHead ph;
        p.setPlayHead (&ph);
        setParam (p, ParamIDs::lfoRate (0), 0.1f);       // would be very slow if not synced
        setParam (p, ParamIDs::lfoSync (0), 1.0f);
        setParam (p, ParamIDs::lfoDiv (0), 3.0f);        // "1/4"
        y = rig.run (tone, 0.0, [&] (int pos) { ph.ppq = pos / Rig::sr * ph.bpm / 60.0; });
        const auto syncCycles = envelopeCycles (y, 0.5, 2.5);
        std::cout << "    synced 1/4 at 120 bpm: " << syncCycles << " cycles in 2 s\n";
        check (syncCycles >= 3 && syncCycles <= 5, "synced LFO follows the tempo");
        p.setPlayHead (nullptr);

        // No assignments: identical to no LFO at all.
        p.modulation.clear();
        y = rig.run (tone, 0.0);
        check (envelopeSwingDb (y, 0.5, 2.5) < 0.5, "no assignment, no movement");

        // Save with the project and in presets.
        p.modulation.assign (0, room::Modulation::targetIndex (ParamIDs::tail), 0.5f);
        p.modulation.assign (1, room::Modulation::targetIndex (ParamIDs::mix), -0.2f);
        juce::MemoryBlock state;
        p.getStateInformation (state);
        RoomAudioProcessor p2;
        p2.setPresetRoot (root);
        p2.setStateInformation (state.getData(), (int) state.getSize());
        check (std::abs (p2.modulation.depthOf (0, room::Modulation::targetIndex (ParamIDs::tail)) - 0.5f) < 1e-4f
                   && std::abs (p2.modulation.depthOf (1, room::Modulation::targetIndex (ParamIDs::mix)) + 0.2f) < 1e-4f,
               "LFO assignments are saved with the project");

        PresetMeta meta;
        meta.name = "LFO test";
        meta.type = "FX";
        juce::String key, err;
        check (p.getLibrary().save (p.capturePreset (meta), "User", true, key, err), "save preset with LFOs");
        p.modulation.clear();
        p.applyPreset (p.getLibrary().loadPreset (*p.getLibrary().find (key)), key);
        check (p.modulation.isAssigned (0, room::Modulation::targetIndex (ParamIDs::tail))
                   && p.modulation.isAssigned (1, room::Modulation::targetIndex (ParamIDs::mix)),
               "LFO assignments come back with the preset");
        check (! p.isPresetModified(), "loaded preset with LFOs is clean");
        p.applyPreset (p.getLibrary().loadPreset (*p.getLibrary().find ("Factory/Big Hall")), "Factory/Big Hall");
        check (! p.modulation.anyAssigned(), "a preset without LFOs clears them");
        p.getLibrary().remove (key);
    }

    //==========================================================================
    // ROOMS (room only) and STUDIOS (full presets)

    void testRoomsAndStudios (const juce::File& root, const juce::File& roomSource)
    {
        std::cout << "ROOMS + STUDIOS\n";
        RoomAudioProcessor proc;
        proc.setPresetRoot (root);
        proc.setPlayConfigDetails (2, 2, 48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        auto& rooms = proc.getRoomLibrary();
        auto& studios = proc.getLibrary();

        check (rooms.getKind() == Kind::room && studios.getKind() == Kind::studio, "two libraries");
        check (rooms.countInPack (PresetLibrary::factoryPack) >= 6, "factory ROOMS are present");
        bool roomsKnown = true;
        for (auto& e : studios.getEntries())
            if (e.factory)
                roomsKnown = roomsKnown && rooms.find ("Factory/" + e.meta.roomName) != nullptr && e.meta.madeFor.size() > 0;
        check (roomsKnown, "every factory STUDIO names a factory ROOM and has Made for tags");
        check (rooms.getUnreadableCount() == 0, "factory ROOMS parse");

        // Auto tags
        bool allTagged = true;
        for (auto& e : rooms.getEntries())
        {
            allTagged = allTagged && allDecayTags().contains (e.tags.decay) && allToneTags().contains (e.tags.tone)
                        && e.tags.source == "Factory";
            std::cout << "    " << e.meta.name << ": " << e.tags.decay << " " << juce::String (e.tags.medianT60, 2)
                      << " s, " << e.tags.tone << " (" << juce::String (e.tags.toneTiltDb, 1) << " dB)\n";
        }
        check (allTagged, "every factory ROOM has decay / tone / source tags");
        if (auto* hall = rooms.find ("Factory/Clap Hall"))
            check (hall->tags.decay == "Long" || hall->tags.decay == "Medium", "Clap Hall is not tagged Short");
        // Tone tag follows the room's EQ: tilt a real room's spectrum and the tag follows.
        if (auto* hall = rooms.find ("Factory/Clap Hall"))
        {
            const auto a = rooms.loadPreset (*hall).getChildWithName ("ROOM_ANALYSIS");
            auto tilted = [&a] (float dbPerOct)
            {
                auto r = room::RoomAnalysis::fromValueTree (a);
                for (size_t i = 0; i < r.toneCurveDb.size(); ++i)
                    r.toneCurveDb[i] += dbPerOct * std::log2 (r.toneFreqsHz[i] / 1000.0f);
                return autoTags (r.toValueTree(), "File").tone;
            };
            check (tilted (-8.0f) == "Dark" && tilted (6.0f) == "Bright", "tone tag: darker room = Dark, brighter = Bright");
            check (hall->tags.source == "Factory", "factory ROOMS are tagged Factory");
        }
        PresetLibrary::Query q;
        q.autoTags = { "Short" };
        for (auto i : rooms.query (q))
            check (rooms.getEntries()[(size_t) i].tags.decay == "Short", "decay filter");
        q.autoTags = { "Short", "Long", "Dark" };
        for (auto i : rooms.query (q))
        {
            const auto& t = rooms.getEntries()[(size_t) i].tags;
            check ((t.decay == "Short" || t.decay == "Long") && t.tone == "Dark", "auto tag groups: any within, all across");
        }
        q = {};
        q.sortBy = PresetLibrary::SortColumn::decay;
        const auto byDecay = rooms.query (q);
        bool decayOrdered = true;
        for (size_t i = 1; i < byDecay.size(); ++i)
            decayOrdered = decayOrdered && rooms.getEntries()[(size_t) byDecay[i - 1]].tags.medianT60
                                               <= rooms.getEntries()[(size_t) byDecay[i]].tags.medianT60;
        check (decayOrdered, "sort ROOMS by decay");

        // Start from a STUDIO, then swap in a dropped room and save it as a ROOM.
        proc.applyPreset (studios.loadPreset (*studios.find ("Factory/Big Hall")), "Factory/Big Hall");
        setParam (proc, ParamIDs::tape, 0.66f);
        setParam (proc, ParamIDs::intensity, 1.3f);
        proc.modulation.assign (0, room::Modulation::targetIndex (ParamIDs::space), 0.4f);
        proc.loadRoomFile (roomSource);
        waitForAnalysis (proc);
        const auto droppedRoom = analysisXml (proc);
        check (proc.getAnalysis().origin == "File", "a dropped room's source is File");

        PresetMeta meta;
        meta.name = "Test Clap Room";
        meta.type = "Small Space";
        meta.styles = { "Tight", "Dirty" };
        juce::String key, err;
        check (rooms.save (proc.captureRoom (meta), "User", false, key, err), "save ROOM: " + err);
        check (root.getChildFile ("ROOMS/User/Test Clap Room.room").existsAsFile(), "ROOM file written to ROOMS");
        const auto roomTree = rooms.loadPreset (*rooms.find (key));
        check (roomTree.hasType ("ROOM") && ! roomTree.getChildWithName ("PARAMS").isValid()
                   && ! roomTree.getChildWithName ("MODULATION").isValid(),
               "a ROOM holds only the room (no knobs, no LFOs)");
        const auto* re = rooms.find (key);
        check (re != nullptr && re->meta.type == "Small Space" && re->meta.styles.contains ("Dirty")
                   && re->tags.source == "File" && re->tags.decay.isNotEmpty(),
               "ROOM tags + auto tags stored");

        // Load a different room, then the saved ROOM: only the room changes.
        proc.applyRoom (rooms.loadPreset (*rooms.find ("Factory/Glass Bottle")), "Factory/Glass Bottle");
        check (analysisXml (proc) != droppedRoom, "another ROOM swaps the room");
        const auto knobsBefore = paramValue (proc, ParamIDs::tape) + paramValue (proc, ParamIDs::intensity);
        proc.applyRoom (rooms.loadPreset (*rooms.find (key)), key);
        check (analysisXml (proc) == droppedRoom, "loading a ROOM restores that room exactly");
        check (std::abs (paramValue (proc, ParamIDs::tape) + paramValue (proc, ParamIDs::intensity) - knobsBefore) < 1e-4f,
               "loading a ROOM leaves the knobs alone");
        check (proc.modulation.isAssigned (0, room::Modulation::targetIndex (ParamIDs::space)), "loading a ROOM keeps the LFOs");
        check (proc.getAnalysis().sourceName == "Test Clap Room" && proc.getCurrentRoomKey() == key, "room takes the ROOM's name");
        check (proc.getCurrentPresetKey() == "Factory/Big Hall" && proc.isPresetModified(),
               "the STUDIO stays loaded and shows as modified");

        // Save the whole thing as a STUDIO: it remembers the room's name and Made for tags.
        PresetMeta sm;
        sm.name = "Clap Vox";
        sm.madeFor = { "Vocals", "Drums" };
        sm.styles = { "Warm" };
        check (studios.save (proc.capturePreset (sm), "User", false, key, err), "save STUDIO: " + err);
        check (root.getChildFile ("STUDIOS/User/Clap Vox.studio").existsAsFile(), "STUDIO file written to STUDIOS");
        const auto* se = studios.find (key);
        check (se != nullptr && se->meta.roomName == "Test Clap Room" && se->meta.madeFor == juce::StringArray { "Vocals", "Drums" },
               "STUDIO stores its room's name and Made for");
        proc.setCurrentPreset (key, sm.name);
        check (! proc.isPresetModified(), "saved STUDIO is clean");
        q = {};
        q.madeFor = { "Vocals" };
        bool foundVox = false;
        for (auto i : studios.query (q))
            foundVox = foundVox || studios.getEntries()[(size_t) i].key == key;
        check (foundVox, "Made for filter");
        proc.applyPreset (studios.loadPreset (*studios.find ("Factory/Glass Bottle")), "Factory/Glass Bottle");
        proc.applyPreset (studios.loadPreset (*se), key);
        check (analysisXml (proc) == droppedRoom && proc.getAnalysis().sourceName == "Test Clap Room",
               "loading the STUDIO brings back its room and room name");

        // Project state remembers the ROOM key and the randomize choices.
        proc.setCurrentRoomKey ("User/Test Clap Room");
        proc.randomizeMask = RoomAudioProcessor::randomLfos | RoomAudioProcessor::randomEq;
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        RoomAudioProcessor proc2;
        proc2.setPresetRoot (root);
        proc2.setStateInformation (state.getData(), (int) state.getSize());
        check (proc2.getCurrentRoomKey() == "User/Test Clap Room", "project remembers the ROOM");
        check (proc2.randomizeMask == (RoomAudioProcessor::randomLfos | RoomAudioProcessor::randomEq),
               "project remembers what Randomize touches");

        rooms.remove ("User/Test Clap Room");
        studios.remove (key);

        // Migration: an old ROOMS folder full of full presets.
        const auto oldBase = root.getSiblingFile ("legacy");
        oldBase.deleteRecursively();
        auto legacyPreset = studios.loadPreset (*studios.find ("Factory/Dark Hall"));
        auto lm = readMeta (legacyPreset);
        lm.name = "My Old Hall";
        lm.roomName = {};
        writeMeta (legacyPreset, lm);
        check (save (legacyPreset, oldBase.getChildFile ("ROOMS/User/My Old Hall.room")), "write an old-style preset");
        auto noRoom = legacyPreset.createCopy();
        noRoom.removeChild (noRoom.getChildWithName ("ROOM_ANALYSIS"), nullptr);
        lm.name = "Knobs Only";
        writeMeta (noRoom, lm);
        save (noRoom, oldBase.getChildFile ("ROOMS/Eville/Knobs Only.room"));
        oldBase.getChildFile ("ROOMS/favorites.xml").replaceWithText (
            "<ROOMS_FAVORITES><FAVORITE key=\"User/My Old Hall\"/></ROOMS_FAVORITES>");

        RoomAudioProcessor old;
        old.setPresetRoot (oldBase);
        auto& oldStudios = old.getLibrary();
        auto& oldRooms = old.getRoomLibrary();
        check (oldStudios.find ("User/My Old Hall") != nullptr && oldStudios.find ("Eville/Knobs Only") != nullptr,
               "old presets became STUDIOS");
        check (oldStudios.find ("User/My Old Hall") != nullptr && oldStudios.find ("User/My Old Hall")->favorite,
               "favorites carried over to STUDIOS");
        check (oldRooms.find ("User/My Old Hall") != nullptr && oldRooms.find ("User/My Old Hall")->favorite
                   && oldRooms.find ("User/My Old Hall")->meta.type == readMeta (legacyPreset).type,
               "their rooms became ROOMS (same name, type, favorite)");
        check (oldRooms.find ("Eville/Knobs Only") == nullptr && oldRooms.getUnreadableCount() == 0,
               "a preset without a room only becomes a STUDIO");
        const auto studioCount = oldStudios.getEntries().size();
        check (PresetLibrary::migrateLegacy (oldBase.getChildFile ("ROOMS"), oldBase.getChildFile ("STUDIOS")) == 0,
               "migration runs once (nothing left to convert)");
        old.setPresetRoot (oldBase);
        check (old.getLibrary().getEntries().size() == studioCount, "no duplicates after a second run");
        oldBase.deleteRecursively();
    }

    //==========================================================================
    // Modulation UI: default tab, depth dots, LFO chips, Clear, Randomize groups

    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> pos, juce::Point<float> downPos,
                              juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier, int clicks = 1)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods,
                                 juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f,
                                 &c, &c, now, downPos, now, clicks, pos != downPos);
    }

    template <typename T>
    T* findChild (juce::Component& root)
    {
        for (auto* c : root.getChildren())
        {
            if (auto* t = dynamic_cast<T*> (c))
                return t;
            if (auto* t = findChild<T> (*c))
                return t;
        }
        return nullptr;
    }

    /** A test phrase: kick / snare at 120 bpm and a saw melody, 4 s, stereo. */
    juce::AudioBuffer<float> testPhrase (double sr)
    {
        const int n = (int) (4.0 * sr);
        juce::AudioBuffer<float> b (2, n);
        b.clear();
        juce::Random rng (3);
        const double beat = 0.5;
        for (int k = 0; k < 8; ++k)
        {
            const int at = (int) (k * beat * sr);
            for (int i = 0; i < (int) (0.3 * sr) && at + i < n; ++i)
            {
                const auto t = i / sr;
                float v = 0.0f;
                if (k % 2 == 0) v += (float) (std::sin (2.0 * juce::MathConstants<double>::pi * (50.0 + 90.0 * std::exp (-t * 30.0)) * t) * std::exp (-t * 9.0));
                else            v += (rng.nextFloat() * 2.0f - 1.0f) * (float) std::exp (-t * 22.0) * 0.6f;
                for (int c = 0; c < 2; ++c) b.addSample (c, at + i, v * 0.5f);
            }
        }
        const double notes[] = { 220.0, 261.63, 329.63, 293.66, 220.0, 196.0, 246.94, 220.0 };
        for (int k = 0; k < 8; ++k)
        {
            const int at = (int) (k * beat * sr);
            for (int i = 0; i < (int) (beat * sr) && at + i < n; ++i)
            {
                const auto t = i / sr;
                double saw = 0.0;
                for (int h = 1; h <= 10; ++h) saw += std::sin (2.0 * juce::MathConstants<double>::pi * notes[k] * h * t) / h;
                const auto env = std::min (1.0, t / 0.01) * std::exp (-t * 2.5);
                for (int c = 0; c < 2; ++c) b.addSample (c, at + i, (float) (saw * env * 0.18));
            }
        }
        return b;
    }

    struct PresetLevels { float peakDb, rmsDiffDb, tailDb; bool finite; };

    /** Render the test phrase (plus 9 s of silence) through a factory STUDIO. */
    PresetLevels measurePreset (const juce::String& key, juce::AudioBuffer<float>* keep = nullptr)
    {
        const double sr = 48000.0;
        const int block = 512;
        RoomAudioProcessor p;
        p.setPresetRoot (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("room_level_presets"));
        p.setPlayConfigDetails (2, 2, sr, block);
        p.prepareToPlay (sr, block);
        auto& lib = p.getLibrary();
        if (auto* e = lib.find (key))
            p.applyPreset (lib.loadPreset (*e), e->key);
        waitForAnalysis (p);
        juce::Thread::sleep (500);

        const auto dry = testPhrase (sr);
        const int total = dry.getNumSamples() + (int) (9.0 * sr);
        juce::AudioBuffer<float> out (2, total);
        out.clear();
        for (int c = 0; c < 2; ++c) out.copyFrom (c, 0, dry, c, 0, dry.getNumSamples());
        juce::MidiBuffer midi;
        for (int pos = 0; pos < total; pos += block)
        {
            const auto m = std::min (block, total - pos);
            juce::AudioBuffer<float> part (out.getArrayOfWritePointers(), 2, pos, m);
            p.processBlock (part, midi);
        }
        const auto lat = p.getLatencySamples();
        PresetLevels r {};
        r.finite = true;
        float peak = 0.0f;
        double eWet = 0.0, eDry = 0.0, eTail = 0.0, eBody = 0.0;
        const int dn = dry.getNumSamples();
        for (int c = 0; c < 2; ++c)
        {
            for (int i = 0; i < total; ++i)
            {
                const auto v = out.getSample (c, i);
                r.finite = r.finite && std::isfinite (v);
                peak = std::max (peak, std::abs (v));
                if (i >= lat && i < dn + lat) eWet += (double) v * v;
                if (i >= lat && i < lat + (int) sr) eBody += (double) v * v;
                if (i >= dn + lat + (int) (8.0 * sr)) eTail += (double) v * v;
            }
            for (int i = 0; i < dn; ++i) eDry += (double) dry.getSample (c, i) * dry.getSample (c, i);
        }
        r.peakDb = juce::Decibels::gainToDecibels (peak, -120.0f);
        r.rmsDiffDb = (float) (10.0 * std::log10 ((eWet + 1e-12) / (eDry + 1e-12)));
        const auto tailLen = (double) (total - (dn + lat + (int) (8.0 * sr)));
        r.tailDb = (float) (10.0 * std::log10 ((eTail / std::max (1.0, tailLen) + 1e-20) / (eBody / sr + 1e-20)));
        if (keep != nullptr) *keep = out;
        return r;
    }

    void testFactoryPresets (const juce::File& root)
    {
        std::cout << "Factory library\n";
        RoomAudioProcessor p;
        p.setPresetRoot (root);
        auto& lib = p.getLibrary();
        int rooms = 0, studios = 0;
        bool allLevels = true, allFinite = true, allPeaks = true, allTails = true;
        juce::StringArray bad;
        for (auto& e : lib.getEntries())
            if (e.factory) ++studios;
        for (auto& e : p.getRoomLibrary().getEntries())
            if (e.factory) ++rooms;
        check (rooms >= 45 && studios >= 40, "the factory library has 45+ ROOMS and 40+ STUDIOS");
        for (auto& e : lib.getEntries())
        {
            if (! e.factory) continue;
            const auto r = measurePreset (e.key);
            allFinite = allFinite && r.finite;
            if (r.peakDb > -0.1f) { allPeaks = false; bad.add (e.meta.name + " peak " + juce::String (r.peakDb, 1)); }
            if (std::abs (r.rmsDiffDb) > 5.0f) { allLevels = false; bad.add (e.meta.name + " level " + juce::String (r.rmsDiffDb, 1)); }
            if (r.tailDb > -60.0f) { allTails = false; bad.add (e.meta.name + " tail " + juce::String (r.tailDb, 1)); }
        }
        check (allFinite, "every STUDIO's output stays finite");
        check (allPeaks, "no STUDIO clips the test phrase");
        check (allLevels, "every STUDIO is within 5 dB of the dry level (output knob untouched)");
        check (allTails, "every STUDIO's tail dies away (60 dB down 8 s after the music stops)");
        for (auto& b : bad) std::cout << "    " << b << "\n";
    }

    /** --render-presets DIR: every factory STUDIO on the test phrase, one WAV each (for listening). */
    int renderPresets (const juce::File& dir)
    {
        dir.createDirectory();
        RoomAudioProcessor p;
        p.setPresetRoot (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("room_level_presets"));
        juce::WavAudioFormat wav;
        {
            auto dry = testPhrase (48000.0);
            auto f = dir.getChildFile ("_dry.wav");
            f.deleteFile();
            std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
            if (auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (48000).withNumChannels (2).withBitsPerSample (16)))
                w->writeFromAudioSampleBuffer (dry, 0, dry.getNumSamples());
        }
        for (auto& e : p.getLibrary().getEntries())
            if (e.factory)
            {
                juce::AudioBuffer<float> out;
                measurePreset (e.key, &out);
                const int len = std::min (out.getNumSamples(), (int) (7.5 * 48000));   // the phrase and 3.5 s of tail
                auto f = dir.getChildFile (e.meta.name + ".wav");
                f.deleteFile();
                std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
                if (auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (48000).withNumChannels (2).withBitsPerSample (16)))
                    w->writeFromAudioSampleBuffer (out, 0, len);
                std::cout << e.meta.name << "\n";
            }
        return 0;
    }

    /** --levels: the numbers behind testFactoryPresets, one line per STUDIO. */
    int levels()
    {
        RoomAudioProcessor p;
        p.setPresetRoot (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("room_level_presets"));
        std::printf ("%-20s %8s %10s %8s\n", "studio", "peak dB", "level dB", "tail dB");
        for (auto& e : p.getLibrary().getEntries())
            if (e.factory)
            {
                const auto r = measurePreset (e.key);
                std::printf ("%-20s %8.1f %10.1f %8.1f%s\n", e.meta.name.toRawUTF8(), r.peakDb, r.rmsDiffDb, r.tailDb, r.finite ? "" : "  NaN!");
            }
        return 0;
    }

    void testFirstRun()
    {
        std::cout << "First run\n";
        RoomAudioProcessor::startWithDefaultStudio = true;
        {
            RoomAudioProcessor p;
            check (p.getAnalysis().valid && p.getCurrentPresetName() == "Init Room" && ! p.isPresetModified(),
                   "a new ROOM starts with Init Room loaded");
            // A project saved with an empty bay stays empty.
            RoomAudioProcessor emptyOne;
            RoomAudioProcessor::startWithDefaultStudio = false;
            RoomAudioProcessor blank;
            juce::MemoryBlock state;
            blank.getStateInformation (state);
            emptyOne.setStateInformation (state.getData(), (int) state.getSize());
            check (! emptyOne.getAnalysis().valid, "loading a project with no room clears the default room");
        }
        RoomAudioProcessor::startWithDefaultStudio = false;
        {
            RoomAudioProcessor p;
            RoomAudioProcessorEditor ed (p);
            ed.showTour (0);
            auto* tour = findChild<TourOverlay> (ed);
            check (tour != nullptr && tour->isVisible() && tour->getStep() == 0, "the tour opens on its first step");
            if (tour != nullptr)
            {
                tour->showStep (1); tour->showStep (2);
                check (tour->isVisible() && tour->getStep() == 2, "the tour steps through");
                tour->showStep (3);
                check (! tour->isVisible(), "done closes the tour");
            }
        }
    }

    void testSuppressorIdle()
    {
        std::cout << "Auto EQ idle path\n";
        // At 0 (nothing to release) the auto eq skips its FFTs. It must still
        // be an exact delay of its reported latency, before and after use.
        room::ResonanceSuppressor sup;
        sup.prepare (48000.0, 2);
        const auto lat = sup.getLatency();
        juce::Random rng (5);
        const int n = 48000;
        juce::AudioBuffer<float> in (2, n);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) in.setSample (c, i, rng.nextFloat() * 2.0f - 1.0f);
        auto run = [&] (float amount, int from, int to, juce::AudioBuffer<float>& out)
        {
            for (int pos = from; pos < to; pos += 256)
            {
                const auto m = std::min (256, to - pos);
                juce::AudioBuffer<float> part (2, m);
                for (int c = 0; c < 2; ++c) part.copyFrom (c, 0, in, c, pos, m);
                sup.process (part, amount);
                for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, part, c, 0, m);
            }
        };
        juce::AudioBuffer<float> out (2, n);
        run (0.0f, 0, n, out);
        float worst = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = lat + 2048; i < n; ++i)
                worst = std::max (worst, std::abs (out.getSample (c, i) - in.getSample (c, i - lat)));
        check (worst < 1.0e-4f, "auto eq at 0 is an exact delay (no ffts)");
    }

    void testConvolver()
    {
        std::cout << "Convolver\n";
        using room::PartitionedConvolver;
        juce::Random rng (11);
        auto makeIr = [&] (int len, float decay)
        {
            juce::AudioBuffer<float> ir (2, len);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < len; ++i)
                    ir.setSample (c, i, (rng.nextFloat() * 2.0f - 1.0f) * std::exp (-(float) i / decay));
            return ir;
        };
        auto direct = [] (const std::vector<float>& x, const juce::AudioBuffer<float>& ir, int ch, int n)
        {
            // y[n] = sum_k x[n-k] h[k]
            double y = 0.0;
            const auto* h = ir.getReadPointer (ch);
            for (int k = 0; k < ir.getNumSamples() && k <= n; ++k)
                y += (double) x[(size_t) (n - k)] * h[k];
            return (float) y;
        };

        const int len = 40000;   // reaches all three stages
        auto irA = makeIr (len, 9000.0f), irB = makeIr (len, 5000.0f);
        PartitionedConvolver conv;
        conv.prepare (48000.0, 2, 2.0);
        conv.setKernel (PartitionedConvolver::makeKernel (irA, 2));

        const int total = 90000;
        std::vector<float> in[2];
        for (auto& v : in) { v.resize (total); for (auto& s : v) s = rng.nextFloat() * 2.0f - 1.0f; }
        std::vector<float> out[2] = { in[0], in[1] };
        int pos = 0;
        while (pos < total)
        {
            const auto n = std::min (total - pos, 1 + rng.nextInt (700));
            float* chans[2] = { out[0].data() + pos, out[1].data() + pos };
            conv.process (chans, 2, n);
            pos += n;
        }
        float worst = 0.0f, peak = 0.0f;
        for (int t : { 200, 1000, 2047, 2048, 5000, 16383, 16384, 30000, 41000, 60000, 89999 })
            for (int c = 0; c < 2; ++c)
            {
                const auto ref = direct (in[c], irA, c, t);
                worst = std::max (worst, std::abs (ref - out[c][(size_t) t]));
                peak = std::max (peak, std::abs (ref));
            }
        check (worst < 1.0e-3f * std::max (1.0f, peak), "partitioned convolution matches direct convolution (all stages, random block sizes)");

        // Switch rooms; once the crossfade is over the output is the new room
        // convolved with everything played before (the history is kept).
        conv.setKernel (PartitionedConvolver::makeKernel (irB, 2));
        const int more = 60000;
        for (auto& v : in) { v.resize ((size_t) (total + more)); for (int i = total; i < total + more; ++i) v[(size_t) i] = rng.nextFloat() * 2.0f - 1.0f; }
        for (int c = 0; c < 2; ++c) { out[c].resize ((size_t) (total + more)); std::copy (in[c].begin() + total, in[c].end(), out[c].begin() + total); }
        pos = total;
        while (pos < total + more)
        {
            const auto n = std::min (total + more - pos, 1 + rng.nextInt (512));
            float* chans[2] = { out[0].data() + pos, out[1].data() + pos };
            conv.process (chans, 2, n);
            pos += n;
        }
        worst = 0.0f;
        for (int t : { total + 40000, total + 50000, total + more - 1 })
            for (int c = 0; c < 2; ++c)
                worst = std::max (worst, std::abs (direct (in[c], irB, c, t) - out[c][(size_t) t]));
        check (worst < 1.0e-3f * std::max (1.0f, peak), "after a room switch, the new room keeps the full history");
        bool finite = true;
        for (auto& v : out) for (auto s : v) finite = finite && std::isfinite (s);
        check (finite, "convolution output stays finite through a switch");
    }

    void testModulationUi (const juce::File& root)
    {
        std::cout << "Modulation UI + Randomize\n";
        using M = room::Modulation;
        RoomAudioProcessor p;
        p.setPresetRoot (root);

        {
            RoomAudioProcessorEditor ed (p);
            auto* adv = findChild<AdvancedPanel> (ed);
            check (adv != nullptr && ! adv->isVisible() && ! ed.isAdvancedOpen(), "advanced starts closed");
            const auto closedWidth = ed.getWidth();
            IconButton* advKey = nullptr;
            std::function<void (juce::Component&)> findKey = [&] (juce::Component& c)
            {
                for (auto* child : c.getChildren())
                {
                    if (auto* ib = dynamic_cast<IconButton*> (child))
                        if (ib->getTooltip().startsWith ("advanced")) advKey = ib;
                    findKey (*child);
                }
            };
            findKey (ed);
            if (advKey != nullptr && advKey->onClick)
                advKey->onClick();
            auto* card = findChild<LfoCard> (ed);
            check (advKey != nullptr && adv != nullptr && adv->isVisible() && ed.getWidth() > closedWidth
                       && card != nullptr && card->isVisible() && ! adv->isShowingLfos(),
                   "the advanced key opens advanced, the window grows, the two lfo cards are there");
            if (card != nullptr && adv != nullptr)
            {
                card->mouseUp (mouseAt (*card, { 20.0f, 20.0f }, { 20.0f, 20.0f }));
                check (adv->isShowingLfos(), "clicking an lfo card opens its controls in place");
                adv->showModulationTab (false);
            }
            if (advKey != nullptr && advKey->onClick)
                advKey->onClick();
            check (! ed.isAdvancedOpen() && ed.getWidth() == closedWidth, "and closes it again");
            p.advancedOpen = false;

            // Main menu: tips on / off, new preset (init keeps the room)
            check (ed.areTipsEnabled(), "tips are on by default");
            p.tipsEnabled = false;
        }
        {
            RoomAudioProcessorEditor ed (p);
            check (! ed.areTipsEnabled(), "tips stay off once switched off");
            p.tipsEnabled = true;
            auto* intensityParam = p.apvts.getParameter (ParamIDs::intensity);
            intensityParam->setValueNotifyingHost (0.9f);
            p.modulation.assign (0, room::Modulation::targetIndex (ParamIDs::tail), 0.3f);
            const auto roomBefore = p.getAnalysis().toValueTree().toXmlString();
            ed.newPreset();
            check (std::abs (intensityParam->getValue() - intensityParam->getDefaultValue()) < 1e-6f
                       && ! p.modulation.isAssigned (0, room::Modulation::targetIndex (ParamIDs::tail))
                       && p.getCurrentPresetName().isEmpty() && ! p.isPresetModified()
                       && p.getAnalysis().toValueTree().toXmlString() == roomBefore,
                   "new preset: knobs and lfos back to init, the room stays");
            check (UpdateChecker::isNewer ("0.2.0", "0.1.0") && ! UpdateChecker::isNewer ("0.1.0", "0.1.0")
                       && UpdateChecker::isNewer ("0.10.0", "0.9.1") && ! UpdateChecker::isNewer ("", "0.1.0"),
                   "update versions compare");
        }

        // A knob's depth dot: drag up for more, down for less, double-click removes.
        ModSlider knob;
        knob.setBounds (0, 0, 190, 130);
        const auto t = M::targetIndex (ParamIDs::tail);
        knob.setModTarget (&p.modulation, t);
        check (knob.depthDotBounds (0).isEmpty(), "no dot before an LFO is assigned");
        p.modulation.assign (0, t, 0.25f);
        p.modulation.assign (1, t, 0.5f);
        const auto dot = knob.depthDotBounds (0).getCentre();
        const auto dot2 = knob.depthDotBounds (1).getCentre();
        check (! knob.depthDotBounds (0).isEmpty() && dot2.y > dot.y, "a dot per assigned LFO, stacked");
        const auto knobValue = knob.getValue();
        knob.mouseDown (mouseAt (knob, dot, dot));
        knob.mouseDrag (mouseAt (knob, dot.translated (0.0f, -30.0f), dot));
        knob.mouseUp (mouseAt (knob, dot.translated (0.0f, -30.0f), dot));
        check (std::abs (p.modulation.depthOf (0, t) - 0.45f) < 0.01f, "dragging the dot up 30 px adds 20%");
        check (std::abs (knob.getValue() - knobValue) < 1e-9, "dragging a dot doesn't move the knob");
        knob.mouseDown (mouseAt (knob, dot, dot, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
        knob.mouseDrag (mouseAt (knob, dot.translated (0.0f, 60.0f), dot, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
        knob.mouseUp (mouseAt (knob, dot.translated (0.0f, 60.0f), dot));
        check (std::abs (p.modulation.depthOf (0, t) - 0.35f) < 0.01f, "Shift-drag is finer (60 px = 10%)");
        knob.mouseDown (mouseAt (knob, dot, dot));
        knob.mouseDrag (mouseAt (knob, dot.translated (0.0f, 400.0f), dot));
        knob.mouseUp (mouseAt (knob, dot.translated (0.0f, 400.0f), dot));
        check (std::abs (p.modulation.depthOf (0, t) + 1.0f) < 1e-4f, "depth stops at -100%");
        knob.mouseDoubleClick (mouseAt (knob, dot2, dot2, juce::ModifierKeys::leftButtonModifier, 2));
        check (! p.modulation.isAssigned (1, t) && p.modulation.isAssigned (0, t), "double-click a dot removes that LFO");

        ModSlider tiny;
        tiny.setBounds (0, 0, 38, 38);
        tiny.setModTarget (&p.modulation, t);
        check (tiny.depthDotBounds (0).isEmpty(), "tiny knobs (top bar) skip the dot");

        // LFO panel chips
        {
            LfoModule module (p, 0);
            module.setBounds (0, 0, 420, 150);
            p.modulation.assign (0, M::targetIndex (ParamIDs::mix), 0.3f);
            const auto chips = module.targetChips();
            check (chips.size() == 2, "a chip per knob the LFO moves");
            juce::Point<float> c;
            for (auto& ch : chips)
                if (ch.target == M::targetIndex (ParamIDs::mix))
                    c = ch.bounds.getCentre().toFloat();
            module.mouseDown (mouseAt (module, c, c));
            module.mouseDrag (mouseAt (module, c.translated (0.0f, -15.0f), c));
            module.mouseUp (mouseAt (module, c.translated (0.0f, -15.0f), c));
            check (std::abs (p.modulation.depthOf (0, M::targetIndex (ParamIDs::mix)) - 0.4f) < 0.01f,
                   "dragging a chip changes that amount");
        }

        // Clear: assignments gone, LFOs back to default, room knobs untouched.
        setParam (p, ParamIDs::tail, 3.3f);
        setParam (p, ParamIDs::lfoShape (1), 4.0f);
        setParam (p, ParamIDs::lfoRate (0), 7.0f);
        setParam (p, ParamIDs::lfoSync (1), 1.0f);
        p.clearModulation();
        check (! p.modulation.anyAssigned(), "Clear removes every assignment");
        bool lfoDefaults = true;
        for (int l = 0; l < M::numLfos; ++l)
            for (const auto& id : { ParamIDs::lfoRate (l), ParamIDs::lfoSync (l), ParamIDs::lfoDiv (l),
                                    ParamIDs::lfoShape (l), ParamIDs::lfoRetrig (l) })
                lfoDefaults = lfoDefaults && std::abs (p.apvts.getParameter (id)->getValue()
                                                       - p.apvts.getParameter (id)->getDefaultValue()) < 1e-6f;
        check (lfoDefaults, "Clear resets both LFOs to default");
        check (std::abs (paramValue (p, ParamIDs::tail) - 3.3f) < 0.02f, "Clear leaves the room knobs alone");

        // Randomize groups: each only touches its own knobs.
        auto snapshot = [&p]
        {
            std::map<juce::String, float> m;
            for (auto* prm : p.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                    m[rp->paramID] = rp->getValue();
            return m;
        };
        const std::map<int, juce::StringArray> owns {
            { RoomAudioProcessor::randomRoom, { "intensity", "balanceAuto", "balance", "tail", "space" } },
            { RoomAudioProcessor::randomAdvanced, { "preDelay", "size", "damping", "duck", "duckRelease" } },
            { RoomAudioProcessor::randomEq, { "eq1Gain", "eq2Gain", "eq3Gain", "eq4Gain", "eq5Gain" } },
            { RoomAudioProcessor::randomFinish, { "tape", "autoEq" } },
            { RoomAudioProcessor::randomLfos, { "lfo1Shape", "lfo1Sync", "lfo1Div", "lfo1Rate", "lfo2Shape", "lfo2Sync", "lfo2Div", "lfo2Rate" } },
        };
        juce::Random r (5);
        for (auto& [bit, ids] : owns)
        {
            p.randomizeMask = bit;
            bool onlyOwn = true, movedAny = false;
            for (int k = 0; k < 6; ++k)
            {
                const auto before = snapshot();
                p.randomizeKnobs (r);
                for (auto& [id, v] : snapshot())
                {
                    const auto moved = std::abs (v - before.at (id)) > 1e-6f;
                    if (moved && ! ids.contains (id))
                    {
                        onlyOwn = false;
                        std::cout << "    group " << bit << " moved " << id << "\n";
                    }
                    movedAny = movedAny || moved;
                }
            }
            check (onlyOwn && movedAny, "Randomize group " + juce::String (bit) + " only moves its own knobs");
        }
        p.randomizeMask = 0;
        const auto before = snapshot();
        p.randomizeKnobs (r);
        check (snapshot() == before, "Randomize with nothing ticked does nothing");
        check (RoomAudioProcessor().randomizeMask == RoomAudioProcessor::randomDefault
                   && (RoomAudioProcessor::randomDefault & RoomAudioProcessor::randomLfos) == 0,
               "by default Randomize leaves the LFOs alone");
    }

    //==========================================================================

    int screenshot (const juce::StringArray& args)
    {
        // OUT.png MODE ROOM_SOURCE TARGET
        RoomAudioProcessor p;
        p.setPresetRoot (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("room_shot_presets"));
        p.setPlayConfigDetails (2, 2, 48000, 512);
        p.prepareToPlay (48000, 512);

        auto& lib = p.getLibrary();
        if (auto* e = lib.find ("Factory/Dark Hall"))
            p.applyPreset (lib.loadPreset (*e), e->key);
        lib.setFavorite ("Factory/Dark Hall", true);
        lib.setFavorite ("Factory/Glass Bottle", true);
        p.getRoomLibrary().setFavorite ("Factory/Clap Hall", true);
        waitForAnalysis (p);
        juce::Thread::sleep (300);

        double sr = 0;
        const auto t = readAudio (juce::File (args[4]), sr);
        RoomAudioProcessorEditor ed (p);
        ed.setVisible (true);
        if (args[2] == "record")
            p.startRecording();
        if (args[2] == "lfo")
        {
            using M = room::Modulation;
            p.modulation.assign (0, M::targetIndex (ParamIDs::mix), 0.3f);
            p.modulation.assign (0, M::targetIndex (ParamIDs::space), 0.4f);
            p.modulation.assign (1, M::targetIndex (ParamIDs::size), 0.25f);
            p.modulation.assign (1, M::targetIndex (ParamIDs::intensity), -0.2f);
            setParam (p, ParamIDs::lfoShape (1), 6.0f);
            p.modulation.assign (0, M::targetIndex (ParamIDs::preDelay), 0.5f);
            p.modulation.assign (1, M::targetIndex (ParamIDs::preDelay), -0.3f);
        }
        juce::MidiBuffer midi;
        juce::AudioBuffer<float> io (2, 512);
        for (int pos = 0; pos < 200000 && t.getNumSamples() > 1024; pos += 512)
        {
            for (int ch = 0; ch < 2; ++ch)
                io.copyFrom (ch, 0, t, std::min (ch, t.getNumChannels() - 1), pos % (t.getNumSamples() - 512), 512);
            p.processBlock (io, midi);
            if ((pos / 512) % 4 == 0) { juce::Thread::sleep (34); juce::Timer::callPendingTimersSynchronously(); }
        }

        // Open the requested view by clicking through the UI's own entry points.
        const auto mode = args[2];
        std::function<void (juce::Component&)> findAndShow = [&] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (mode == "browser" || mode == "rooms")
                    if (auto* b = dynamic_cast<PresetBrowser*> (child))
                        if ((b->getKind() == Kind::room) == (mode == "rooms")) { b->setVisible (true); b->toFront (false); }
                if (mode == "save")
                    if (auto* bar = dynamic_cast<PresetBar*> (child)) if (bar->onSave) bar->onSave();
                if (mode == "newpack")
                {
                    if (auto* b = dynamic_cast<PresetBrowser*> (child))
                        if (b->getKind() == Kind::studio) { b->setVisible (true); b->toFront (false); }
                    if (auto* tb = dynamic_cast<juce::TextButton*> (child))
                        if (tb->getButtonText().containsIgnoreCase ("new pack") && tb->onClick) tb->onClick();
                }
                if (mode == "roomsave")
                    if (auto* ib = dynamic_cast<IconButton*> (child))
                        if (ib->getTooltip().startsWith ("Save this room") && ib->onClick) ib->onClick();
                findAndShow (*child);
            }
        };
        findAndShow (ed);
        if (mode == "lfo")
            ed.setAdvanced (true);
        if (mode == "advanced")
            ed.setAdvancedOpen (true);
        if (mode == "info")
            ed.showRoomInfo (true);
        if (mode == "about")
            ed.openAbout();
        if (mode.startsWith ("tour"))
            ed.showTour (mode.getTrailingIntValue() - 1);
        if (mode == "longname")
            p.setCurrentPreset ("Factory/x", "small room with a very long name");
        if (args.size() > 5)   // optional scale, e.g. 0.75 or 1.5
            ed.setSize (juce::roundToInt ((float) ed.contentWidth() * args[5].getFloatValue()),
                        juce::roundToInt (RoomAudioProcessorEditor::baseHeight * args[5].getFloatValue()));
        for (int i = 0; i < 4; ++i)   // let the 15 Hz editor timer come due
        {
            juce::Thread::sleep (70);
            juce::Timer::callPendingTimersSynchronously();
        }

        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
        if (mode == "menu" || mode == "mainmenu")
        {
            // Draw the randomize menu and a knob's modulation menu with the editor's
            // look and feel, laid over the snapshot where they would open.
            auto& lf = ed.getLookAndFeel();
            const juce::PopupMenu::Options opts;
            auto drawMenu = [&] (juce::Point<int> at, std::vector<std::tuple<juce::String, int, bool, bool>> rows)
            {
                // rows: text, kind (0 item, 1 header, 2 separator, 3 submenu), ticked, highlighted
                const auto border = lf.getPopupMenuBorderSizeWithOptions (opts);
                int w = 0, h = 0;
                for (auto& [text, kind, ticked, hi] : rows)
                {
                    int iw = 0, ih = 0;
                    lf.getIdealPopupMenuItemSizeWithOptions (text, kind == 2, 30, iw, ih, opts);
                    w = std::max (w, iw);
                    h += kind == 1 ? 26 : ih;
                }
                const auto scale = (float) img.getHeight() / (float) RoomAudioProcessorEditor::baseHeight;
                juce::Image menu (juce::Image::ARGB, w + border * 2, h + border * 2, true);
                {
                    juce::Graphics g (menu);
                    lf.drawPopupMenuBackgroundWithOptions (g, menu.getWidth(), menu.getHeight(), opts);
                    int y = border;
                    for (auto& [text, kind, ticked, hi] : rows)
                    {
                        int iw = 0, ih = 0;
                        lf.getIdealPopupMenuItemSizeWithOptions (text, kind == 2, 30, iw, ih, opts);
                        if (kind == 1)
                        {
                            lf.drawPopupMenuSectionHeaderWithOptions (g, { border, y, w, 26 }, text, opts);
                            y += 26;
                            continue;
                        }
                        juce::PopupMenu::Item item (text);
                        item.isSeparator = kind == 2;
                        item.isTicked = ticked;
                        if (kind == 3)
                            item.subMenu = std::make_unique<juce::PopupMenu>();
                        lf.drawPopupMenuItemWithOptions (g, { border, y, w, ih }, hi, item, opts);
                        y += ih;
                    }
                }
                juce::Graphics g (img);
                g.addTransform (juce::AffineTransform::scale (scale));
                g.drawImageAt (menu, at.x, at.y);
            };
            if (args[2] == "mainmenu")
            {
                drawMenu ({ 74, 80 }, { { "new preset", 0, false, false }, { "", 2, false, false }, { "tips", 0, true, true },
                                        { "", 2, false, false }, { "check for updates", 0, false, false },
                                        { "visit website", 0, false, false }, { "get support", 0, false, false },
                                        { juce::String (juce::CharPointer_UTF8 ("about room\xe2\x80\xa6")), 0, false, false },
                                        { "", 2, false, false }, { "room v0.1.0", 0, false, false } });
            }
            else
            {
            drawMenu ({ 600, 100 }, { { "randomize", 1, false, false }, { "randomize now", 0, false, false }, { "", 2, false, false },
                                      { "what moves", 1, false, false },
                                      { "room knobs  (intensity, balance, tail, space)", 0, true, true },
                                      { "advanced  (pre-delay, size, damping, ducking)", 0, true, false },
                                      { "eq bands", 0, false, false }, { "tape and auto eq", 0, false, false },
                                      { "lfo settings  (shape, rate)", 0, true, false }, { "", 2, false, false },
                                      { "all", 0, false, false }, { "none", 0, false, false }, { "back to default", 0, false, false } });
            drawMenu ({ 560, 470 }, { { "modulation", 1, false, false }, { juce::String (juce::CharPointer_UTF8 ("lfo 1 \xc2\xb7 25%")), 3, false, true },
                                      { "move with lfo 2", 0, false, false } });
            drawMenu ({ 790, 500 }, { { "10%", 0, false, false }, { "25%", 0, true, true }, { "50%", 0, false, false },
                                      { "100%", 0, false, false }, { "", 2, false, false }, { "invert", 0, false, false },
                                      { "remove from lfo 1", 0, false, false } });
            }
        }
        juce::File out (args[1]);
        out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat().writeImageToStream (img, os);
        return 0;
    }
}


    /** --bench [preset keys...]: how much of one core ROOM needs. */
    int bench (const juce::StringArray& args)
    {
        juce::StringArray keys;
        for (int i = 1; i < args.size(); ++i)
            keys.add (args[i]);
        if (keys.isEmpty())
            keys = { "Factory/Small Room", "Factory/Big Hall", "Factory/Pipe Space", "Factory/Dark Hall" };

        // --bench-rt: feed blocks at real-time pace, like a host does. The
        // background stages then have the time they would have in a DAW.
        const bool realtime = args[0] == "--bench-rt";
        const double seconds = realtime ? 10.0 : 30.0;
        std::cout << (realtime ? "(real-time paced)\n" : "(as fast as possible)\n");
        std::cout << "preset                     rate    block   % of one core   worst block (% of its time)   waits\n";
        if (realtime)
            keys.insert (0, "bypass");   // baseline: how noisy this machine's scheduling is
        const std::vector<double> rates = realtime ? std::vector<double> { 48000.0, 96000.0 } : std::vector<double> { 44100.0, 48000.0, 96000.0 };
        const std::vector<int> blocks = realtime ? std::vector<int> { 64, 256 } : std::vector<int> { 64, 256, 512 };
        for (auto& key : keys)
            for (double sr : rates)
                for (int block : blocks)
                {
                    RoomAudioProcessor p;
                    p.setPresetRoot (juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("room_bench_presets"));
                    p.setPlayConfigDetails (2, 2, sr, block);
                    p.prepareToPlay (sr, block);
                    auto& lib = p.getLibrary();
                    if (key == "bypass")
                        setParam (p, ParamIDs::bypass, 1.0f);
                    else if (auto* e = lib.find (key))
                        p.applyPreset (lib.loadPreset (*e), e->key);
                    else { std::cout << "  (no preset " << key << ")\n"; break; }
                    room::PartitionedConvolver::audioThreadWaits = 0;
                    waitForAnalysis (p);
                    juce::Thread::sleep (400);   // let the room build

                    juce::AudioBuffer<float> io (2, block);
                    juce::MidiBuffer midi;
                    juce::Random rng (7);
                    const int total = (int) (sr * seconds);
                    double worst = 0.0, busy = 0.0;
                    const auto t0 = juce::Time::getHighResolutionTicks();
                    for (int pos = 0; pos < total; pos += block)
                    {
                        for (int c = 0; c < 2; ++c)
                            for (int i = 0; i < block; ++i)
                                io.setSample (c, i, (rng.nextFloat() * 2.0f - 1.0f) * 0.2f);
                        const auto b0 = juce::Time::getHighResolutionTicks();
                        p.processBlock (io, midi);
                        const auto took = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - b0);
                        worst = std::max (worst, took);
                        busy += took;
                        if (realtime)   // wait for this block's moment, like an audio callback
                        {
                            const auto due = (pos + block) / sr;
                            while (juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) < due)
                                std::this_thread::yield();
                        }
                    }
                    const auto load = 100.0 * busy / seconds;
                    const auto worstPct = 100.0 * worst / (block / sr);
                    std::printf ("%-26s %6.0f  %5d   %10.2f %%     %10.1f %%   %8d\n",
                                 (key.contains ("/") ? key.fromFirstOccurrenceOf ("/", false, false) : key).toRawUTF8(),
                                 sr, block, load, worstPct, room::PartitionedConvolver::audioThreadWaits.load());
                }
        return 0;
    }

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray args;
    const bool startEmpty = ! (argc > 1 && juce::String (argv[1]) == "--first-run");
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    RoomAudioProcessor::startWithDefaultStudio = ! startEmpty;   // tests start from an empty ROOM
    RoomAudioProcessorEditor::autoShowTour = false;
    if (args[0] == "--first-run")
        args.remove (0);
    if (args[0] == "--screenshot" && args.size() >= 5)
        return screenshot (args);
    if (args[0] == "--bench" || args[0] == "--bench-rt")
        return bench (args);
    if (args[0] == "--levels")
        return levels();
    if (args[0] == "--render-presets" && args.size() > 1)
        return renderPresets (juce::File (args[1]));

    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("room_tests_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30)))
                          .getChildFile ("ROOMS");
    root.createDirectory();

    // A room source to "drop": the test audio if present, otherwise a click.
    auto roomSource = juce::File::getCurrentWorkingDirectory().getChildFile ("examples/audio/small_room_clap.wav");
    if (! roomSource.existsAsFile())
    {
        roomSource = root.getSiblingFile ("click.wav");
        juce::AudioBuffer<float> b (1, 48000);
        b.clear();
        juce::Random r (1);
        for (int i = 0; i < 48000; ++i)
            b.setSample (0, i, (r.nextFloat() * 2.0f - 1.0f) * std::exp (-i / 4000.0f));
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (roomSource.createOutputStream());
        if (auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (48000).withNumChannels (1).withBitsPerSample (24)))
            w->writeFromAudioSampleBuffer (b, 0, 48000);
    }

    testLibrary (root);
    testSaveLoad (root, roomSource);
    testSpace();
    testAdvanced (root);
    testRecord (root, roomSource);
    testLfo (root);
    testRoomsAndStudios (root, roomSource);
    testConvolver();
    testFirstRun();
    testSuppressorIdle();
    testFactoryPresets (root);
    testModulationUi (root);

    root.getParentDirectory().deleteRecursively();
    std::cout << (failures == 0 ? "ALL ROOM TESTS PASSED" : "ROOM TESTS FAILED") << " (" << checks - failures << "/" << checks << ")\n";
    return failures == 0 ? 0 : 1;
}
