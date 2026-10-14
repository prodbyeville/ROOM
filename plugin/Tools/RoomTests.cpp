// room_tests: ROOMS presets, Space, and state restore, tested against the real
// processor (no DAW needed).
//
//   room_tests                                           run all tests
//   room_tests --screenshot OUT.png MODE ROOM_SRC TARGET  save a UI snapshot
//        MODE = main | browser | save | advanced | record     [SCALE, e.g. 0.75]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "presets/PresetLibrary.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

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
        check (root.getChildFile ("User").isDirectory(), "User pack folder is created");

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
        root.getChildFile ("User").getChildFile ("broken.room").replaceWithText ("<ROOM_PRESET this is not xml");
        root.getChildFile ("User").getChildFile ("wrong.room").replaceWithText ("<SOMETHING_ELSE/>");
        proc.getLibrary().rescan();
        check (proc.getLibrary().getUnreadableCount() == 2, "damaged / foreign files are counted as unreadable");
        root.getChildFile ("User").getChildFile ("broken.room").deleteFile();
        root.getChildFile ("User").getChildFile ("wrong.room").deleteFile();

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
        check (proc.getCurrentPresetKey().isEmpty(), "dropping a sound starts a new, unsaved preset");

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
        check (root.getChildFile ("User/My Test Room.room").existsAsFile(), "file written");
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
        check (exportDir.getChildFile ("Eville/Renamed Room.room").existsAsFile(), "exported file exists");
        check (lib.exportPack ("Factory", exportDir) && exportDir.getChildFile ("Factory/Big Hall.room").existsAsFile(),
               "factory pack can be exported");
        const auto imported = lib.importFiles ({ exportDir.getChildFile ("Eville/Renamed Room.room") }, "Eville");
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
            // no clicks: no sudden sample jump after freezing
            float maxJump = 0, typical = 0;
            for (size_t i = (size_t) (0.7 * Rig::sr); i < (size_t) (4.0 * Rig::sr); ++i)
            {
                maxJump = std::max (maxJump, std::abs (held[i] - held[i - 1]));
                typical = std::max (typical, std::abs (held[i]));
            }
            check (maxJump < typical * 0.8f, "Freeze loop has no clicks");
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
        check (p.getCurrentPresetName().startsWith ("Recorded room"), "a recording starts a new, unsaved sound");
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
                if (mode == "browser")
                    if (auto* b = dynamic_cast<PresetBrowser*> (child)) { b->setVisible (true); b->toFront (false); }
                if (mode == "save")
                    if (auto* bar = dynamic_cast<PresetBar*> (child)) if (bar->onSave) bar->onSave();
                if (mode == "lfo")
                    if (auto* adv = dynamic_cast<AdvancedPanel*> (child)) adv->showModulationTab (true);
                findAndShow (*child);
            }
        };
        findAndShow (ed);
        if (mode == "advanced" || mode == "lfo")
            ed.setAdvanced (true);
        if (args.size() > 5)   // optional scale, e.g. 0.75 or 1.5
            ed.setSize (juce::roundToInt (RoomAudioProcessorEditor::baseWidth * args[5].getFloatValue()),
                        juce::roundToInt (RoomAudioProcessorEditor::baseHeight * args[5].getFloatValue()));
        juce::Timer::callPendingTimersSynchronously();

        auto img = ed.createComponentSnapshot (ed.getLocalBounds(), true, 1.0f);
        juce::File out (args[1]);
        out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat().writeImageToStream (img, os);
        return 0;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    if (args[0] == "--screenshot" && args.size() >= 5)
        return screenshot (args);

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

    root.getParentDirectory().deleteRecursively();
    std::cout << (failures == 0 ? "ALL ROOM TESTS PASSED" : "ROOM TESTS FAILED") << " (" << checks - failures << "/" << checks << ")\n";
    return failures == 0 ? 0 : 1;
}
