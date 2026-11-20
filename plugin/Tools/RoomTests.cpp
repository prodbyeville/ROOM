// room_tests: ROOMS presets, Space, and state restore, tested against the real
// processor (no DAW needed).
//
//   room_tests                                           run all tests
//   room_tests --screenshot OUT.png MODE ROOM_SRC TARGET  save a UI snapshot
//        MODE = main | browser | save | rooms | roomsave | advanced | record | lfo | info | menu   [SCALE, e.g. 0.75]

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "RoomMenu.h"
#include "dsp/PartitionedConvolver.h"
#include "dsp/ResonanceSuppressor.h"
#include "presets/PresetLibrary.h"
#include "licensing/License.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdlib>
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
        return rp != nullptr ? rp->convertFrom0to1 (rp->getValue()) : std::numeric_limits<float>::quiet_NaN();   // NaN fails any check
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
                     { ParamIDs::mix, 1.0f }, { ParamIDs::balanceAuto, 0.0f }, { ParamIDs::toneAmount, 0.0f }, { ParamIDs::spaceAmount, 1.0f },
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
            check (room::Randomizer::isSelected (rig.proc, "room"), "Randomize picks a new room by default");
            room::Randomizer::setSelected (rig.proc, "room", false);   // the room on its own: below
            room::Randomizer::randomizeSelected (rig.proc, r);
            const auto after = paramValue (rig.proc, ParamIDs::intensity) + paramValue (rig.proc, ParamIDs::tail)
                             + paramValue (rig.proc, ParamIDs::size);
            check (std::abs (after - before) > 0.01f, "Randomize changes the knobs");
            check (std::abs (paramValue (rig.proc, ParamIDs::mix) - 0.37f) < 0.011f, "Randomize keeps Dry/Wet");
            check (std::abs (paramValue (rig.proc, ParamIDs::output) + 4.0f) < 0.05f, "Randomize keeps Output");
            check (paramValue (rig.proc, ParamIDs::bypass) < 0.5f && paramValue (rig.proc, ParamIDs::freeze) < 0.5f,
                   "Randomize never bypasses or freezes");
            check (analysisXml (rig.proc) == room, "Randomize keeps the room when room isn't ticked");
            for (int k = 0; k < 50; ++k)
            {
                room::Randomizer::randomizeSelected (rig.proc, r);
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
        waitUntil ([&] { return ! p.isRecording()
                                && (p.getStatus() == RoomAudioProcessor::Status::failed
                                    || p.getStatus() == RoomAudioProcessor::Status::ready); }, 3000);
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
        room::Randomizer::setSelected (proc, "lfo1", true);
        room::Randomizer::setSelected (proc, "tail", false);
        room::Randomizer::setIntensity (proc, 0.9);
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        RoomAudioProcessor proc2;
        proc2.setPresetRoot (root);
        proc2.setStateInformation (state.getData(), (int) state.getSize());
        check (proc2.getCurrentRoomKey() == "User/Test Clap Room", "project remembers the ROOM");
        check (room::Randomizer::isSelected (proc2, "lfo1") && ! room::Randomizer::isSelected (proc2, "tail")
                   && std::abs (room::Randomizer::getIntensity (proc2) - 0.9) < 1e-6,
               "project remembers what Randomize touches and how wild");

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
                tour->showStep (6);   // six steps: past the last one closes it
                check (! tour->isVisible(), "done closes the tour");
            }
        }
    }

    /** Like Apple's auval stress test: a fresh ROOM (Init Room loaded), 44.1 kHz,
        every parameter jumped to a random value before each block, noise in.
        Nothing may come out that isn't a finite number. */
    void testStress()
    {
        std::cout << "Stress (auval-style)\n";
        RoomAudioProcessor::startWithDefaultStudio = true;
        int bad = 0, badFirstBlock = -1, badFirstSample = -1;
        for (const auto& [ins, outs] : { std::pair { 2, 2 }, std::pair { 1, 2 }, std::pair { 1, 1 } })
        for (const int maxBlock : { 512, 1156, 4096 })
            for (const int firstParamsJump : { 0, 1 })
            {
                RoomAudioProcessor p;
                juce::AudioProcessor::BusesLayout layout;
                layout.inputBuses.add (ins == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
                layout.outputBuses.add (outs == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
                if (! p.setBusesLayout (layout))
                    continue;
                p.setRateAndBufferSizeDetails (44100.0, maxBlock);
                p.prepareToPlay (44100.0, maxBlock);
                const auto nch = std::max (ins, outs);
                juce::AudioBuffer<float> io (nch, 512);
                juce::MidiBuffer midi;
                juce::Random r (maxBlock * 7 + firstParamsJump);
                const auto blocks = (int) (20.0 * 44100.0 / 512.0);
                for (int b = 0; b < blocks; ++b)
                {
                    if (firstParamsJump == 1 || b > 0)
                        for (auto* prm : p.getParameters())
                            prm->setValueNotifyingHost (r.nextFloat());
                    for (int ch = 0; ch < nch; ++ch)
                        for (int i = 0; i < 512; ++i)
                            io.setSample (ch, i, ch < ins ? (r.nextFloat() * 2.0f - 1.0f) * 0.5f : 0.0f);
                    p.processBlock (io, midi);
                    for (int ch = 0; ch < outs; ++ch)
                        for (int i = 0; i < 512; ++i)
                            if (! std::isfinite (io.getSample (ch, i)))
                            {
                                if (bad++ == 0)
                                {
                                    badFirstBlock = b; badFirstSample = i;
                                    std::cout << "    first bad: " << ins << " in / " << outs << " out, max block " << maxBlock << "\n";
                                }
                            }
                }
            }
        // Every sample rate a host might use, with its room fully built for it.
        for (const double rate : { 11025.0, 22050.0, 32000.0, 44100.0, 88200.0, 192000.0 })
        {
            RoomAudioProcessor p;
            p.setPlayConfigDetails (2, 2, rate, 512);
            p.prepareToPlay (rate, 512);
            juce::AudioBuffer<float> io (2, 512);
            juce::MidiBuffer midi;
            juce::Random r (7);
            int badHere = 0;
            // Let the room rebuild for this rate and swap in before measuring.
            for (int b = 0; b < 150; ++b)
            {
                io.clear();
                p.processBlock (io, midi);
                juce::Thread::sleep (5);
            }
            for (int b = 0; b < (int) (2.0 * rate / 512.0); ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        io.setSample (ch, i, (r.nextFloat() * 2.0f - 1.0f) * 0.5f);
                p.processBlock (io, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                        if (! std::isfinite (io.getSample (ch, i)))
                            ++badHere;
            }
            if (badHere > 0)
                std::cout << "    " << rate << " Hz: " << badHere << " bad samples\n";
            check (badHere == 0, ("no NaN or inf at " + juce::String (rate, 0) + " Hz").toRawUTF8());
        }

        // Like auval: the same instance re-initialised over and over with new
        // layouts and rates, rendering right away each time (no settling).
        {
            RoomAudioProcessor p;
            juce::Random r (99);
            juce::MidiBuffer midi;
            const std::tuple<int, int, double, int> steps[] = {
                { 2, 2, 48000.0, 512 }, { 1, 1, 44100.0, 1156 }, { 1, 2, 96000.0, 256 }, { 2, 2, 22050.0, 4096 },
                { 1, 1, 48000.0, 512 }, { 2, 2, 44100.0, 1156 }, { 1, 2, 44100.0, 512 }, { 2, 2, 44100.0, 512 } };
            for (int round = 0; round < 4; ++round)
                for (const auto& [ins, outs, rate, maxBlock] : steps)
                {
                    p.releaseResources();
                    juce::AudioProcessor::BusesLayout layout;
                    layout.inputBuses.add (ins == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
                    layout.outputBuses.add (outs == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo());
                    p.setBusesLayout (layout);
                    p.setRateAndBufferSizeDetails (rate, maxBlock);
                    p.prepareToPlay (rate, maxBlock);
                    const auto nch = std::max (ins, outs);
                    juce::AudioBuffer<float> io (nch, std::min (512, maxBlock));
                    for (int b = 0; b < 40; ++b)
                    {
                        for (auto* prm : p.getParameters())
                            prm->setValueNotifyingHost (r.nextFloat());
                        for (int ch = 0; ch < nch; ++ch)
                            for (int i = 0; i < io.getNumSamples(); ++i)
                                io.setSample (ch, i, ch < ins ? (r.nextFloat() * 2.0f - 1.0f) * 0.5f : 0.0f);
                        p.processBlock (io, midi);
                        for (int ch = 0; ch < outs; ++ch)
                            for (int i = 0; i < io.getNumSamples(); ++i)
                                if (! std::isfinite (io.getSample (ch, i)) && bad++ == 0)
                                {
                                    badFirstBlock = b; badFirstSample = i;
                                    std::cout << "    first bad (re-init): " << ins << " in / " << outs << " out, "
                                              << rate << " Hz, max block " << maxBlock << ", round " << round << "\n";
                                }
                        if (b % 8 == 0)
                            juce::Thread::sleep (1);
                    }
                }
        }
        RoomAudioProcessor::startWithDefaultStudio = false;
        if (bad > 0)
            std::cout << "    " << bad << " bad samples, first at block " << badFirstBlock << " sample " << badFirstSample << "\n";
        check (bad == 0, "random parameters every block never produce NaN or inf");
    }

    //==========================================================================
    // Licensing: WARP's RSA licenses, signed here with a throwaway key pair.

    struct TestKeys { juce::String pub, priv; };
    const TestKeys& testKeys()
    {
        static const TestKeys keys = []
        {
            juce::RSAKey pub, priv;
            juce::RSAKey::createKeyPair (pub, priv, 512);
            return TestKeys { pub.toString(), priv.toString() };
        }();
        return keys;
    }

    LicenseCrypto::License makeLicense (const juce::String& product = "room",
                                        const juce::String& machine = LicenseManager::machineId(),
                                        const juce::String& signer = testKeys().priv)
    {
        LicenseCrypto::License l { product, "Ann@Example.com", "ROOM-ABCDE-FGHJK-LMNPQ-RSTUV", machine, "2026-11-22T17:00:00Z", {} };
        l.signature = LicenseCrypto::sign (LicenseCrypto::payload (l), signer);
        return l;
    }

    void testLicensing (const juce::File& root)
    {
        std::cout << "Licensing\n";
        const auto& keys = testKeys();
        LicenseManager::setPublicKeyForTests (keys.pub);
        const auto folder = root.getSiblingFile ("license_folder");
        folder.deleteRecursively();
        LicenseManager::setFolderForTests (folder);

        // --- the signed text: byte for byte what WARP's server signs (Server/src/index.js: payload) ---
        check (LicenseCrypto::payload (makeLicense()) == "warp-license-v1\nroom\nann@example.com\nROOM-ABCDE-FGHJK-LMNPQ-RSTUV\n"
                                                          + LicenseManager::machineId() + "\n2026-11-22T17:00:00Z",
               "the signed text matches the server's");
        check (LicenseManager::machineId().length() == 32 && LicenseManager::machineId().containsOnly ("0123456789abcdef"),
               "the computer id is the hex the server accepts");
        check (juce::String (LicenseManager::product) == "room", "every request says product: room");

        // --- which licenses count ---
        check (LicenseManager::isValid (makeLicense()), "a ROOM license signed by the server is accepted");
        check (! LicenseManager::isValid (makeLicense ("warp")), "a WARP license doesn't open ROOM");
        check (! LicenseManager::isValid (makeLicense ("room", "another-computer")), "a license for another computer is rejected");
        auto edited = makeLicense();
        edited.email = "someone@else.com";
        check (! LicenseManager::isValid (edited), "an edited license is rejected");
        juce::RSAKey otherPub, otherPriv;
        juce::RSAKey::createKeyPair (otherPub, otherPriv, 512);
        check (! LicenseManager::isValid (makeLicense ("room", LicenseManager::machineId(), otherPriv.toString())),
               "a license signed by anyone else is rejected");
        check (! LicenseManager::isValid ({}), "an empty license is rejected");
        check (! LicenseManager::isValid (makeLicense(), LicensePublicKey::key), "the real public key doesn't take test signatures");

        // --- the manager and open sessions ---
        RoomAudioProcessor::enforceLicense = true;
        {
            juce::SharedResourcePointer<LicenseManager> m;
            check (! m->isActivated(), "fresh computer: not activated");
            RoomAudioProcessor blank;
            check (blank.isLicenseLocked(), "without a license ROOM loads locked");

            // Locked: audio passes through dry, aligned to the reported latency.
            blank.setPlayConfigDetails (2, 2, 48000.0, 512);
            blank.prepareToPlay (48000.0, 512);
            const auto lat = blank.getLatencySamples();
            juce::AudioBuffer<float> in (2, 48000), io (2, 512);
            juce::Random r (5);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < in.getNumSamples(); ++i)
                    in.setSample (ch, i, r.nextFloat() - 0.5f);
            juce::MidiBuffer midi;
            float worst = 0.0f;
            for (int pos = 0; pos + 512 <= in.getNumSamples(); pos += 512)
            {
                for (int ch = 0; ch < 2; ++ch)
                    io.copyFrom (ch, 0, in, ch, pos, 512);
                blank.processBlock (io, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < 512; ++i)
                    {
                        const auto src = pos + i - lat;
                        const auto expect = src >= 0 ? in.getSample (ch, src) : 0.0f;
                        worst = std::max (worst, std::abs (io.getSample (ch, i) - expect));
                    }
            }
            check (worst < 1.0e-5f, "locked ROOM passes the audio through dry, latency-matched");

            // Saved sessions still open while locked.
            RoomAudioProcessor other;
            other.getParameters()[0]->setValueNotifyingHost (0.37f);
            juce::MemoryBlock state;
            other.getStateInformation (state);
            blank.setStateInformation (state.getData(), (int) state.getSize());
            check (std::abs (blank.getParameters()[0]->getValue() - 0.37f) < 0.01f, "a session still opens while locked");

            check (! m->installForTests (makeLicense ("warp")), "the manager refuses a WARP license");
            check (m->installForTests (makeLicense()), "the manager takes a good license");
            m->dispatchPendingMessages();
            check (! blank.isLicenseLocked(), "signing in unlocks an open ROOM right away");
            check (m->licensedEmail() == "Ann@Example.com", "the menu knows who it's licensed to");
            check (folder.getChildFile ("license.json").existsAsFile(), "the license is saved in ROOM's own folder");

            // A revoked license (the 14-day check) doesn't lock a session that's open.
            m->revokeForTests();
            m->dispatchPendingMessages();
            check (! blank.isLicenseLocked(), "a revoked license never locks a session that's already open");
            RoomAudioProcessor next;
            check (next.isLicenseLocked(), "the next load after revoking is locked");

            // "deactivate this computer" locks right away.
            m->installForTests (makeLicense());
            m->dispatchPendingMessages();
            check (! next.isLicenseLocked(), "signing in again unlocks it");
            next.lockLicense();
            check (next.isLicenseLocked(), "deactivating locks ROOM right away");
            m->installForTests (makeLicense());
        }
        {
            juce::SharedResourcePointer<LicenseManager> m2;
            check (m2->isActivated(), "the license is remembered");
            RoomAudioProcessor again;
            check (! again.isLicenseLocked(), "an activated computer opens ROOM unlocked");
        }
        {
            folder.getChildFile ("license.json").replaceWithText (folder.getChildFile ("license.json").loadFileAsString()
                                                                       .replace ("ann@example.com", "bob@example.com", true));
            juce::SharedResourcePointer<LicenseManager> m3;
            check (! m3->isActivated(), "an edited license file doesn't count");
        }
        RoomAudioProcessor::enforceLicense = false;
        folder.deleteRecursively();
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

    // Intensity 0..200 % (old 100 % is 50), extreme mode, the glitch stutter,
    // new rooms starting at 50 %, and the stage's quick-select slots.
    void testIntensityGlitchQuick (const juce::File& root, const juce::File& roomSource)
    {
        std::cout << "intensity / glitch / quick select\n";
        {
            Rig rig (root);
            auto* p = rig.proc.apvts.getParameter (ParamIDs::intensity);
            check (p->getText (p->convertTo0to1 (1.0f), 16) == "50%" && p->getText (p->convertTo0to1 (2.0f), 16) == "100%"
                       && p->getText (p->convertTo0to1 (4.0f), 16) == "200%",
                   "intensity shows the measured room as 50 %, twice as 100 %, extreme as 200 %");
            check (std::abs (p->convertFrom0to1 (p->getDefaultValue()) - 1.0f) < 1e-4f, "intensity defaults to the room as measured (50 %)");

            std::vector<float> noise ((size_t) (0.6 * Rig::sr));
            juce::Random r (3);
            for (auto& x : noise) x = (r.nextFloat() * 2.0f - 1.0f) * 0.25f;

            setParam (rig.proc, ParamIDs::intensity, 2.0f);
            rig.settle();
            const auto at100 = rig.run (noise, 1.5);
            {
                // Up to 100 % the extreme / stutter stage is off: the wet sound passes untouched.
                room::WetFx fx;
                fx.prepare (Rig::sr, 2);
                fx.setTargets (0.0f, false);
                float worst = 0.0f;
                for (int i = 0; i < 20000; ++i)
                {
                    float w[2] = { noise[(size_t) (i % (int) noise.size())], -noise[(size_t) (i % (int) noise.size())] };
                    const float in0 = w[0], in1 = w[1];
                    fx.processSample (w, 2);
                    worst = std::max ({ worst, std::abs (w[0] - in0), std::abs (w[1] - in1) });
                }
                check (worst == 0.0f && fx.isIdle(), "up to 100 % there is no extreme processing (untouched)");
            }
            setParam (rig.proc, ParamIDs::intensity, 4.0f);
            rig.settle();
            const auto at200 = rig.run (noise, 1.5);
            float peak = 0.0f, diff = 0.0f;
            bool finite = true;
            for (size_t i = 0; i < at200.size(); ++i)
            {
                finite = finite && std::isfinite (at200[i]);
                peak = std::max (peak, std::abs (at200[i]));
                diff = std::max (diff, std::abs (at200[i] - at100[i]));
            }
            check (finite && peak < 2.0f, "extreme mode stays finite and bounded");
            check (diff > 0.05f, "extreme mode (200 %) changes the sound");
            const auto lv100 = rmsDb (at100, 0.0, 1.0), lv200 = rmsDb (at200, 0.0, 1.0);
            check (std::abs (lv200 - lv100) < 12.0, "extreme mode stays near the room's level (" + juce::String (lv200 - lv100, 1) + " dB)");

            setParam (rig.proc, ParamIDs::intensity, 1.0f);
            rig.settle();
            const auto plain = rig.run (noise, 2.0);
            setParam (rig.proc, ParamIDs::glitch, 1.0f);
            rig.proc.reset();
            const auto glitched = rig.run (noise, 2.0);
            float gd = 0.0f, gp = 0.0f;
            for (size_t i = 0; i < glitched.size(); ++i)
            {
                gd = std::max (gd, std::abs (glitched[i] - plain[i]));
                gp = std::max (gp, std::abs (glitched[i]));
            }
            check (gd > 0.01f && gp < 2.0f, "glitch stutters the room (and stays bounded)");
            setParam (rig.proc, ParamIDs::glitch, 0.0f);
        }
        {
            RoomAudioProcessor proc;
            proc.setPresetRoot (root);
            proc.setPlayConfigDetails (2, 2, 48000.0, 512);
            proc.prepareToPlay (48000.0, 512);
            setParam (proc, ParamIDs::intensity, 3.2f);
            proc.loadRoomFile (roomSource);
            waitForAnalysis (proc);
            check (std::abs (paramValue (proc, ParamIDs::intensity) - 1.0f) < 1e-3f, "a new room starts at intensity 50 %");

            auto& rooms = proc.getRoomLibrary();
            const auto first = rooms.getEntries().front().key;
            const auto second = rooms.getEntries().back().key;
            rooms.setQuickSlot (2, first);
            check (rooms.getQuickSlots()[2] == first && rooms.isQuick (first), "a quick-select slot takes a room");
            check (rooms.setQuick (second, true) && rooms.getQuickSlots().indexOf (second) == 0, "add to quick select fills the first free slot");
            rooms.setQuickSlot (4, first);
            check (rooms.getQuickSlots()[2].isEmpty() && rooms.getQuickSlots()[4] == first, "a room sits in one quick slot only");
            {
                PresetLibrary again (rooms.getRoot(), {}, Kind::room);
                check (again.getQuickSlots()[4] == first && again.getQuickSlots()[0] == second, "quick select is saved");
            }
            rooms.setQuick (second, false);
            rooms.setQuickSlot (4, {});
            check (! rooms.isQuick (first) && ! rooms.isQuick (second), "quick select slots can be emptied");
        }
    }

    void testNoiseRateToneSync (const juce::File& root)
    {
        std::cout << "noise / rate / tone + space / glitch sync / quick slots\n";
        juce::MessageManager::getInstance();

        // --- Noise loops are seamless ---
        {
            room::NoiseBank bank;
            check (bank.size() == room::NoiseBank::names().size() && bank.size() >= 4, "every noise profile has a slot");
            check (room::NoiseBank::names()[0] == "vinyl" && room::NoiseBank::names()[3] == "room tone",
                   "the four 1.0.2 noises keep their places (projects saved the index)");
           #if ROOM_HAS_NOISE_FILES
            check (bank.size() >= 50, "the recorded noises are listed (" + juce::String (bank.size()) + ")");
           #endif
            // Unpacked only when asked for: a recording isn't in memory until it's chosen.
            if (bank.size() > 4)
            {
                const auto* before = bank.tryGet (bank.size() - 1);
                juce::Thread::sleep (400);
                const auto* after = bank.tryGet (bank.size() - 1);
                check (before == nullptr && after != nullptr, "a recording is unpacked on the bank's thread when it's asked for");
            }
            float worstJump = 0.0f;
            int loaded = 0;
            juce::String worstName;
            for (int i = 0; i < bank.size(); ++i)
            {
                const auto* l = bank.getNow (i);
                if (l == nullptr)
                    continue;
                ++loaded;
                const auto n = l->length;
                for (int ch = 0; ch < 2; ++ch)
                {
                    // the wrap (last sample -> first) should look like any other step: compare
                    // it with the loop's own 99.9th percentile step
                    std::vector<float> steps;
                    steps.reserve ((size_t) n);
                    for (int k = 1; k < n; ++k)
                        steps.push_back (std::abs (l->sample (ch, k) - l->sample (ch, k - 1)));
                    std::nth_element (steps.begin(), steps.begin() + (long) (steps.size() * 999 / 1000), steps.end());
                    const auto p999 = std::max (1.0e-5f, steps[steps.size() * 999 / 1000]);
                    const auto jump = std::abs (l->sample (ch, 0) - l->sample (ch, n - 1)) / p999;
                    if (jump > worstJump) { worstJump = jump; worstName = room::NoiseBank::names()[i]; }
                }
            }
            check (loaded == bank.size(), "every noise unpacks (" + juce::String (loaded) + " of " + juce::String (bank.size()) + ")");
            check (worstJump <= 1.0f, "noise loops wrap without a click (worst: " + worstName + ", "
                                          + juce::String (worstJump, 2) + "x its own biggest steps)");

            // A sine with a non-whole number of cycles: the crossfade hides the seam.
            juce::AudioBuffer<float> sine (2, 44100);
            for (int k = 0; k < 44100; ++k)
                for (int ch = 0; ch < 2; ++ch)
                    sine.setSample (ch, k, std::sin (2.0f * juce::MathConstants<float>::pi * 3.37f * (float) k / 44100.0f));
            const auto loop = room::NoiseBank::makeSeamless (sine, 4000);
            const auto seam = std::abs (loop.getSample (0, 0) - loop.getSample (0, loop.getNumSamples() - 1));
            check (seam < 0.01f, "the loop crossfade joins the end to the start smoothly");
        }

        // --- Noise in the plugin: off = untouched; follow 1 = silent without input ---
        {
            Rig rig (root);
            std::vector<float> hit ((size_t) (0.2 * Rig::sr), 0.0f);
            for (size_t i = 0; i < 2000; ++i) hit[i] = 0.5f * std::sin ((float) i * 0.05f);
            {
                // Level 0: the noise stage is idle and adds nothing at all.
                room::NoiseBank bank;
                room::NoisePlayer np;
                np.setBank (&bank);
                np.prepare (Rig::sr, 512);
                np.setTargets (0, 0.0f, 0.5f);
                juce::AudioBuffer<float> b (2, 512);
                for (int i = 0; i < 512; ++i) for (int ch = 0; ch < 2; ++ch) b.setSample (ch, i, hit[(size_t) i]);
                juce::AudioBuffer<float> copy (b);
                np.trackInput (b, 512);
                if (! np.isIdle()) np.addTo (b, 512);
                float d = 0.0f;
                for (int i = 0; i < 512; ++i) d = std::max (d, std::abs (b.getSample (0, i) - copy.getSample (0, i)));
                check (np.isIdle() && d == 0.0f, "noise at level 0 is off and adds nothing");
            }
            setParam (rig.proc, ParamIDs::noiseLevel, 0.8f);
            setParam (rig.proc, ParamIDs::noiseFollow, 0.0f);
            setParam (rig.proc, ParamIDs::noiseStop, 0.0f);   // these hear the noise with nothing playing
            rig.proc.reset();
            std::vector<float> silence ((size_t) (0.5 * Rig::sr), 0.0f);
            const auto always = rig.run (silence, 1.0);
            check (rmsDb (always, 0.5, 1.4) > -50.0, "noise at follow 0 plays with nothing coming in (" + juce::String (rmsDb (always, 0.5, 1.4), 1) + " dB)");
            setParam (rig.proc, ParamIDs::noiseFollow, 1.0f);
            rig.proc.reset();
            const auto followed = rig.run (silence, 1.5);
            check (rmsDb (followed, 1.0, 1.9) < rmsDb (always, 0.5, 1.4) - 30.0, "noise at follow 1 stays quiet while nothing comes in");
            std::vector<float> loud ((size_t) (1.0 * Rig::sr));
            for (size_t i = 0; i < loud.size(); ++i) loud[i] = 0.3f * std::sin ((float) i * 0.03f);
            setParam (rig.proc, ParamIDs::mix, 0.0f);   // only the dry and the noise
            rig.proc.reset();
            const auto playing = rig.run (loud, 0.5);
            float noiseOnTop = 0.0f;
            for (size_t i = (size_t) (0.3 * Rig::sr); i < (size_t) (0.9 * Rig::sr); ++i)
                noiseOnTop = std::max (noiseOnTop, std::abs (playing[i] - loud[i]));
            check (noiseOnTop > 0.003f, "noise at follow 1 plays while sound comes in");

            // Stop (on by default): with the transport stopped and nothing coming in, the noise goes quiet
            check (rig.proc.apvts.getParameter (ParamIDs::noiseStop)->getDefaultValue() > 0.5f, "the noise's stop is on by default");
            setParam (rig.proc, ParamIDs::noiseStop, 1.0f);
            setParam (rig.proc, ParamIDs::noiseFollow, 0.0f);
            rig.proc.reset();
            const auto stopped = rig.run (silence, 1.0);
            check (rmsDb (stopped, 0.5, 1.4) < -90.0, "stop: no hiss while nothing plays (" + juce::String (rmsDb (stopped, 0.5, 1.4), 1) + " dB)");
            rig.proc.reset();
            const auto stillPlaying = rig.run (loud, 0.2);
            float onTop = 0.0f;
            for (size_t i = (size_t) (0.3 * Rig::sr); i < (size_t) (0.9 * Rig::sr); ++i)
                onTop = std::max (onTop, std::abs (stillPlaying[i] - loud[i]));
            check (onTop > 0.003f, "stop: the noise plays while sound comes in");
            setParam (rig.proc, ParamIDs::mix, 1.0f);
            setParam (rig.proc, ParamIDs::noiseLevel, 0.0f);
        }

        // --- Rate: off at 44.1 kHz; below, clean (filtered) sample-rate reduction ---
        {
            RoomAudioProcessor pp;
            pp.setPresetRoot (root);
            auto* p = pp.apvts.getParameter (ParamIDs::rate);
            check (p->getDefaultValue() == 0.0f && p->getText (0.0f, 16) == "44.1 kHz", "rate starts at 44.1 kHz with the knob all the way left (off)");
            check (p->convertFrom0to1 (1.0f) < 600.0f && p->convertFrom0to1 (0.5f) < 6000.0f, "turning rate right lowers it (down to 500 Hz, log scale)");
            // At 4 kHz: a 500 Hz tone passes, a 6 kHz tone (above the new Nyquist) is gone,
            // and the 500 Hz tone gains no aliases (nothing between 2.2 and 20 kHz).
            auto run = [] (float freq, float rateHz, std::vector<float>& out)
            {
                room::RateReducer rr;
                rr.prepare (48000.0);
                out.clear();
                for (int i = 0; i < 48000 * 2; ++i)
                {
                    float x[2] = { 0.5f * std::sin (juce::MathConstants<float>::twoPi * freq * (float) i / 48000.0f), 0.0f };
                    rr.processSample (x, 2, rateHz);
                    if (i >= 48000) out.push_back (x[0]);   // after the glide has settled
                }
            };
            auto rms = [] (const std::vector<float>& v) { double e = 0; for (auto x : v) e += x * x; return std::sqrt (e / (double) v.size()); };
            std::vector<float> low, high;
            run (500.0f, 4000.0f, low);
            run (6000.0f, 4000.0f, high);
            const auto lowDb = juce::Decibels::gainToDecibels ((float) rms (low) / 0.3536f);
            const auto highDb = juce::Decibels::gainToDecibels ((float) rms (high) / 0.3536f);
            check (std::abs (lowDb) < 1.5f, "rate at 4 kHz keeps a 500 Hz tone (" + juce::String (lowDb, 2) + " dB)");
            check (highDb < -40.0f, "rate at 4 kHz removes a 6 kHz tone cleanly (" + juce::String (highDb, 1) + " dB)");
            {
                // aliases / images of the 500 Hz tone: remove the tone with a narrow notch, measure what's left
                juce::dsp::IIR::Filter<float> notch;
                notch.coefficients = juce::dsp::IIR::Coefficients<float>::makeNotch (48000.0, 500.0f, 2.0f);
                std::vector<float> rest;
                for (auto x : low) rest.push_back (notch.processSample (x));
                rest.erase (rest.begin(), rest.begin() + 4800);
                const auto restDb = juce::Decibels::gainToDecibels ((float) rms (rest) / 0.3536f);
                check (restDb < -45.0f, "rate at 4 kHz adds no crunch (everything but the tone at " + juce::String (restDb, 1) + " dB)");
            }
            check (room::RateReducer::isOff (44100.0f) && ! room::RateReducer::isOff (44000.0f), "only the top of rate is off");
        }

        // --- Glitch: more with intensity, and on the 1/16s when synced ---
        {
            auto run = [] (float amount, bool sync, int& firstDiff)
            {
                room::WetFx fx;
                fx.prepare (48000.0, 2);
                fx.setTargets (0.0f, true, amount, sync);
                fx.setTransport (true, 0.0, 120.0);
                juce::Random r (9);
                int changed = 0;
                firstDiff = -1;
                for (int i = 0; i < 48000 * 10; ++i)
                {
                    const auto in = r.nextFloat() - 0.5f;
                    float w[2] = { in, in };
                    fx.processSample (w, 2);
                    if (w[0] != in) { ++changed; if (firstDiff < 0) firstDiff = i; }
                }
                return changed;
            };
            int f = 0;
            const auto low = run (0.1f, false, f), high = run (1.0f, false, f);
            check (high > low * 1.3, "glitch stutters more as its amount goes up (" + juce::String (low) + " -> " + juce::String (high) + " samples)");
            run (0.6f, true, f);
            const auto sixteenths = (double) f / 48000.0 * 2.0 * 4.0;   // 120 bpm: 2 beats a second, 4 sixteenths a beat
            check (f > 0 && std::abs (sixteenths - std::round (sixteenths)) < 0.01, "synced glitch starts on a 1/16 (at " + juce::String (sixteenths, 3) + ")");
        }

        // --- Tone and space: separate, synced if asked; the editor's new pieces ---
        {
            RoomAudioProcessor p;
            p.setPresetRoot (root);
            RoomAudioProcessorEditor ed (p);
            ed.setSize (RoomAudioProcessorEditor::closedWidth, RoomAudioProcessorEditor::baseHeight);
            ed.turnByHand (true, 0.3);
            check (std::abs (paramValue (p, ParamIDs::toneAmount) - 0.3f) < 0.011f && std::abs (paramValue (p, ParamIDs::spaceAmount) - 0.5f) < 0.011f,
                   "tone moves on its own (space stays)");
            // tone / space sync is retired: even a project with it on moves only the knob you turn
            setParam (p, ParamIDs::toneSpaceSync, 1.0f);
            ed.turnByHand (true, 0.5);
            check (std::abs (paramValue (p, ParamIDs::toneAmount) - 0.5f) < 0.011f && std::abs (paramValue (p, ParamIDs::spaceAmount) - 0.5f) < 0.011f,
                   "tone / space sync is gone: turning tone leaves space alone");
            setParam (p, ParamIDs::toneSpaceSync, 0.0f);

            // Old presets and projects had one balance b: they load as tone 1 - b, space b.
            {
                auto& studios = p.getLibrary();
                auto tree = studios.loadPreset (*studios.find ("Factory/Dark Hall"));
                auto params = tree.getChildWithName ("PARAMS");
                for (int i = params.getNumChildren(); --i >= 0;)
                    if (params.getChild (i)["id"].toString().startsWith ("tone") || params.getChild (i)["id"].toString() == "spaceAmount"
                        || params.getChild (i)["id"].toString() == "balance")
                        params.removeChild (i, nullptr);
                juce::ValueTree old ("PARAM");
                old.setProperty ("id", "balance", nullptr);
                old.setProperty ("value", 0.74f, nullptr);
                params.appendChild (old, nullptr);
                p.applyPreset (tree, "Factory/Dark Hall");
                check (std::abs (paramValue (p, ParamIDs::toneAmount) - 0.26f) < 0.011f && std::abs (paramValue (p, ParamIDs::spaceAmount) - 0.74f) < 0.011f,
                       "an old preset's balance 0.74 loads as tone 26 / space 74");
            }
            {
                juce::MemoryBlock state;
                p.getStateInformation (state);
                auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
                auto* params = xml->getChildByName ("PARAMS");
                for (auto* e = params->getFirstChildElement(); e != nullptr;)
                {
                    auto* next = e->getNextElement();
                    if (e->getStringAttribute ("id") == "toneAmount" || e->getStringAttribute ("id") == "spaceAmount")
                        params->removeChildElement (e, true);
                    e = next;
                }
                auto* old = params->createNewChildElement ("PARAM");
                old->setAttribute ("id", "balance");
                old->setAttribute ("value", 0.2);
                juce::MemoryBlock oldState;
                juce::AudioProcessor::copyXmlToBinary (*xml, oldState);
                RoomAudioProcessor q;
                q.setPresetRoot (root);
                q.setStateInformation (oldState.getData(), (int) oldState.getSize());
                check (std::abs (paramValue (q, ParamIDs::toneAmount) - 0.8f) < 0.011f && std::abs (paramValue (q, ParamIDs::spaceAmount) - 0.2f) < 0.011f,
                       "an old project's balance 0.2 loads as tone 80 / space 20");
            }
            {
                room::Modulation m;
                juce::ValueTree t ("MODULATION"), e ("MOD");
                e.setProperty ("lfo", 0, nullptr);
                e.setProperty ("target", "balance", nullptr);
                e.setProperty ("depth", 0.3f, nullptr);
                t.appendChild (e, nullptr);
                m.fromValueTree (t);
                check (m.isAssigned (0, room::Modulation::targetIndex ("spaceAmount")), "an lfo on the old balance now moves space");
            }
            check (p.apvts.getParameter ("balance") == nullptr && p.apvts.getParameter ("eq1Gain") == nullptr,
                   "balance and the eq are gone from the parameters");

            check (findChild<NoiseScreen> (ed) != nullptr, "the noise screen is in the bottom module");
            check (findChild<SyncDot> (ed) != nullptr, "the glitch has its sync button");
            auto* noiseType = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter (ParamIDs::noiseType));
            check (noiseType != nullptr && noiseType->choices.size() == room::NoiseBank::names().size(), "the noise list matches the profiles");
            if (auto* scr = findChild<NoiseScreen> (ed); scr != nullptr && noiseType != nullptr)
            {
                scr->step (1);
                check (noiseType->getIndex() == 1, "the arrows step through the noise profiles");
                scr->step (-2);
                check (noiseType->getIndex() == noiseType->choices.size() - 1, "and wrap around");
            }
            auto* adv = p.apvts.getParameter (ParamIDs::space);
            check (adv->getName (32) == "Width", "advanced space is called width");
        }

        // --- Quick slots: your own packs come before the factory rooms on the stage ---
        {
            RoomAudioProcessor p;
            p.setPresetRoot (root);
            auto& rooms = p.getRoomLibrary();
            const auto factoryKey = rooms.getEntries().front().key;
            const auto tree = rooms.loadPreset (*rooms.find (factoryKey));
            rooms.getRoot().getChildFile ("Eville").createDirectory();
            room::presets::save (tree, rooms.getRoot().getChildFile ("Eville").getChildFile ("Stairwell" + juce::String (room::presets::extensionFor (Kind::room))));
            rooms.rescan();
            const auto* mine = rooms.find ("Eville/Stairwell");
            check (mine != nullptr, "a room in a user pack (Eville) is in the library");
            for (int s = 0; s < room::presets::PresetLibrary::numQuickSlots; ++s)
                rooms.setQuickSlot (s, {});
            RoomAudioProcessorEditor ed (p);
            if (auto* stage = findChild<LoadScreen> (ed))
            {
                stage->refreshCards();
                check (stage->cardKeys().contains ("Eville/Stairwell"), "user-pack rooms show on the stage before factory ones");
                rooms.setQuickSlot (3, "Eville/Stairwell");
                stage->refreshCards();
                check (stage->cardKeys()[0] == "Eville/Stairwell", "a user-pack room in a quick slot is on the stage");
                rooms.setQuickSlot (3, {});
            }
            auto* strip = findChild<room::presets::QuickSlotStrip> (ed);
            check (strip != nullptr, "the rooms browser has the six quick-select slots");
            if (strip != nullptr)
            {
                strip->setBounds (0, 0, 600, 66);
                juce::MouseEvent click (juce::Desktop::getInstance().getMainMouseSource(), strip->slotBounds (1).getCentre(),
                                        {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, strip, strip, juce::Time::getCurrentTime(),
                                        strip->slotBounds (1).getCentre(), juce::Time::getCurrentTime(), 1, false);
                strip->mouseUp (click);
                check (strip->getArmed() == 1, "clicking an empty slot waits for a room");
                strip->fillArmed ("Eville/Stairwell");
                check (rooms.getQuickSlots()[1] == "Eville/Stairwell" && strip->getArmed() < 0, "the next room clicked fills it");
                rooms.setQuickSlot (1, {});
            }
            rooms.getRoot().getChildFile ("Eville").deleteRecursively();
            rooms.rescan();
        }
    }

    void testRound4 (const juce::File& root)
    {
        std::cout << "extreme bloom / smooth / rate keys\n";
        auto hfRatio = [] (const std::vector<float>& x, size_t a, size_t b)
        {
            // energy of the first difference over energy: high when there's lots of top end
            double e = 1e-20, d = 0.0;
            for (auto i = std::max<size_t> (a, 1); i < std::min (b, x.size()); ++i)
            {
                e += (double) x[i] * x[i];
                d += (double) (x[i] - x[i - 1]) * (x[i] - x[i - 1]);
            }
            return d / e;
        };

        // --- Extreme (180..200 %): 187 % and below don't use the bloom; at 200 % no gating, harmonic, bounded ---
        {
            auto render = [] (float e, bool withNotes, std::vector<float>& out)
            {
                room::WetFx fx;
                fx.prepare (48000.0, 2);
                if (withNotes) { const float hz[] = { 196.0f, 247.0f }; fx.setHarmonics (hz, 2); }
                fx.setTargets (e, false);
                juce::Random r (4);
                out.clear();
                for (int i = 0; i < 48000 * 3; ++i)
                {
                    const auto in = (r.nextFloat() - 0.5f) * 0.4f * (0.6f + 0.4f * std::sin ((float) i * 0.0004f));
                    float w[2] = { in, in };
                    fx.processSample (w, 2);
                    out.push_back (w[0]);
                }
            };
            std::vector<float> a, b;
            render (0.35f, false, a);
            render (0.35f, true, b);
            check (a == b, "at 187 % the bloom isn't used (the room's notes change nothing)");
            render (1.0f, true, b);
            bool finite = true;
            float peak = 0.0f;
            for (auto x : b) { finite = finite && std::isfinite (x); peak = std::max (peak, std::abs (x)); }
            check (finite && peak <= 1.0f, "at 200 % the bloom stays finite and bounded");
            // no gate: every 20 ms window keeps a good share of the median level
            std::vector<double> win;
            for (size_t k = 48000; k + 960 < b.size(); k += 960)
            {
                double e = 0; for (size_t i = k; i < k + 960; ++i) e += (double) b[i] * b[i];
                win.push_back (std::sqrt (e / 960.0));
            }
            auto sorted = win; std::sort (sorted.begin(), sorted.end());
            const auto median = sorted[sorted.size() / 2], lowest = sorted.front();
            check (lowest > 0.25 * median, "at 200 % nothing gates the sound out (quietest 20 ms at "
                                           + juce::String (lowest / median * 100.0, 0) + " % of typical)");
            check (hfRatio (b, 48000, b.size()) < hfRatio (a, 48000, a.size()), "at 200 % the sound is more tonal than at 187 % (less hiss, more notes)");
        }

        // --- Smooth: off is untouched; on tucks the attack, keeps the body, darkens the top ---
        {
            room::RoomMic mic;
            mic.prepare (48000.0);
            check (mic.isIdle(), "smooth at 0 does nothing (idle)");
            auto run = [&mic] (float amount, const std::vector<float>& in)
            {
                mic.reset();
                mic.setAmount (amount);
                juce::AudioBuffer<float> b (2, (int) in.size());
                for (int i = 0; i < (int) in.size(); ++i) b.setSample (0, i, in[(size_t) i]), b.setSample (1, i, in[(size_t) i]);
                for (int i = 0; i < 4800; ++i) { juce::AudioBuffer<float> z (2, 1); z.clear(); mic.process (z, 1); }   // settle the amount
                mic.process (b, b.getNumSamples());
                std::vector<float> out ((size_t) b.getNumSamples());
                for (int i = 0; i < b.getNumSamples(); ++i) out[(size_t) i] = b.getSample (0, i);
                return out;
            };
            // a hit then a steady tone
            std::vector<float> hit (48000, 0.0f);
            for (int i = 0; i < 48000; ++i)
                hit[(size_t) i] = (i < 200 ? 0.9f * std::exp (-i / 40.0f) * (i % 2 ? 1.0f : -1.0f) : 0.0f)
                                + (i > 12000 ? 0.3f * std::sin (2.0f * 3.14159f * 220.0f * (float) i / 48000.0f) : 0.0f);
            const auto soft = run (1.0f, hit), plain = run (0.0f, hit);
            float hitPlain = 0, hitSoft = 0;
            for (int i = 0; i < 600; ++i) { hitPlain = std::max (hitPlain, std::abs (plain[(size_t) i])); hitSoft = std::max (hitSoft, std::abs (soft[(size_t) i])); }
            const auto hitDb = juce::Decibels::gainToDecibels (hitSoft / hitPlain);
            const auto bodyDb = rmsDb (soft, 0.5, 0.99) - rmsDb (plain, 0.5, 0.99);
            check (hitDb < -6.0f, "smooth tucks the attack (" + juce::String (hitDb, 1) + " dB on the hit)");
            check (std::abs (bodyDb) < 2.0, "and keeps the body of the note (" + juce::String (bodyDb, 2) + " dB)");
            std::vector<float> noise (48000);
            juce::Random r (2);
            for (auto& x : noise) x = (r.nextFloat() - 0.5f) * 0.3f;
            check (hfRatio (run (1.0f, noise), 4800, 48000) < 0.5 * hfRatio (run (0.0f, noise), 4800, 48000), "smooth rolls off the top");
        }

        // --- Rate stays clean near the top: nothing added below the cutoff (no crunch) ---
        for (auto hz : { 43300.0f, 30000.0f, 20800.0f })
        {
            room::RateReducer rr;
            rr.prepare (48000.0);
            juce::Random r (6);
            std::vector<float> in, out;
            for (int i = 0; i < 48000 * 2; ++i)
            {
                float x[2] = { (r.nextFloat() - 0.5f) * 0.5f, 0.0f };
                in.push_back (x[0]);
                rr.processSample (x, 2, hz);
                out.push_back (x[0]);
            }
            // What rate takes away, kept below 0.6 x the cutoff: a clean low-pass leaves almost
            // nothing there (only its phase shift); any crunch would show up here.
            juce::dsp::IIR::Filter<float> lp1, lp2, lpIn1, lpIn2;
            const auto band = std::min (0.6f * 0.45f * hz, 9000.0f);
            lp1.coefficients = lp2.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (48000.0, band);
            lpIn1.coefficients = lpIn2.coefficients = juce::dsp::IIR::Coefficients<float>::makeLowPass (48000.0, band);
            double inE = 1e-20, outE = 1e-20;
            for (size_t i = 0; i < in.size(); ++i)
            {
                const auto a = lpIn2.processSample (lpIn1.processSample (in[i]));
                const auto b = lp2.processSample (lp1.processSample (out[i]));
                if (i > 24000) { inE += (double) a * a; outE += (double) b * b; }
            }
            const auto lossDb = 10.0 * std::log10 (outE / inE);
            check (std::abs (lossDb) < 0.5, "rate at " + juce::String (hz / 1000.0f, 1) + " kHz keeps the band below it untouched ("
                                               + juce::String (lossDb, 2) + " dB), no crunch");
        }

        // --- A known newer version is announced by itself; "later" holds it off a day ---
        if (auto settings = UpdateChecker::openSettings())
        {
            const auto oldLatest = settings->getValue ("latestVersion");
            const auto oldSnooze = settings->getValue ("updateSnoozed"), oldAt = settings->getValue ("updateSnoozedAt");
            settings->setValue ("latestVersion", "99.0.0");
            settings->removeValue ("updateSnoozed");
            settings->removeValue ("updateSnoozedAt");
            settings->saveIfNeeded();
            {
                RoomAudioProcessor p;
                p.setPresetRoot (root);
                RoomAudioProcessorEditor ed (p);
                check (ed.wouldAnnounceUpdate(), "a newer version is announced without asking");
                ed.snoozeUpdate();
                check (! ed.wouldAnnounceUpdate(), "and only once");
            }
            {
                RoomAudioProcessor p;
                p.setPresetRoot (root);
                RoomAudioProcessorEditor ed (p);
                check (! ed.wouldAnnounceUpdate(), "after later, that version waits a day");
            }
            if (auto s2 = UpdateChecker::openSettings()) { s2->setValue ("latestVersion", "99.1.0"); s2->saveIfNeeded(); }
            {
                RoomAudioProcessor p;
                p.setPresetRoot (root);
                RoomAudioProcessorEditor ed (p);
                check (ed.wouldAnnounceUpdate(), "an even newer version is announced straight away");
            }
            if (auto s3 = UpdateChecker::openSettings())
            {
                s3->setValue ("latestVersion", oldLatest);
                s3->setValue ("updateSnoozed", oldSnooze);
                s3->setValue ("updateSnoozedAt", oldAt);
                s3->saveIfNeeded();
            }
        }

        // --- Size reads 0..100 %, 50 % = the room as measured (1x) ---
        {
            RoomAudioProcessor pp;
            pp.setPresetRoot (root);
            auto* sz = pp.apvts.getParameter (ParamIDs::size);
            check (sz->getText (sz->getDefaultValue(), 16) == "50%" && sz->getText (0.0f, 16) == "0%" && sz->getText (1.0f, 16) == "100%",
                   "size shows 0 .. 100 % with 50 % in the middle");
            check (std::abs (sz->convertFrom0to1 (0.0f) - 0.5f) < 1e-4f && std::abs (sz->convertFrom0to1 (0.5f) - 1.0f) < 1e-4f
                       && std::abs (sz->convertFrom0to1 (1.0f) - 2.0f) < 1e-4f,
                   "size 0 / 50 / 100 % is the room at 0.5x / 1x / 2x (same sound as before)");
        }

        // --- Rate keys: D / R / N choose what Rate reaches ---
        {
            Rig rig (root);
            setParam (rig.proc, ParamIDs::mix, 0.0f);   // just the dry sound
            setParam (rig.proc, ParamIDs::rate, 2000.0f);
            std::vector<float> in ((size_t) (0.5 * Rig::sr));
            juce::Random r (8);
            for (auto& x : in) x = (r.nextFloat() - 0.5f) * 0.3f;
            setParam (rig.proc, ParamIDs::rateDry, 0.0f);
            rig.proc.reset();
            const auto clean = rig.run (in, 0.1);
            setParam (rig.proc, ParamIDs::rateDry, 1.0f);
            rig.proc.reset();
            const auto crushed = rig.run (in, 0.1);
            check (hfRatio (clean, 4800, in.size()) > 4.0 * hfRatio (crushed, 4800, in.size()),
                   "D off: rate leaves the dry sound alone; D on: it reaches it");

            // N: only the noise, at follow 0
            setParam (rig.proc, ParamIDs::rateDry, 0.0f);
            setParam (rig.proc, ParamIDs::rateReverb, 0.0f);
            setParam (rig.proc, ParamIDs::noiseType, 1.0f);   // tape hiss: bright
            setParam (rig.proc, ParamIDs::noiseLevel, 0.8f);
            setParam (rig.proc, ParamIDs::noiseStop, 0.0f);
            std::vector<float> silence ((size_t) (0.6 * Rig::sr), 0.0f);
            setParam (rig.proc, ParamIDs::rateNoise, 1.0f);
            rig.proc.reset();
            const auto noiseCrushed = rig.run (silence, 0.1);
            setParam (rig.proc, ParamIDs::rateNoise, 0.0f);
            rig.proc.reset();
            const auto noiseClean = rig.run (silence, 0.1);
            check (hfRatio (noiseClean, 9600, silence.size()) > 4.0 * hfRatio (noiseCrushed, 9600, silence.size()),
                   "N on: rate reaches the noise; N off: it doesn't");
            for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> { { ParamIDs::noiseLevel, 0.0f }, { ParamIDs::rate, 44100.0f },
                     { ParamIDs::rateDry, 1.0f }, { ParamIDs::rateReverb, 1.0f }, { ParamIDs::rateNoise, 1.0f }, { ParamIDs::mix, 1.0f } })
                setParam (rig.proc, id, v);
        }
    }

    // Round 6: Intensity's room artifacts (100..180 %), Tune to key, Imprint, pack covers /
    // delete, randomizing the room and the menu geometry.
    void testRound6 (const juce::File& root)
    {
        std::cout << "round 6/7: the room falling apart / pitch / mud / imprint / packs / menus\n";
        constexpr double sr = 48000.0;
        auto noiseOf = [] (int n, int seed, float level = 0.25f)
        {
            std::vector<float> v ((size_t) n);
            juce::Random r (seed);
            for (auto& x : v) x = (r.nextFloat() * 2.0f - 1.0f) * level;
            return v;
        };
        auto rmsOf = [] (const std::vector<float>& x, size_t a, size_t b)
        {
            double e = 0.0;
            for (auto i = a; i < std::min (b, x.size()); ++i) e += (double) x[i] * x[i];
            return 10.0 * std::log10 (e / (double) std::max<size_t> (1, std::min (b, x.size()) - a) + 1e-20);
        };

        // --- RoomCharacter: idle at 0; flutter, comb and push are each there; level kept ---
        {
            auto runChar = [&] (float amount, const std::vector<float>& in)
            {
                room::RoomCharacter c;
                c.prepare (sr, 2);
                c.setRoom (25.0f, 1.6f);
                c.setAmount (amount);
                std::vector<float> out;
                for (auto x : in)
                {
                    float w[2] = { x, x };
                    c.processSample (w, 2);
                    out.push_back (w[0]);
                }
                return std::make_pair (out, c.isIdle());
            };
            const auto noise = noiseOf ((int) sr * 2, 21);
            const auto [same, idle] = runChar (0.0f, noise);
            check (same == noise && idle, "room artifacts are off up to 100 % (untouched)");

            // an impulse train, one click every 250 ms: flutter repeats every 25 ms after each click
            std::vector<float> clicks ((size_t) sr * 2, 0.0f);
            for (size_t k = 0; k < clicks.size(); k += (size_t) (0.25 * sr)) clicks[k] = 0.8f;
            const auto at160 = runChar (0.75f, clicks).first;
            const auto at100 = runChar (0.0f, clicks).first;
            const auto click = (size_t) (1.5 * sr), period = (size_t) (0.025 * sr);
            auto around = [] (const std::vector<float>& x, size_t i) { float m = 0; for (size_t k = i - 4; k <= i + 4; ++k) m = std::max (m, std::abs (x[k])); return m; };
            check (around (at160, click + period) > 0.05f && around (at100, click + period) == 0.0f,
                   "flutter echo: the room repeats at its own period (" + juce::String (around (at160, click + period), 3) + ")");
            check (around (at160, click + period + (size_t) (0.025 * 1.071 * sr)) > 0.01f, "flutter echo keeps bouncing (left to right and back)");
            float near = 0;
            for (auto k = click + (size_t) (0.0008 * sr); k < click + (size_t) (0.0024 * sr); ++k) near = std::max (near, std::abs (at160[k]));
            check (near > 0.1f, "close-wall comb: a near reflection right after the sound");

            // push: a bursty sound gets squeezed (crest factor down)
            std::vector<float> bursts ((size_t) sr * 3);
            juce::Random r (5);
            for (size_t i = 0; i < bursts.size(); ++i)
                bursts[i] = (r.nextFloat() * 2.0f - 1.0f) * ((i / 2400) % 4 == 0 ? 0.6f : 0.03f);
            auto crest = [] (const std::vector<float>& x)
            {
                double e = 0; float pk = 0;
                for (size_t i = x.size() / 3; i < x.size(); ++i) { e += (double) x[i] * x[i]; pk = std::max (pk, std::abs (x[i])); }
                return pk / std::sqrt (e / (double) (x.size() - x.size() / 3));
            };
            const auto pushed = runChar (1.0f, bursts).first;
            check (crest (pushed) < 0.8 * crest (bursts), "pushed mic squeezes the room (crest " + juce::String (crest (bursts), 1)
                                                            + " -> " + juce::String (crest (pushed), 1) + ")");
            const auto full = runChar (1.0f, noise).first;
            bool finite = true;
            for (auto x : full) finite = finite && std::isfinite (x);
            const auto dIn = rmsOf (noise, (size_t) sr, noise.size()), dOut = rmsOf (full, (size_t) sr, full.size());
            check (finite && dOut - dIn > -3.0 && dOut - dIn < 5.0, "the falling-apart room stays near the room's level (" + juce::String (dOut - dIn, 1) + " dB)");
            check (room::RoomCharacter::ringAmount (0.0f) == 0.0f && room::RoomCharacter::ringAmount (0.4f) == 1.0f,
                   "the room's notes ring from 100 % and are full at 130 %");
        }

        // --- The whole plugin at 100 / 140 / 180 %: different, finite, near the room's level ---
        {
            Rig rig (root);
            setParam (rig.proc, ParamIDs::toneAmount, 0.6f);
            const auto in = noiseOf ((int) (0.6 * sr), 3);
            auto at = [&] (float v) { setParam (rig.proc, ParamIDs::intensity, v); rig.settle(); return rig.run (in, 1.5); };
            const auto a = at (2.0f), b = at (2.5f), c = at (3.5f);
            float d1 = 0, d2 = 0; bool finite = true;
            for (size_t i = 0; i < a.size(); ++i)
            {
                d1 = std::max (d1, std::abs (b[i] - a[i])); d2 = std::max (d2, std::abs (c[i] - b[i]));
                finite = finite && std::isfinite (b[i]) && std::isfinite (c[i]);
            }
            check (finite && d1 > 0.01f && d2 > 0.01f, "125 % and 175 % each change the room");
            // clearly audible: the difference from 100 % is within 6 dB of the room itself at 125 %
            std::vector<float> diff (a.size());
            for (size_t i = 0; i < a.size(); ++i) diff[i] = b[i] - a[i];
            const auto l100 = rmsDb (a, 0.0, 1.5), lDiff = rmsDb (diff, 0.0, 1.5), l175 = rmsDb (c, 0.0, 1.5);
            check (lDiff > l100 - 6.0, "at 125 % the room has clearly changed (difference " + juce::String (lDiff - l100, 1) + " dB of the room)");
            float peak = 0; for (auto x : c) peak = std::max (peak, std::abs (x));
            check (peak < 4.0f, "175 % stays bounded (peak " + juce::String (peak, 2) + ")");
            check (std::abs (l175 - l100) < 8.0, "175 % stays near the room's level (" + juce::String (l175 - l100, 1) + " dB)");
        }

        // --- Pitch: dry in the middle, an octave either side; room only ---
        {
            auto run = [&] (float amount)
            {
                room::OctaveShift o;
                o.prepare (sr, 2);
                o.setAmount (amount);
                std::vector<float> out;
                for (int i = 0; i < (int) sr; ++i)
                {
                    const auto x = 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / sr);
                    float w[2] = { x, x };
                    o.processSample (w, 2);
                    out.push_back (w[0]);
                }
                return out;
            };
            const auto dry = run (0.0f);
            bool same = true;
            for (int i = 0; i < (int) sr; ++i)
                same = same && dry[(size_t) i] == 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / sr);
            check (same, "pitch in the middle leaves the room untouched");
            auto energyAt = [&] (const std::vector<float>& x, double hz)
            {
                double re = 0, im = 0;
                for (size_t i = x.size() / 2; i < x.size(); ++i)
                {
                    re += x[i] * std::cos (juce::MathConstants<double>::twoPi * hz * (double) i / sr);
                    im += x[i] * std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sr);
                }
                return re * re + im * im;
            };
            const auto up = run (1.0f), down = run (-1.0f);
            check (energyAt (up, 440.0) > 4.0 * energyAt (up, 220.0), "pitch right: the room an octave up");
            check (energyAt (down, 110.0) > 4.0 * energyAt (down, 220.0), "pitch left: the room an octave down");
            check (room::Modulation::targetIndex (ParamIDs::pitch) >= 0, "an LFO can move pitch");

            // the dry sound stays in tune (room only): with no room mixed in, pitch changes nothing
            Rig rig (root);
            setParam (rig.proc, ParamIDs::mix, 0.0f);
            rig.settle();
            const auto in = noiseOf ((int) (0.3 * sr), 6);
            const auto plain = rig.run (in, 0.3);
            setParam (rig.proc, ParamIDs::pitch, 1.0f);
            rig.settle();
            const auto shifted = rig.run (in, 0.3);
            float worst = 0;
            for (size_t i = 0; i < plain.size(); ++i) worst = std::max (worst, std::abs (shifted[i] - plain[i]));
            check (worst < 1e-5f, "pitch leaves the dry sound alone");
        }

        // --- Mud: off is untouched; on cuts 350 Hz ---
        {
            auto run = [&] (float amount, double hz)
            {
                room::MudCut m;
                m.prepare (sr);
                m.setAmount (amount);
                juce::AudioBuffer<float> buf (2, 512);
                double e = 0.0, eIn = 0.0;
                for (int block = 0; block < 100; ++block)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        const auto x = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (block * 512 + i) / sr);
                        buf.setSample (0, i, x); buf.setSample (1, i, x);
                        if (block > 50) eIn += (double) x * x;
                    }
                    m.process (buf, 512);
                    if (block > 50) for (int i = 0; i < 512; ++i) e += (double) buf.getSample (0, i) * buf.getSample (0, i);
                }
                return 10.0 * std::log10 (e / eIn);
            };
            check (run (0.0f, 350.0) == 0.0, "mud at 0 leaves the sound untouched");
            const auto cut = run (1.0f, 350.0), far = run (1.0f, 3000.0);
            check (cut < -5.0 && std::abs (far) < 1.0, "mud cuts the boxy 350 Hz (" + juce::String (cut, 1) + " dB) and leaves the top alone ("
                                                     + juce::String (far, 1) + " dB)");
        }

        // --- Imprint ---
        {
            room::Imprint im;
            im.prepare (sr, 2);
            room::Imprint::Profile prof;
            prof.valid = true;
            for (int b = 0; b < room::Imprint::numBands; ++b) prof.bandDb[b] = -12.0f + 24.0f * (float) b / 8.0f;   // a bright room
            im.setProfile (prof);
            auto run = [&] (float amount, const std::vector<float>& in)
            {
                im.reset();
                im.setAmount (amount);
                juce::AudioBuffer<float> buf (2, 256);
                std::vector<float> out;
                for (size_t start = 0; start + 256 <= in.size(); start += 256)
                {
                    for (int i = 0; i < 256; ++i) buf.setSample (0, i, in[start + (size_t) i]), buf.setSample (1, i, in[start + (size_t) i]);
                    im.process (buf, 256);
                    for (int i = 0; i < 256; ++i) out.push_back (buf.getSample (0, i));
                }
                return out;
            };
            // pink-ish (dark) noise in
            auto in = noiseOf ((int) sr * 3, 12, 0.3f);
            float lp = 0; for (auto& x : in) { lp += (x - lp) * 0.05f; x = lp * 3.0f; }
            const auto none = run (0.0f, in);
            bool same = true;
            for (size_t i = 0; i < none.size(); ++i) same = same && none[i] == in[i];
            check (same && im.isIdle(), "imprint at 0 leaves the sound untouched");
            const auto full = run (1.0f, in);
            auto hfShare = [] (const std::vector<float>& x)
            {
                double e = 1e-20, d = 0.0;
                for (size_t i = x.size() / 2; i < x.size(); ++i) { e += (double) x[i] * x[i]; d += (double) (x[i] - x[i - 1]) * (x[i] - x[i - 1]); }
                return d / e;
            };
            check (hfShare (full) > 2.0 * hfShare (none), "imprint gives a dark sound a bright room's timbre ("
                                                          + juce::String (hfShare (none), 4) + " -> " + juce::String (hfShare (full), 4) + ")");
            const auto l0 = rmsOf (none, none.size() / 2, none.size()), l1 = rmsOf (full, full.size() / 2, full.size());
            check (std::abs (l1 - l0) < 3.0, "imprint keeps the level (" + juce::String (l1 - l0, 1) + " dB)");

            Rig rig (root);
            const auto latency = rig.proc.getLatencySamples();
            setParam (rig.proc, ParamIDs::imprint, 1.0f);
            rig.settle();
            check (rig.proc.getLatencySamples() == latency, "imprint adds no latency");
            const auto out = rig.run (noiseOf ((int) (0.5 * sr), 4), 0.5);
            bool finite = true;
            for (auto x : out) finite = finite && std::isfinite (x);
            check (finite, "imprint on a real room stays finite");
            check (room::Modulation::targetIndex (ParamIDs::imprint) >= 0, "an LFO can move Imprint");
        }

        // --- Randomize the room ---
        {
            Rig rig (root);
            namespace R = room::Randomizer;
            for (const auto& t : R::targets())
                R::setSelected (rig.proc, t.key, juce::String (t.key) == "room");
            juce::Random r (17);
            const auto before = analysisXml (rig.proc);
            const auto keyBefore = rig.proc.getCurrentRoomKey();
            R::randomizeSelected (rig.proc, r);
            check (analysisXml (rig.proc) != before && rig.proc.getCurrentRoomKey() != keyBefore,
                   "randomize can pick a new room (" + rig.proc.getCurrentRoomKey() + ")");
        }

        // --- Pack cover and delete ---
        {
            Rig rig (root);
            auto& lib = rig.proc.getLibrary();
            juce::String err;
            check (lib.createPack ("Cover Test", err), "make a pack for the cover test");
            juce::Image img (juce::Image::RGB, 1600, 900, true);
            juce::Graphics (img).fillAll (juce::Colours::orange);
            const auto src = root.getChildFile ("cover-src.png");
            { juce::FileOutputStream os (src); os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
            check (lib.setPackCover ("Cover Test", src, err) && lib.packCoverFile ("Cover Test").existsAsFile(), "a pack takes a cover image");
            const auto cover = juce::ImageFileFormat::loadFrom (lib.packCoverFile ("Cover Test"));
            check (cover.isValid() && cover.getWidth() <= 1200, "the cover is kept at most 1200 px wide");
            lib.removePackCover ("Cover Test");
            check (! lib.packCoverFile ("Cover Test").existsAsFile(), "a cover can be removed");
            check (! lib.removePack ("Factory", err) && ! lib.removePack ("User", err), "Factory and User packs can't be deleted");
            check (lib.removePack ("Cover Test", err), "a user pack can be deleted");
            lib.rescan();
            bool gone = true;
            for (const auto& e : lib.getEntries()) gone = gone && e.pack != "Cover Test";
            check (gone && ! lib.getPacks().contains ("Cover Test"), "a deleted pack is gone from the library");
        }

        // --- Menus: every highlight inside the panel; long menus break at headers ---
        {
            juce::PopupMenu m;
            for (int sct = 0; sct < 6; ++sct)
            {
                m.addSectionHeader ("SECTION " + juce::String (sct));
                for (int i = 0; i < 10; ++i)
                    m.addItem ("item " + juce::String (sct) + "." + juce::String (i) + " with a long-ish name", [] {});
                m.addSeparator();
            }
            juce::PopupMenu sub; sub.addItem ("inner", [] {});
            m.addSubMenu ("more", sub);
            auto menu = RoomMenu::createForTest (m);
            const auto panel = menu->panelBounds();
            bool inside = true;
            for (const auto& e : menu->getEntries())
                if (e.kind == RoomMenu::Entry::Kind::item)
                    inside = inside && panel.reduced (1.0f).contains (menu->highlightFor (e));
            check (inside, "every menu highlight sits inside the menu's panel");
            check (menu->numColumns() >= 1, "the long menu is laid out");
            bool headersFirst = true;
            float lastX = -1.0f;
            for (const auto& e : menu->getEntries())
            {
                if (e.kind == RoomMenu::Entry::Kind::separator) continue;
                if (e.row.getX() > lastX + 1.0f && lastX >= 0.0f)
                    headersFirst = headersFirst && e.kind == RoomMenu::Entry::Kind::header;
                lastX = std::max (lastX, e.row.getX());
            }
            check (headersFirst, "menu columns start at a section header");
        }
    }

    void testStage (const juce::File& root, const juce::File& roomSource)
    {
        std::cout << "Stage\n";
        RoomAudioProcessor p;
        p.setPresetRoot (root);
        check (p.apvts.getRawParameterValue (ParamIDs::balanceAuto)->load() < 0.5f, "balance auto is off by default");
        p.loadRoomFile (roomSource);
        waitForAnalysis (p);
        RoomAudioProcessorEditor ed (p);
        auto* stage = findChild<LoadScreen> (ed);
        check (stage != nullptr, "the stage is there");
        if (stage == nullptr)
            return;
        // A drop anywhere on the stage, the room box included, reaches the stage.
        juce::Component* hit = stage->getComponentAt (stage->boxArea().getCentre());
        bool reaches = false;
        for (auto* c = hit; c != nullptr; c = c->getParentComponent())
            if (dynamic_cast<juce::FileDragAndDropTarget*> (c) != nullptr) { reaches = c == stage; break; }
        check (hit != stage && reaches, "a file dropped on the room box goes to the stage");
        check (stage->isInterestedInFileDrag ({ roomSource.getFullPathName() }), "the stage takes audio files");

        // Clear empties the room.
        IconButton* clear = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c)
        {
            if (auto* b = dynamic_cast<IconButton*> (&c); b != nullptr && b->getTooltip().startsWith ("clear the room"))
                clear = b;
            for (auto* child : c.getChildren())
                find (*child);
        };
        find (ed);
        check (clear != nullptr && clear->getParentComponent() == stage, "clear sits on the stage");
        check (p.getAnalysis().valid, "a room is loaded before clear");
        if (clear != nullptr && clear->onClick)
            clear->onClick();
        for (int i = 0; i < 100 && p.getAnalysis().valid; ++i)
            juce::Thread::sleep (20);
        check (! p.getAnalysis().valid, "clear empties the room");
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
            module.setBounds (0, 0, 420, 300);
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

        // Randomize: each ticked item only moves its own parameters.
        auto snapshot = [&p]
        {
            std::map<juce::String, float> m;
            for (auto* prm : p.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                    m[rp->paramID] = rp->getValue();
            return m;
        };
        namespace R = room::Randomizer;
        auto tickOnly = [&p] (const char* key)
        {
            for (const auto& t : R::targets())
                R::setSelected (p, t.key, juce::String (t.key) == key);
        };
        juce::Random r (5);
        R::setIntensity (p, 1.0);
        bool allOwn = true, allMove = true;
        for (const auto& t : R::targets())
        {
            if (t.params.empty())
                continue;   // the room: it changes the room, not a parameter (tested on its own)
            tickOnly (t.key);
            bool moved = false;
            for (int k = 0; k < 8; ++k)
            {
                const auto before = snapshot();
                R::randomizeSelected (p, r);
                for (auto& [id, v] : snapshot())
                    if (std::abs (v - before.at (id)) > 1e-6f)
                    {
                        moved = true;
                        if (std::find (t.params.begin(), t.params.end(), id) == t.params.end())
                        {
                            allOwn = false;
                            std::cout << "    " << t.key << " moved " << id << "\n";
                        }
                    }
            }
            if (! moved) { allMove = false; std::cout << "    " << t.key << " never moved\n"; }
        }
        check (allOwn, "every randomize item only moves its own parameters");
        check (allMove, "every randomize item moves something");
        tickOnly ("");
        const auto before = snapshot();
        R::randomizeSelected (p, r);
        check (snapshot() == before, "Randomize with nothing ticked does nothing");
        RoomAudioProcessor fresh;
        check (! R::isSelected (fresh, "lfo1") && ! R::isSelected (fresh, "glitchOn") && ! R::isSelected (fresh, "rate")
                   && R::isSelected (fresh, "intensity") && std::abs (R::getIntensity (fresh) - 0.5) < 1e-9,
               "by default Randomize leaves the LFOs, switches and rate alone (moderate)");
        // Basic stays narrower than extreme
        {
            auto* tail = p.apvts.getParameter (ParamIDs::tail);
            float basicLo = 99.0f, basicHi = 0.0f, exLo = 99.0f, exHi = 0.0f;
            for (int k = 0; k < 400; ++k)
            {
                const auto b = R::randomFor (*tail, ParamIDs::tail, 0.0, r), e = R::randomFor (*tail, ParamIDs::tail, 1.0, r);
                basicLo = std::min (basicLo, b); basicHi = std::max (basicHi, b);
                exLo = std::min (exLo, e); exHi = std::max (exHi, e);
            }
            check (basicHi - basicLo < 0.5f * (exHi - exLo) && exHi > 5.0f && exLo < 0.5f, "basic stays musical, extreme uses the whole knob");
        }
    }

    //==========================================================================

    int screenshot (const juce::StringArray& args)
    {
        // OUT.png MODE ROOM_SOURCE TARGET
        // lic-signin / lic-error: the sign-in page (no license on this computer).
        if (args[2].startsWith ("lic-"))
            RoomAudioProcessor::enforceLicense = true;
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
        if (args[2] == "about")
            ed.openAbout();
        if (args[2] == "lic-error")
        {
            std::function<void (juce::Component&)> fillPage = [&] (juce::Component& c)
            {
                if (auto* page = dynamic_cast<LicensePage*> (&c))
                    page->fill ("ann@example.com", "ROOM-7KQ2M-9XD4T-PWHC3-F8JRN",
                                "That email and license key don't match. Use the email you got ROOM with, and copy the key from your ROOM email.", false);
                for (auto* child : c.getChildren())
                    fillPage (*child);
            };
            fillPage (ed);
        }
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
        bool filtersOpened = false;
        std::function<void (juce::Component&)> findAndShow = [&] (juce::Component& c)
        {
            for (auto* child : c.getChildren())
            {
                if (mode == "browser" || mode == "rooms")
                    if (auto* b = dynamic_cast<PresetBrowser*> (child))
                        if ((b->getKind() == Kind::room) == (mode == "rooms")) { b->setVisible (true); b->toFront (false); }
                if (mode == "save")
                    if (auto* bar = dynamic_cast<PresetBar*> (child)) if (bar->onSave) bar->onSave();
                if (mode == "filters")
                {
                    if (auto* b = dynamic_cast<PresetBrowser*> (child))
                        if (b->getKind() == Kind::studio) { b->setVisible (true); b->toFront (false); }
                    if (auto* tb = dynamic_cast<juce::TextButton*> (child))
                        if (tb->getButtonText().startsWith ("filters") && tb->onClick && ! filtersOpened
                            && dynamic_cast<PresetBrowser*> (tb->getParentComponent()) != nullptr
                            && dynamic_cast<PresetBrowser*> (tb->getParentComponent())->getKind() == Kind::studio)
                        {
                            filtersOpened = true;
                            tb->onClick();
                        }
                }
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
        if (mode == "lfo" || mode == "advlfo")
        {
            // a few LFO assignments, so the depth dots, arcs and chips show
            using M = room::Modulation;
            for (auto* id : { "size", "tape", "autoEq", "duck", "preDelay", "intensity" })
                p.modulation.assign (0, M::targetIndex (id), 0.25f);
            for (auto* id : { "tape", "mud", "damping" })
                p.modulation.assign (1, M::targetIndex (id), -0.4f);
        }
        if (mode == "lfo")
            ed.setAdvanced (true);
        if (mode == "advlfo")
            ed.setAdvancedOpen (true);
        if (mode == "advanced")
            ed.setAdvancedOpen (true);
        if (mode == "info")
            ed.showRoomInfo (true);
        if (mode == "about")
            ed.openAbout();
        if (mode == "randomize")
            if (auto* page = findChild<RandomizePage> (ed)) { page->setVisible (true); page->toFront (false); }
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
        if (mode == "menu" || mode == "mainmenu" || mode == "noisemenu")
        {
            // ROOM's menus (RoomMenu), drawn over the snapshot where they would open
            const auto scale = (float) img.getHeight() / (float) RoomAudioProcessorEditor::baseHeight;
            auto drawMenu = [&] (juce::Point<int> at, const juce::PopupMenu& m, int hover)
            {
                auto menu = RoomMenu::createForTest (m);
                menu->setHoverForTest (hover);
                juce::Image pic (juce::Image::ARGB, menu->getWidth(), menu->getHeight(), true);
                {
                    juce::Graphics mg (pic);
                    menu->paint (mg);
                }
                juce::Graphics g (img);
                g.addTransform (juce::AffineTransform::scale (scale));
                g.drawImageAt (pic, at.x, at.y);
            };
            if (mode == "mainmenu")
            {
                juce::PopupMenu m;
                m.addItem ("new preset", [] {});
                m.addItem ("reset all knobs", [] {});
                m.addSeparator();
                m.addItem ("tips", true, true, [] {});
                m.addItem ("show the tour", [] {});
                m.addSeparator();
                m.addItem ("check for updates", [] {});
                m.addItem ("visit website", [] {});
                m.addItem ("get support", [] {});
                m.addItem (juce::String (juce::CharPointer_UTF8 ("about room\xe2\x80\xa6")), [] {});
                m.addSeparator();
                m.addItem ("room v1.0.2", false, false, nullptr);
                drawMenu ({ 4, 60 }, m, 2);
            }
            else if (mode == "noisemenu")
            {
                juce::PopupMenu m;
                const auto& infos = room::NoiseBank::infos();
                juce::StringArray sections;
                for (const auto& info : infos)
                    sections.addIfNotAlreadyThere (info.section);
                for (const auto& section : sections)
                {
                    m.addSectionHeader (section);
                    for (size_t k = 0; k < infos.size(); ++k)
                        if (infos[k].section == section)
                            m.addItem (infos[k].name, true, k == 31, [] {});
                }
                drawMenu ({ 4, 150 }, m, 31);
            }
            else
            {
                juce::PopupMenu row;
                row.addSectionHeader ("basement show");
                row.addItem (juce::String (juce::CharPointer_UTF8 ("room  \xc2\xb7  medium  \xc2\xb7  neutral")), false, false, nullptr);
                row.addItem ("a packed basement: low, loud and wide.", false, false, nullptr);
                row.addSeparator();
                row.addItem ("load", [] {});
                row.addItem ("favourite", [] {});
                row.addItem ("add to quick select", [] {});
                juce::PopupMenu slots;
                slots.addItem ("slot 1", [] {});
                row.addSubMenu ("quick select slot", slots);
                drawMenu ({ 560, 90 }, row, 2);

                juce::PopupMenu depth;
                for (auto pct : { 10, 25, 50, 100 })
                    depth.addItem (juce::String (pct) + "%", true, pct == 25, [] {});
                depth.addSeparator();
                depth.addItem ("invert", [] {});
                depth.addItem ("remove from lfo 1", [] {});
                juce::PopupMenu mod;
                mod.addSectionHeader ("modulation");
                mod.addSubMenu (juce::String (juce::CharPointer_UTF8 ("lfo 1 \xc2\xb7 25%")), depth);
                mod.addItem ("move with lfo 2", [] {});
                drawMenu ({ 300, 470 }, mod, 0);
                drawMenu ({ 520, 470 }, depth, 1);
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

// Checks ROOM against the license server's own signing code (WARP's Server/src/index.js):
//   room_tests --license-interop DIR keys    writes a throwaway key pair + this computer's id
//   (node signs a ROOM license with the server's signLicense into DIR/license.json)
//   room_tests --license-interop DIR check   ROOM must accept it, and refuse it as WARP's
int licenseInterop (const juce::File& dir, const juce::String& step)
{
    if (step == "keys")
    {
        dir.createDirectory();
        dir.getChildFile ("private.txt").replaceWithText (testKeys().priv);
        dir.getChildFile ("public.txt").replaceWithText (testKeys().pub);
        dir.getChildFile ("machine.txt").replaceWithText (LicenseManager::machineId());
        return 0;
    }
    const auto pub = dir.getChildFile ("public.txt").loadFileAsString();
    auto license = LicenseCrypto::fromVar (juce::JSON::parse (dir.getChildFile ("license.json").loadFileAsString()));
    check (license.product == "room" && license.key.startsWith ("ROOM-"), "interop: the server made a ROOM license");
    check (LicenseManager::isValid (license, pub), "interop: ROOM accepts the server's signature");
    license.product = "warp";
    check (! LicenseManager::isValid (license, pub), "interop: the same license can't pass as WARP's");
    std::cout << (failures == 0 ? "LICENSE INTEROP PASSED" : "LICENSE INTEROP FAILED") << "\n";
    return failures == 0 ? 0 : 1;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::StringArray args;
    const bool startEmpty = ! (argc > 1 && juce::String (argv[1]) == "--first-run");
    for (int i = 1; i < argc; ++i)
        args.add (argv[i]);

    // Licensing: never touch this computer's real license (tests sign their own).
    LicenseManager::setFolderForTests (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                           .getChildFile ("room_tests_license_" + juce::String (juce::Random::getSystemRandom().nextInt (1 << 30))));
    RoomAudioProcessor::enforceLicense = false;   // the DSP tests run unlocked; testLicensing switches it on

    RoomAudioProcessor::startWithDefaultStudio = ! startEmpty;   // tests start from an empty ROOM
    RoomAudioProcessorEditor::autoShowTour = false;
    if (args[0] == "--first-run")
        args.remove (0);
    if (args[0] == "--screenshot" && args.size() >= 5)
        return screenshot (args);
    if (args[0] == "--bench" || args[0] == "--bench-rt")
        return bench (args);
    if (args[0] == "--license-interop" && args.size() > 2)
        return licenseInterop (juce::File (args[1]), args[2]);
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
    testLicensing (root);
    testStress();
    testFactoryPresets (root);
    testModulationUi (root);
    testStage (root, roomSource);
    testIntensityGlitchQuick (root, roomSource);
    testNoiseRateToneSync (root);
    testRound4 (root);
    testRound6 (root);

    root.getParentDirectory().deleteRecursively();
    std::cout << (failures == 0 ? "ALL ROOM TESTS PASSED" : "ROOM TESTS FAILED") << " (" << checks - failures << "/" << checks << ")\n";
    return failures == 0 ? 0 : 1;
}
