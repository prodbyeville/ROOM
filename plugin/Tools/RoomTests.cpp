// room_tests: ROOMS presets, Space, and state restore, tested against the real
// processor (no DAW needed).
//
//   room_tests                                           run all tests
//   room_tests --screenshot OUT.png MODE ROOM_SRC TARGET  save a UI snapshot
//        MODE = main | browser | save

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
                if (rp->paramID != ParamIDs::output && std::abs (now - want) > 1e-4f * std::max (1.0f, std::abs (want)))
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
                findAndShow (*child);
            }
        };
        findAndShow (ed);
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

    root.getParentDirectory().deleteRecursively();
    std::cout << (failures == 0 ? "ALL ROOM TESTS PASSED" : "ROOM TESTS FAILED") << " (" << checks - failures << "/" << checks << ")\n";
    return failures == 0 ? 0 : 1;
}
