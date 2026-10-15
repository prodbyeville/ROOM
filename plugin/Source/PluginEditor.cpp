#include "PluginEditor.h"

#include "UiColours.h"
#include "Screens.h"
#include "dsp/RoomSynth.h"

namespace
{
    /** A parameter as it is right now: the knob plus whatever the LFOs add. */
    struct Live { float norm = 0.0f, value = 0.0f; };

    Live liveParam (RoomAudioProcessor& p, const juce::String& id)
    {
        auto* rp = p.apvts.getParameter (id);
        if (rp == nullptr)
            return {};
        auto norm = rp->getValue();
        if (const auto t = room::Modulation::targetIndex (id); t >= 0)
            norm = juce::jlimit (0.0f, 1.0f, norm + p.modulation.liveOffset[(size_t) t].load());
        return { norm, rp->convertFrom0to1 (norm) };
    }

    float liveBalance (RoomAudioProcessor& p)
    {
        return p.apvts.getRawParameterValue (ParamIDs::balanceAuto)->load() > 0.5f ? p.getDetectedBalance()
                                                                                  : liveParam (p, ParamIDs::balance).value;
    }

    juce::String lower (const juce::String& s) { return s.toLowerCase(); }
}

//==============================================================================
// Load screen

LoadScreen::LoadScreen (RoomAudioProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("click for ROOMS, drop a sound here, right-click for more");
    const char* files[5] = { "room_hall.jpg", "room_booth.jpg", "room_chamber.jpg", "room_objects.jpg", "room_tape.jpg" };
    for (int i = 0; i < 5; ++i)
        photos[i] = Hw::asset (files[i]);
    startTimerHz (15);
}

bool LoadScreen::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (proc.formatManager.findFormatForFileExtension (juce::File (f).getFileExtension()) != nullptr)
            return true;
    return false;
}

void LoadScreen::filesDropped (const juce::StringArray& files, int, int)
{
    hovering = false;
    for (auto& f : files)
    {
        const juce::File file (f);
        if (proc.formatManager.findFormatForFileExtension (file.getFileExtension()) != nullptr)
        {
            proc.loadRoomFile (file);
            break;
        }
    }
    repaint();
}

void LoadScreen::chooseSoundFile()
{
    chooser = std::make_unique<juce::FileChooser> ("choose a sound to build a room from", juce::File(),
                                                   proc.formatManager.getWildcardForAllFormats());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  proc.loadRoomFile (fc.getResult());
                          });
}

void LoadScreen::mouseUp (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addItem ("rooms library", [this] { if (onOpenLibrary) onOpenLibrary(); });
        m.addItem ("build a room from a sound file...", [this] { chooseSoundFile(); });
        m.addSeparator();
        m.addItem ("clear the room", proc.getAnalysis().valid, false, [this] { proc.clearRoom(); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        return;
    }
    if (onOpenLibrary)
        onOpenLibrary();
}

juce::Image LoadScreen::photoFor (const room::RoomAnalysis& a) const
{
    // A glimpse that fits the room: hiss and hum = the tape room, ringing
    // tonal sounds = objects, then by how long the room rings.
    if (! a.valid)            return photos[0];
    if (a.steady)             return photos[4];
    if (a.balance < 0.42f && a.peaks.size() >= 2) return photos[3];
    if (a.medianT60 > 1.2f)   return photos[0];
    if (a.medianT60 > 0.6f)   return photos[2];
    return photos[1];
}

void LoadScreen::timerCallback()
{
    repaint();
}

void LoadScreen::paint (juce::Graphics& g)
{
    const auto glass = Hw::drawScreen (g, getLocalBounds().toFloat());
    auto r = glass.reduced (12.0f, 8.0f);
    const auto status = proc.getStatus();
    const auto t = (float) (juce::Time::getMillisecondCounter() % 100000) * 0.001f;

    // Title row: the room's name (or what's happening)
    juce::String title;
    auto titleCol = Colours::screenText;
    switch (status)
    {
        case RoomAudioProcessor::Status::empty:     title = "drop a sound"; titleCol = Colours::screenDim; break;
        case RoomAudioProcessor::Status::analyzing: title = "listening..."; break;
        case RoomAudioProcessor::Status::recording: title = "recording  " + juce::String (proc.getRecordedSeconds(), 1) + " s";
                                                    titleCol = Colours::crimson.brighter (0.4f); break;
        case RoomAudioProcessor::Status::failed:    title = lower (proc.getStatusMessage()); titleCol = Colours::crimson.brighter (0.4f); break;
        case RoomAudioProcessor::Status::ready:     title = lower (proc.getStatusMessage()); break;
    }
    auto titleRow = r.removeFromTop (24.0f);
    g.setFont (Fonts::mono (15.0f));
    g.setColour (titleCol);
    g.drawText (title, titleRow.withTrimmedRight (16.0f), juce::Justification::centredLeft, true);
    g.setColour (Colours::screenText.withAlpha (0.8f));
    g.drawText (">", titleRow, juce::Justification::centredRight);
    r.removeFromTop (4.0f);
    g.setColour (Colours::screenDim.withAlpha (0.35f));
    g.drawHorizontalLine (juce::roundToInt (r.getY()), r.getX(), r.getRight());
    r.removeFromTop (6.0f);

    // A glimpse of the room
    const auto photoArea = juce::Rectangle<float> (glass.getX(), r.getY(), glass.getWidth(), glass.getBottom() - r.getY());
    float photoAlpha = 0.85f;
    if (status == RoomAudioProcessor::Status::empty) photoAlpha = 0.18f;
    if (status == RoomAudioProcessor::Status::analyzing) photoAlpha = 0.35f + 0.25f * std::sin (t * 4.0f);
    if (status == RoomAudioProcessor::Status::recording) photoAlpha = 0.3f;
    if (const auto photo = photoFor (proc.getAnalysis()); photo.isValid())
    {
        g.saveState();
        g.reduceClipRegion (photoArea.toNearestInt());
        g.setOpacity (juce::jlimit (0.0f, 1.0f, photoAlpha));
        g.drawImage (photo, photoArea, juce::RectanglePlacement::fillDestination);
        g.restoreState();
    }
    juce::ColourGradient fade (Colours::glass, 0.0f, photoArea.getY(), Colours::glass.withAlpha (0.0f), 0.0f, photoArea.getY() + 18.0f, false);
    g.setGradientFill (fade);
    g.fillRect (photoArea.withHeight (18.0f));

    if (status == RoomAudioProcessor::Status::recording)
    {
        auto meter = photoArea.reduced (14.0f, 0.0f).withHeight (4.0f).withY (photoArea.getCentreY());
        g.setColour (Colours::graphite);
        g.fillRoundedRectangle (meter, 2.0f);
        const auto lvl = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (proc.getRecordLevel(), -60.0f) + 60.0f) / 60.0f);
        g.setColour (Colours::crimson);
        g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * lvl), 2.0f);
        g.setColour (Colours::screenDim);
        g.setFont (Fonts::mono (10.5f));
        g.drawText ("press record again to stop", meter.translated (0.0f, 10.0f).withHeight (14.0f), juce::Justification::centredLeft);
    }
    else if (status == RoomAudioProcessor::Status::empty)
    {
        g.setColour (Colours::screenDim);
        g.setFont (Fonts::mono (10.5f));
        g.drawFittedText ("(record one, or click for rooms)", photoArea.reduced (10.0f).toNearestInt(),
                          juce::Justification::centred, 2);
    }

    Hw::drawScreenOverlay (g, glass);
    if (hovering)
    {
        g.setColour (Colours::crimson);
        g.drawRoundedRectangle (glass.reduced (1.0f), 4.5f, 2.0f);
    }
}

//==============================================================================
// Room screen

RoomScreen::RoomScreen (RoomAudioProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("the room. click for its numbers");
    startTimerHz (30);
}

void RoomScreen::setShowInfo (bool s)
{
    showInfo = s;
    repaint();
}

void RoomScreen::timerCallback()
{
    if (proc.getAnalysisVersion() != shownVersion)
    {
        shownVersion = proc.getAnalysisVersion();
        analysis = proc.getAnalysis();
    }
    const auto frozen = proc.apvts.getRawParameterValue (ParamIDs::freeze)->load() > 0.5f;
    phase += frozen ? 0.012f : 0.05f;

    // Sound coming in: a pulse travels from the front of the room to the back.
    const auto level = proc.getInputLevel();
    levelSmooth = std::max (level, levelSmooth * 0.92f);
    if (! frozen && level > 0.05f && level > lastLevel * 1.6f && pulsePos > 0.35f)
        pulsePos = 0.0f;
    lastLevel = level;
    const auto tailS = liveParam (proc, ParamIDs::tail).value;
    pulsePos += 1.0f / (30.0f * juce::jlimit (0.4f, 3.0f, tailS * 0.6f));
    repaint();
}

void RoomScreen::paint (juce::Graphics& g)
{
    const auto glass = Hw::drawScreen (g, getLocalBounds().toFloat());
    if (showInfo)
    {
        paintInfo (g, glass);
        Hw::drawScreenOverlay (g, glass);
        return;
    }

    const auto intensity = liveParam (proc, ParamIDs::intensity);
    const auto tail = liveParam (proc, ParamIDs::tail);
    const auto space = liveParam (proc, ParamIDs::space);
    const auto size = liveParam (proc, ParamIDs::size);
    const auto damping = liveParam (proc, ParamIDs::damping);
    const auto preDelay = liveParam (proc, ParamIDs::preDelay);
    const auto duck = liveParam (proc, ParamIDs::duck);
    const auto balance = liveBalance (proc);
    const auto frozen = proc.apvts.getRawParameterValue (ParamIDs::freeze)->load() > 0.5f;
    const auto valid = analysis.valid;

    // --- the room: front frame -> back wall, one-point perspective ---
    const auto front = glass.reduced (5.0f);
    const auto depth = juce::jlimit (0.45f, 1.25f, 1.12f - 0.5f * size.norm);           // size: deeper room, smaller back wall
    const auto widthK = 0.62f + 0.55f * juce::jlimit (0.0f, 1.0f, space.norm);              // space: wider
    auto back = juce::Rectangle<float> (front.getWidth() * 0.34f * depth * widthK, front.getHeight() * 0.42f * depth)
                    .withCentre ({ front.getCentreX(), front.getY() + front.getHeight() * 0.47f });
    auto at = [&] (float z)   // the room's cross-section at depth z (0 front, 1 back)
    {
        const auto k = (1.0f - 1.0f / (1.0f + 3.0f * z)) / 0.75f;
        return juce::Rectangle<float>::leftTopRightBottom (
            juce::jmap (k, front.getX(), back.getX()), juce::jmap (k, front.getY(), back.getY()),
            juce::jmap (k, front.getRight(), back.getRight()), juce::jmap (k, front.getBottom(), back.getBottom()));
    };

    const auto wallAlpha = (valid ? 0.35f + 0.5f * balance : 0.22f) * (1.0f - 0.35f * duck.norm * levelSmooth);
    const auto wallCol = Colours::screenText.withAlpha (wallAlpha);
    juce::Path walls;
    walls.addRectangle (back);
    for (auto [a, b] : { std::pair { front.getTopLeft(), back.getTopLeft() }, { front.getTopRight(), back.getTopRight() },
                         { front.getBottomLeft(), back.getBottomLeft() }, { front.getBottomRight(), back.getBottomRight() } })
        walls.addLineSegment ({ a, b }, 0.0f);
    for (float z : { 0.22f, 0.45f, 0.68f })   // panels on the side walls and ceiling
    {
        const auto s = at (z);
        walls.addLineSegment ({ s.getTopLeft(), s.getBottomLeft() }, 0.0f);
        walls.addLineSegment ({ s.getTopRight(), s.getBottomRight() }, 0.0f);
        walls.addLineSegment ({ s.getTopLeft(), s.getTopRight() }, 0.0f);
    }
    // A doorway in the back wall
    const auto door = juce::Rectangle<float> (back.getWidth() * 0.15f, back.getHeight() * 0.42f)
                          .withCentre ({ back.getCentreX(), back.getBottom() - back.getHeight() * 0.21f });
    walls.addLineSegment ({ door.getBottomLeft(), door.getTopLeft() }, 0.0f);
    walls.addLineSegment ({ door.getTopLeft(), door.getTopRight() }, 0.0f);
    walls.addLineSegment ({ door.getTopRight(), door.getBottomRight() }, 0.0f);
    Hw::strokeGlow (g, walls, wallCol, 1.0f, 0.5f);

    // --- the decay per band, laid on the floor ---
    if (valid)
    {
        const auto nb = analysis.numBands();
        std::vector<float> decay ((size_t) nb);
        float maxT = 0.3f, median = 0.0f;
        for (int b = 0; b < nb; ++b)
        {
            decay[(size_t) b] = room::bandDecay (analysis.bandT60s[(size_t) b], analysis.bandCentersHz[(size_t) b],
                                                 intensity.value, size.value, damping.value);
            maxT = std::max (maxT, decay[(size_t) b]);
        }
        {
            auto sorted = decay;
            std::sort (sorted.begin(), sorted.end());
            median = sorted.empty() ? 0.0f : sorted[sorted.size() / 2];
        }

        const auto glow = 0.3f + 0.7f * juce::jlimit (0.0f, 1.0f, intensity.norm * 1.4f);
        const auto gap = 0.22f * preDelay.norm;                      // pre-delay: the floor starts further back
        const auto reach = 0.3f + 0.7f * tail.norm;                  // tail: how far back the decay reaches
        const auto dark = damping.norm - 0.5f;                       // damping: darker / brighter red
        const auto baseCol = dark > 0.0f ? Colours::crimson.interpolatedWith (juce::Colour (0xff5e1010), dark * 1.4f)
                                         : Colours::crimson.interpolatedWith (juce::Colour (0xffff8a7a), -dark * 1.2f);
        const auto duckDim = 1.0f - 0.6f * duck.norm * levelSmooth;

        // Warm haze on the floor
        {
            const auto floorBack = at (gap + reach);
            juce::Path floor;
            floor.startNewSubPath (front.getBottomLeft());
            floor.lineTo (front.getBottomRight());
            floor.lineTo (floorBack.getBottomRight());
            floor.lineTo (floorBack.getBottomLeft());
            floor.closeSubPath();
            juce::ColourGradient haze (baseCol.withAlpha (0.0f), 0.0f, front.getBottom(),
                                       baseCol.withAlpha (0.16f * glow * duckDim), 0.0f, floorBack.getBottom(), false);
            g.setGradientFill (haze);
            g.fillPath (floor);
        }

        for (int b = 0; b < nb; ++b)
        {
            const auto z = gap + reach * ((float) b + 0.6f) / (float) nb;
            const auto s = at (z);
            auto a = (0.25f + 0.75f * decay[(size_t) b] / maxT) * glow * duckDim;
            a *= 1.0f - std::max (0.0f, dark) * 1.3f * z;            // dark rooms lose their highs early
            if (frozen)
                a *= 0.75f + 0.25f * std::sin (phase * 3.0f);
            else
                a *= 0.86f + 0.14f * std::sin (phase * 2.0f + (float) b * 0.8f);
            a += 0.8f * std::exp (-std::pow (z - pulsePos, 2.0f) / 0.004f) * glow;   // the pulse from incoming sound
            if (! analysis.bandConfident[(size_t) b])
                a *= 0.6f;
            juce::Path line;
            line.startNewSubPath (s.getBottomLeft());
            line.lineTo (s.getBottomRight());
            Hw::strokeGlow (g, line, baseCol.withAlpha (juce::jlimit (0.0f, 1.0f, a)), 1.1f, 1.2f);
        }

        g.setFont (Fonts::mono (12.0f));
        g.setColour (Colours::screenText.withAlpha (0.85f));
        g.drawText (juce::String (median, 1) + " s", glass.reduced (9.0f, 6.0f).removeFromTop (14.0f),
                    juce::Justification::centredLeft);
    }
    else
    {
        g.setFont (Fonts::mono (11.0f));
        g.setColour (Colours::screenDim);
        g.drawText ("(the room is empty)", glass.reduced (8.0f).removeFromBottom (18.0f), juce::Justification::centred);
    }
    if (frozen)
    {
        g.setFont (Fonts::mono (11.0f));
        g.setColour (Colours::crimson.brighter (0.4f));
        g.drawText ("freeze", glass.reduced (9.0f, 6.0f).removeFromTop (14.0f), juce::Justification::centredRight);
    }
    Hw::drawScreenOverlay (g, glass);
}

void RoomScreen::paintInfo (juce::Graphics& g, juce::Rectangle<float> glass)
{
    auto r = glass.reduced (10.0f, 7.0f);
    g.setFont (Fonts::mono (10.5f));
    auto line = [&] (const juce::String& k, const juce::String& v)
    {
        auto row = r.removeFromTop (13.5f);
        g.setColour (Colours::screenDim);
        g.drawText (k, row.removeFromLeft (64.0f), juce::Justification::centredLeft);
        g.setColour (Colours::screenText);
        g.drawText (v, row, juce::Justification::centredLeft, true);
    };
    if (! analysis.valid)
    {
        g.setColour (Colours::screenDim);
        g.drawText ("(no room yet)", r, juce::Justification::centred);
        return;
    }
    const auto lean = analysis.balance > 0.6f ? "spatial" : analysis.balance < 0.4f ? "tonal" : "in between";
    line ("source", lower (analysis.sourceName));
    line ("length", juce::String (analysis.durationS, 2) + " s" + (analysis.steady ? " (steady)" : " (a hit)"));
    line ("balance", juce::String (analysis.balance, 2) + " " + lean);
    line ("decay", analysis.steady ? juce::String ("none (estimated)") : juce::String (analysis.medianT60, 2) + " s median");
    line ("late", juce::String (juce::roundToInt (analysis.lateEnergyFraction * 100.0f)) + "% after 50 ms");
    juce::String rings;
    for (size_t i = 0; i < analysis.peaks.size() && i < 3; ++i)
    {
        const auto& pk = analysis.peaks[i];
        rings << (pk.freqHz >= 1000.0f ? juce::String (pk.freqHz / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (pk.freqHz)))
              << " +" << juce::roundToInt (pk.gainDb) << "  ";
    }
    line ("rings", rings.isEmpty() ? juce::String ("none") : rings.trim());

    // Decay per band, as a row of tiny bars
    r.removeFromTop (4.0f);
    const auto nb = analysis.numBands();
    float maxT = 0.3f;
    for (auto t : analysis.bandT60s) maxT = std::max (maxT, t);
    const auto bw = r.getWidth() / (float) std::max (1, nb);
    for (int b = 0; b < nb; ++b)
    {
        const auto h = r.getHeight() * analysis.bandT60s[(size_t) b] / maxT;
        g.setColour (Colours::crimson.withAlpha (analysis.bandConfident[(size_t) b] ? 0.9f : 0.45f));
        g.fillRect (r.getX() + bw * (float) b + bw * 0.3f, r.getBottom() - h, bw * 0.4f, h);
    }
}

//==============================================================================
// Tone screen

ToneScreen::ToneScreen (RoomAudioProcessor& p) : proc (p)
{
    startTimerHz (15);
}

void ToneScreen::timerCallback()
{
    const auto inten = liveParam (proc, ParamIDs::intensity).value;
    const auto bal = liveBalance (proc);
    if (proc.getAnalysisVersion() != shownVersion || std::abs (inten - lastIntensity) > 1e-3f || std::abs (bal - lastBalance) > 1e-3f)
    {
        shownVersion = proc.getAnalysisVersion();
        analysis = proc.getAnalysis();
        lastIntensity = inten;
        lastBalance = bal;
        repaint();
    }
}

void ToneScreen::paint (juce::Graphics& g)
{
    const auto glass = Hw::drawScreen (g, getLocalBounds().toFloat());
    auto plot = glass.reduced (8.0f, 8.0f);
    plot.removeFromLeft (24.0f);
    plot.removeFromBottom (14.0f);
    plot.removeFromRight (6.0f);
    plot.removeFromTop (6.0f);

    const auto fMin = 20.0f, fMax = 20000.0f, dbRange = 12.0f;
    auto xFor = [&] (float f) { return plot.getX() + plot.getWidth() * std::log (f / fMin) / std::log (fMax / fMin); };
    auto yFor = [&] (float db) { return plot.getCentreY() - plot.getHeight() * 0.5f * juce::jlimit (-1.1f, 1.1f, db / dbRange); };

    // Grid and printed numbers
    g.setColour (Colours::screenDim.withAlpha (0.16f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        g.drawVerticalLine (juce::roundToInt (xFor (f)), plot.getY(), plot.getBottom());
    for (float db : { -12.0f, -6.0f, 0.0f, 6.0f, 12.0f })
        g.drawHorizontalLine (juce::roundToInt (yFor (db)), plot.getX(), plot.getRight());
    g.setColour (Colours::screenDim);
    g.setFont (Fonts::mono (9.5f));
    for (auto [db, t] : { std::pair { 12.0f, "+12" }, { 0.0f, "0" }, { -12.0f, "-12" } })
        g.drawText (t, juce::Rectangle<float> (glass.getX() + 4.0f, yFor (db) - 6.0f, 26.0f, 12.0f), juce::Justification::centredLeft);
    for (auto [f, t] : { std::pair { 20.0f, "20" }, { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" }, { 20000.0f, "20k" } })
        g.drawText (t, juce::Rectangle<float> (xFor (f) - 14.0f, plot.getBottom() + 2.0f, 28.0f, 12.0f), juce::Justification::centred);

    if (analysis.valid)
    {
        const auto raw = room::targetEqCurveDb (analysis, lastIntensity);
        const auto alpha = 0.45f + 0.55f * (1.0f - juce::jlimit (0.0f, 1.0f, lastBalance));   // tonal rooms read stronger

        // Resample onto a dense log grid (flat beyond the measured ends), then
        // smooth over about half an octave so it reads as a tone, not steps.
        constexpr int n = 200;
        const auto& fr = analysis.toneFreqsHz;
        auto rawAt = [&] (float f)
        {
            if (raw.empty()) return 0.0f;
            if (f <= fr.front()) return raw.front();
            if (f >= fr.back()) return raw.back();
            for (size_t i = 1; i < fr.size() && i < raw.size(); ++i)
                if (fr[i] >= f)
                    return juce::jmap (std::log (f / fr[i - 1]) / std::log (fr[i] / fr[i - 1]), raw[i - 1], raw[i]);
            return raw.back();
        };
        std::vector<float> fs (n), dense (n), curve (n);
        for (int i = 0; i < n; ++i)
        {
            fs[(size_t) i] = fMin * std::pow (fMax / fMin, (float) i / (n - 1));
            dense[(size_t) i] = rawAt (fs[(size_t) i]);
        }
        const auto sigma = 6.0f;   // points (n points span 10 octaves)
        for (int i = 0; i < n; ++i)
        {
            float sum = 0.0f, wsum = 0.0f;
            for (int k = -18; k <= 18; ++k)
            {
                const auto j = juce::jlimit (0, n - 1, i + k);
                const auto w = std::exp (-0.5f * (float) (k * k) / (sigma * sigma));
                sum += w * dense[(size_t) j];
                wsum += w;
            }
            curve[(size_t) i] = sum / wsum;
        }
        juce::Path p;
        for (int i = 0; i < n; ++i)
        {
            const auto x = xFor (fs[(size_t) i]), y = yFor (curve[(size_t) i]);
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());
        Hw::strokeGlow (g, p, Colours::screenText.withAlpha (alpha), 1.3f);
        g.restoreState();

        // The strongest resonance (or the curve's peak), in crimson
        float markF = 1000.0f;
        if (! analysis.peaks.empty())
            markF = std::max_element (analysis.peaks.begin(), analysis.peaks.end(),
                                      [] (auto& a, auto& b) { return a.gainDb < b.gainDb; })->freqHz;
        else
            markF = fs[(size_t) std::distance (curve.begin(), std::max_element (curve.begin(), curve.end()))];
        float markDb = curve.back();
        for (int i = 1; i < n; ++i)
            if (fs[(size_t) i] >= markF)
            {
                markDb = juce::jmap (std::log (markF / fs[(size_t) i - 1]) / std::log (fs[(size_t) i] / fs[(size_t) i - 1]),
                                     curve[(size_t) i - 1], curve[(size_t) i]);
                break;
            }
        const juce::Point<float> dot (xFor (juce::jlimit (fMin, fMax, markF)), yFor (markDb));
        g.setColour (Colours::crimson.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (dot));
        g.setColour (Colours::crimson.brighter (0.15f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (dot));
    }
    Hw::drawScreenOverlay (g, glass);
}

//==============================================================================
// Keys

IconButton::IconButton (Icon i, const juce::String& tip) : juce::Button (tip), icon (i)
{
    setTooltip (tip);
}

void IconButton::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && onRightClick != nullptr)
    {
        onRightClick();
        return;
    }
    juce::Button::mouseDown (e);
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    Hw::drawKey (g, r, true, over, down);
    r = r.withTrimmedBottom (2.0f).translated (0.0f, down ? 0.8f : 0.0f);
    const auto c = r.getCentre();
    const auto s = std::min (r.getWidth(), r.getHeight()) * 0.5f;
    g.setColour (Colours::ink);
    juce::PathStrokeType stroke (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    switch (icon)
    {
        case Icon::record:
        {
            const auto on = getToggleState();
            g.drawEllipse (juce::Rectangle<float> (s * 1.1f, s * 1.1f).withCentre (c), 2.2f);
            const auto blink = ! on || (juce::Time::getMillisecondCounter() / 450) % 2 == 0;
            g.setColour (on ? Colours::crimson.withAlpha (blink ? 1.0f : 0.45f) : Colours::ink);
            g.fillEllipse (juce::Rectangle<float> (s * 0.48f, s * 0.48f).withCentre (c));
            return;
        }
        case Icon::save:
        {
            // a folder
            const auto w = s * 1.25f, h = s * 0.92f;
            const auto x = c.x - w * 0.5f, y = c.y - h * 0.38f;
            p.startNewSubPath (x, y + h);
            p.lineTo (x, y - h * 0.2f);
            p.lineTo (x + w * 0.38f, y - h * 0.2f);
            p.lineTo (x + w * 0.48f, y);
            p.lineTo (x + w, y);
            p.lineTo (x + w, y + h);
            p.closeSubPath();
            break;
        }
        case Icon::randomize:
        {
            const auto x0 = c.x - s * 0.62f, x1 = c.x + s * 0.62f, y0 = c.y - s * 0.38f, y1 = c.y + s * 0.38f;
            p.startNewSubPath (x0, y1);
            p.cubicTo (c.x, y1, c.x, y0, x1, y0);
            p.startNewSubPath (x0, y0);
            p.cubicTo (c.x, y0, c.x, y1, x1, y1);
            for (auto y : { y0, y1 })
            {
                p.startNewSubPath (x1 - s * 0.22f, y - s * 0.2f);
                p.lineTo (x1, y);
                p.lineTo (x1 - s * 0.22f, y + s * 0.2f);
            }
            break;
        }
    }
    g.strokePath (p, stroke);
}

void BypassSwitch::paintButton (juce::Graphics& g, bool over, bool)
{
    // Toggle lever (up = on) and the power LED: lit while ROOM is running.
    const auto bypassed = getToggleState();
    auto r = getLocalBounds().toFloat();
    const auto lever = r.removeFromLeft (16.0f).withSizeKeepingCentre (12.0f, 24.0f);
    g.setColour (juce::Colours::black.withAlpha (0.2f));
    g.fillRoundedRectangle (lever.translated (0.0f, 1.5f), 5.0f);
    juce::ColourGradient body (juce::Colour (0xff3a3a3a), lever.getX(), lever.getY(), juce::Colour (0xff111111), lever.getRight(), lever.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (lever, 5.0f);
    const auto knob = juce::Rectangle<float> (8.0f, 9.0f).withCentre ({ lever.getCentreX(), bypassed ? lever.getBottom() - 6.0f : lever.getY() + 6.0f });
    juce::ColourGradient metal (juce::Colour (over ? 0xfff2f0eb : 0xffdedbd4), knob.getX(), knob.getY(), juce::Colour (0xff7c7972), knob.getRight(), knob.getBottom(), false);
    g.setGradientFill (metal);
    g.fillRoundedRectangle (knob, 3.0f);
    Hw::drawLed (g, r.withTrimmedLeft (8.0f).getCentre(), ! bypassed, 4.5f);
}

//==============================================================================
// Editor

RoomAudioProcessorEditor::RoomAudioProcessorEditor (RoomAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), loadScreen (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.painter = [this] (juce::Graphics& g) { paintContent (g); };
    content.layout = [this] { layoutContent(); };

    consoleGrain = Hw::grain ((int) consoleArea.getWidth(), (int) consoleArea.getHeight(), false, 7);
    blackGrain = Hw::grain ((int) advancedArea.getWidth(), (int) advancedArea.getHeight(), true, 11);
    paperTexture = Hw::asset ("tex_paper.jpg");    // painted steel and powder coat, made in Higgsfield
    blackTexture = Hw::asset ("tex_black.jpg");

    // --- Top bar ---
    content.addAndMakeVisible (presetBar);
    content.addAndMakeVisible (randomButton);
    randomButton.onClick = [this] { proc.randomizeKnobs (random); };
    randomButton.onRightClick = [this] { showRandomizeMenu(); };
    content.addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::bypass, bypassButton);
    setupKnob (mix, ParamIDs::mix, "wet", 15.0f, "", "", "", false);
    setupKnob (output, ParamIDs::output, "out", 15.0f, "", "", "", false);

    // --- Load / room / tone ---
    content.addAndMakeVisible (loadScreen);
    loadScreen.onOpenLibrary = [this] { roomBrowser.setVisible (true); roomBrowser.toFront (true); };
    content.addAndMakeVisible (recordButton);
    recordButton.setClickingTogglesState (false);
    recordButton.onClick = [this]
    {
        if (proc.isRecording()) proc.stopRecording();
        else                    proc.startRecording();
    };
    content.addAndMakeVisible (roomSaveButton);
    roomSaveButton.onClick = [this] { openRoomSaveDialog(); };
    content.addAndMakeVisible (roomScreen);
    content.addAndMakeVisible (toneScreen);

    // --- Shape the room ---
    setupKnob (intensity, ParamIDs::intensity, "intensity", 37.0f, "0", "100", "200", false);
    setupKnob (balance, ParamIDs::balance, "balance", 37.0f, "tone", "", "space", false);
    setupKnob (tail, ParamIDs::tail, "tail", 37.0f, "0.3", "", "6 s", false);
    setupKnob (space, ParamIDs::space, "space", 37.0f, "mono", "", "wide", false);
    autoBalance.getProperties().set ("style", "led");
    autoBalance.setTooltip ("auto: use the balance ROOM detected in the sound");
    content.addAndMakeVisible (autoBalance);
    autoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::balanceAuto, autoBalance);
    balance.slider.textFromValueFunction = [this] (double v)
    {
        const auto isAuto = autoBalance.getToggleState();
        const auto b = isAuto ? proc.getDetectedBalance() : (float) v;
        return juce::String (isAuto ? "auto: " : "") + juce::String (juce::roundToInt ((1.0f - b) * 100.0f)) + " tone / "
               + juce::String (juce::roundToInt (b * 100.0f)) + " space";
    };

    // --- Advanced (attached) ---
    content.addAndMakeVisible (advancedPanel);

    // --- Finish (separate module) ---
    content.addAndMakeVisible (eqPanel);
    setupKnob (tape, ParamIDs::tape, "tape", 28.0f, "0", "", "100", true);
    setupKnob (autoEq, ParamIDs::autoEq, "auto eq", 28.0f, "0", "", "100", true);

    // --- STUDIOS ---
    presetBar.onOpen = [this] { browser.setVisible (true); browser.toFront (true); };
    presetBar.onPrev = [this] { stepPreset (-1); };
    presetBar.onNext = [this] { stepPreset (1); };
    presetBar.onSave = [this] { openSaveDialog(); };
    presetBar.onToggleFavorite = [this]
    {
        auto& lib = proc.getLibrary();
        if (auto* e = lib.find (proc.getCurrentPresetKey()))
            lib.setFavorite (e->key, ! e->favorite);
    };

    content.addChildComponent (browser);
    browser.onClose = [this] { browser.setVisible (false); };
    browser.onLoad = [this] (const juce::String& key) { loadPreset (key); };
    browser.onEdit = [this] (const juce::String& key, const room::presets::PresetMeta& meta, const juce::String& pack)
    {
        editingKey = key;
        saveDialog.open (room::presets::SaveDialog::Mode::edit, meta, proc.getLibrary().getPacks(), pack);
    };

    content.addChildComponent (roomBrowser);
    roomBrowser.onClose = [this] { roomBrowser.setVisible (false); };
    roomBrowser.onLoad = [this] (const juce::String& key) { loadRoom (key); roomBrowser.setVisible (false); };
    roomBrowser.onOpenSoundFile = [this] { roomBrowser.setVisible (false); loadScreen.chooseSoundFile(); };
    roomBrowser.onEdit = [this] (const juce::String& key, const room::presets::PresetMeta& meta, const juce::String& pack)
    {
        editingRoomKey = key;
        juce::String info;
        if (auto* e = proc.getRoomLibrary().find (key))
            info = "auto tags:  " + juce::StringArray { e->tags.decay, e->tags.tone, e->tags.source }.joinIntoString ("  \xc2\xb7  ").toLowerCase();
        roomSaveDialog.open (room::presets::SaveDialog::Mode::edit, meta, proc.getRoomLibrary().getPacks(), pack, info);
    };

    content.addChildComponent (saveDialog);
    saveDialog.onSubmit = [this] (auto mode, const auto& meta, const auto& pack, bool overwrite)
    {
        return submitSaveDialog (mode, meta, pack, overwrite);
    };
    content.addChildComponent (roomSaveDialog);
    roomSaveDialog.onSubmit = [this] (auto mode, const auto& meta, const auto& pack, bool overwrite)
    {
        return submitRoomSaveDialog (mode, meta, pack, overwrite);
    };

    content.setBounds (0, 0, baseWidth, baseHeight);

    // Resizable, keeping the shape: everything scales together.
    // (Size first: the limits/aspect constrainer would otherwise start from 0 x 0.)
    const auto startScale = juce::jlimit (0.6f, 1.4f, proc.editorScale);
    setSize (juce::roundToInt (baseWidth * startScale), juce::roundToInt (baseHeight * startScale));
    setResizable (true, true);
    setResizeLimits (baseWidth * 3 / 5, baseHeight * 3 / 5, baseWidth * 7 / 5, baseHeight * 7 / 5);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseWidth / baseHeight);
    setSize (juce::roundToInt (baseWidth * startScale), juce::roundToInt (baseHeight * startScale));

    startTimerHz (15);
}

RoomAudioProcessorEditor::~RoomAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void RoomAudioProcessorEditor::setupKnob (Knob& k, const juce::String& id, const juce::String& name, float radius,
                                          const juce::String& lo, const juce::String& mid, const juce::String& hi, bool onBlack)
{
    k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.slider.setPopupDisplayEnabled (true, true, &content);
    auto& props = k.slider.getProperties();
    props.set ("knobRadius", radius);
    props.set ("scaleMin", lo);
    props.set ("scaleMid", mid);
    props.set ("scaleMax", hi);
    if (onBlack)
        props.set ("surface", "black");
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (Fonts::sans (radius < 20.0f ? 13.0f : 16.0f));
    k.label.setColour (juce::Label::textColourId, onBlack ? Colours::dustWhite : Colours::ink);
    k.label.setInterceptsMouseClicks (false, false);
    content.addAndMakeVisible (k.slider);
    content.addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
    k.slider.setModTarget (&proc.modulation, room::Modulation::targetIndex (id));   // -1 = not an LFO target
}

void RoomAudioProcessorEditor::setAdvanced (bool showLfos)
{
    advancedPanel.showModulationTab (showLfos);
}

void RoomAudioProcessorEditor::timerCallback()
{
    {
        auto& lib = proc.getLibrary();
        const auto* e = lib.find (proc.getCurrentPresetKey());
        presetBar.setPreset (lower (proc.getCurrentPresetName()), proc.isPresetModified(), e != nullptr && e->favorite, e != nullptr);
    }

    // LFO assignments can change from presets or the right-click menu.
    for (auto* k : { &intensity, &balance, &tail, &space, &mix, &output, &tape, &autoEq })
        k->slider.refreshModulation();
    advancedPanel.refreshModulation();

    const auto rec = proc.isRecording();
    if (recordButton.getToggleState() != rec)
        recordButton.setToggleState (rec, juce::dontSendNotification);
    if (rec)
        recordButton.repaint();
    const auto valid = proc.getAnalysis().valid;
    roomSaveButton.setEnabled (valid);
    roomSaveButton.setAlpha (valid ? 1.0f : 0.45f);

    const auto isAuto = autoBalance.getToggleState();
    if (balance.slider.isEnabled() == isAuto)
        balance.slider.setEnabled (! isAuto);
}

//==============================================================================
// Painting: backdrop, the console with its printed path, the black modules

void RoomAudioProcessorEditor::paintContent (juce::Graphics& g)
{
    g.fillAll (Colours::backdrop);

    auto plate = [&] (juce::Rectangle<float> r, bool black, float corner)
    {
        g.setColour (juce::Colours::black.withAlpha (0.18f));
        g.fillRoundedRectangle (r.translated (0.0f, 6.0f).expanded (2.0f), corner + 2.0f);
        g.setColour (juce::Colours::black.withAlpha (0.10f));
        g.fillRoundedRectangle (r.translated (0.0f, 12.0f).expanded (5.0f), corner + 5.0f);
        juce::ColourGradient body (black ? juce::Colour (0xff1c1c1c) : Colours::paper.brighter (0.03f), 0.0f, r.getY(),
                                   black ? juce::Colour (0xff0d0d0d) : Colours::paper.darker (0.04f), 0.0f, r.getBottom(), false);
        if (const auto& tex = black ? blackTexture : paperTexture; tex.isValid())
        {
            g.setTiledImageFill (tex, 0, 0, 1.0f);
            g.fillRoundedRectangle (r, corner);
            if (black)   // calm the powder coat down: it should read as matte, not rough
            {
                g.setColour (juce::Colour (0xff111111).withAlpha (0.55f));
                g.fillRoundedRectangle (r, corner);
            }
            // light falls off a touch toward the bottom
            juce::ColourGradient shade (juce::Colours::white.withAlpha (black ? 0.03f : 0.10f), 0.0f, r.getY(),
                                        juce::Colours::black.withAlpha (black ? 0.30f : 0.06f), 0.0f, r.getBottom(), false);
            g.setGradientFill (shade);
            g.fillRoundedRectangle (r, corner);
        }
        else
        {
            g.setGradientFill (body);
            g.fillRoundedRectangle (r, corner);
        }
        g.setOpacity (1.0f);
        g.drawImage (black ? blackGrain : consoleGrain, r, juce::RectanglePlacement::stretchToFit);
        g.setColour (black ? juce::Colours::white.withAlpha (0.07f) : juce::Colours::white.withAlpha (0.6f));
        g.drawLine (r.getX() + corner, r.getY() + 0.8f, r.getRight() - corner, r.getY() + 0.8f, 1.0f);
        g.setColour (black ? juce::Colours::black : juce::Colour (0xffa29d93));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
    };

    // Cable from the console down into finish
    {
        const auto x = consoleArea.getCentreX() - 30.0f;
        juce::Path cable;
        cable.startNewSubPath (x, consoleArea.getBottom() - 4.0f);
        cable.lineTo (x, finishArea.getY() + 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.25f));
        g.strokePath (cable, juce::PathStrokeType (12.0f), juce::AffineTransform::translation (2.0f, 2.0f));
        g.setColour (juce::Colour (0xff121212));
        g.strokePath (cable, juce::PathStrokeType (10.0f));
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawLine (x - 2.5f, consoleArea.getBottom(), x - 2.5f, finishArea.getY(), 1.5f);
        for (auto y : { consoleArea.getBottom() + 2.0f, finishArea.getY() - 6.0f })
        {
            g.setColour (juce::Colour (0xff2a2a2a));
            g.fillRoundedRectangle (x - 9.0f, y, 18.0f, 6.0f, 2.0f);
        }
    }

    plate (finishArea, true, 6.0f);
    plate (advancedArea, true, 6.0f);
    plate (consoleArea, false, 6.0f);

    paintConsoleArt (g);

    // Screws
    for (auto r : { consoleArea, advancedArea, finishArea })
    {
        const auto black = r != consoleArea;
        const auto in = r.reduced (14.0f);
        for (auto p : { in.getTopLeft(), in.getTopRight(), in.getBottomLeft(), in.getBottomRight() })
            Hw::drawScrew (g, p, black);
    }

    // Finish module print
    g.setColour (Colours::dustWhite);
    g.setFont (Fonts::sans (26.0f, Fonts::bold));
    g.drawText ("finish", juce::Rectangle<float> (finishArea.getX() + 36.0f, finishArea.getY() + 16.0f, 200.0f, 34.0f),
                juce::Justification::centredLeft);
    {
        // LED and the out jack
        const juce::Point<float> led (finishArea.getRight() - 72.0f, finishArea.getY() + 64.0f);
        Hw::drawLed (g, led, ! (proc.apvts.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f));
        const juce::Point<float> jack (finishArea.getRight() - 72.0f, finishArea.getY() + 128.0f);
        g.setColour (juce::Colour (0xff8f8c86));
        g.fillEllipse (juce::Rectangle<float> (34.0f, 34.0f).withCentre (jack));
        g.setColour (juce::Colour (0xff2c2c2c));
        g.fillEllipse (juce::Rectangle<float> (26.0f, 26.0f).withCentre (jack));
        g.setColour (juce::Colours::black);
        g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (jack));
        g.setColour (Colours::dustWhite);
        g.setFont (Fonts::sans (13.0f));
        g.drawText ("out", juce::Rectangle<float> (60.0f, 18.0f).withCentre (jack.translated (0.0f, 32.0f)), juce::Justification::centred);
    }
}

void RoomAudioProcessorEditor::paintConsoleArt (juce::Graphics& g)
{
    const auto c = consoleArea;

    // Wordmark: room (Satoshi Black) by eville (Cormorant italic)
    g.setColour (Colours::ink);
    const auto wordFont = Fonts::sans (54.0f, Fonts::black);
    g.setFont (wordFont);
    const auto wordX = c.getX() + 38.0f;
    const auto wordW = (float) juce::GlyphArrangement::getStringWidthInt (wordFont, "room");
    g.drawText ("room", juce::Rectangle<float> (wordX, c.getY() + 22.0f, wordW + 10.0f, 60.0f), juce::Justification::centredLeft);
    g.setFont (Fonts::serif (21.0f));
    g.setColour (Colours::ink.withAlpha (0.85f));
    g.drawText ("by eville", juce::Rectangle<float> (wordX + wordW + 12.0f, c.getY() + 42.0f, 100.0f, 30.0f), juce::Justification::centredLeft);

    // Top bar print
    g.setFont (Fonts::sans (13.0f));
    g.setColour (Colours::ink);
    g.drawText ("bypass", bypassButton.getBounds().translated (0, -20).withHeight (18).withSizeKeepingCentre (70, 18),
                juce::Justification::centred);
    g.setColour (Colours::paperLine);
    g.drawLine (c.getX() + 24.0f, c.getY() + 100.0f, c.getRight() - 24.0f, c.getY() + 100.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.drawLine (c.getX() + 24.0f, c.getY() + 101.0f, c.getRight() - 24.0f, c.getY() + 101.0f, 1.0f);

    // Faint contour lines (printed, dotted), behind everything else
    {
        juce::Random rng (5);
        for (int i = 0; i < 7; ++i)
        {
            juce::Path contour;
            const auto y0 = c.getY() + 372.0f + (float) i * 26.0f;
            const auto ph = rng.nextFloat() * 6.0f;
            for (int k = 0; k <= 60; ++k)
            {
                const auto x = c.getX() + 30.0f + (c.getWidth() - 60.0f) * (float) k / 60.0f;
                const auto y = y0 + 9.0f * std::sin ((float) k * 0.21f + ph) + 5.0f * std::sin ((float) k * 0.07f + ph * 2.0f);
                if (k == 0) contour.startNewSubPath (x, y); else contour.lineTo (x, y);
            }
            Hw::drawDottedPath (g, contour, Colours::inkDim.withAlpha (0.28f), 5.5f, 0.6f);
        }
    }

    // Bay frames with their names on the frame line
    const char* names[3] = { "load", "room", "tone" };
    for (int i = 0; i < 3; ++i)
    {
        const auto b = bays[i].toFloat();
        g.setColour (Colours::paperLine);
        g.drawRoundedRectangle (b, 7.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawRoundedRectangle (b.translated (0.5f, 1.0f), 7.0f, 0.6f);
        g.setFont (Fonts::sans (16.0f));
        const auto tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), names[i]);
        const auto label = juce::Rectangle<float> (b.getX() + 14.0f, b.getY() - 10.0f, tw + 12.0f, 20.0f);
        g.setColour (Colours::paper);
        g.fillRect (label);
        g.setColour (Colours::ink);
        g.drawText (names[i], label, juce::Justification::centred);
    }

    // Labels under the record / save keys
    g.setFont (Fonts::sans (12.5f));
    g.setColour (Colours::ink);
    for (auto* b : { (juce::Component*) &recordButton, (juce::Component*) &roomSaveButton })
        g.drawText (b == &recordButton ? "record" : "save", b->getBounds().translated (b->getWidth() + 6, 0).withWidth (60),
                    juce::Justification::centredLeft);

    // The signal path: crimson dots with a faint black companion, through tiny rooms.
    const auto red = Colours::crimson.withAlpha (0.85f);
    const auto blk = Colours::ink.withAlpha (0.5f);
    auto path = [&] (std::initializer_list<juce::Point<float>> pts)
    {
        // Smooth curve through the points (Catmull-Rom as cubic beziers)
        std::vector<juce::Point<float>> v (pts);
        juce::Path p;
        p.startNewSubPath (v.front());
        for (size_t i = 0; i + 1 < v.size(); ++i)
        {
            const auto p0 = v[i == 0 ? 0 : i - 1], p1 = v[i], p2 = v[i + 1], p3 = v[std::min (v.size() - 1, i + 2)];
            p.cubicTo (p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
        }
        Hw::drawDottedPath (g, p, red, 5.0f, 1.0f);
        juce::Path off (p);
        off.applyTransform (juce::AffineTransform::translation (0.0f, 4.0f));
        Hw::drawDottedPath (g, off, blk, 7.0f, 0.6f);
    };
    const auto ox = c.getX(), oy = c.getY();
    const auto b0 = bays[0].toFloat(), b1 = bays[1].toFloat(), b2 = bays[2].toFloat();
    // load -> room -> tone (over the top of the bays)
    path ({ { b0.getRight() - 30.0f, b0.getY() - 2.0f }, { b0.getRight() + 10.0f, b0.getY() - 16.0f }, { b1.getX() + 60.0f, b1.getY() - 10.0f } });
    path ({ { b1.getRight() - 60.0f, b1.getY() - 10.0f }, { b1.getRight() + 10.0f, b1.getY() - 18.0f }, { b2.getX() + 50.0f, b2.getY() - 10.0f } });
    // out of the load bay, under the screens, round to the knobs
    const juce::Point<float> roomA (ox + 58.0f, b0.getBottom() + 34.0f), roomB (ox + 858.0f, b0.getBottom() + 36.0f);
    path ({ { b0.getCentreX() - 30.0f, b0.getBottom() }, { ox + 100.0f, b0.getBottom() + 22.0f }, roomA + juce::Point<float> (14.0f, -6.0f) });
    path ({ roomA + juce::Point<float> (14.0f, 8.0f), { ox + 200.0f, b0.getBottom() + 34.0f }, { ox + 300.0f, b0.getBottom() + 26.0f }, { ox + 480.0f, b0.getBottom() + 20.0f },
            { ox + 680.0f, b0.getBottom() + 34.0f }, roomB - juce::Point<float> (16.0f, 0.0f) });
    // knob to knob
    const auto ky = (float) intensity.slider.getBounds().getCentreY();
    juce::Component* knobs[4] = { &intensity.slider, &balance.slider, &tail.slider, &space.slider };
    for (int i = 0; i < 3; ++i)
    {
        const auto x0 = (float) knobs[i]->getBounds().getCentreX() + 56.0f, x1 = (float) knobs[i + 1]->getBounds().getCentreX() - 56.0f;
        path ({ { x0, ky + 4.0f }, { juce::jmap (0.33f, x0, x1), ky + 16.0f }, { juce::jmap (0.66f, x0, x1), ky - 8.0f }, { x1, ky + 4.0f } });
    }
    // space -> the room on the right -> into advanced
    const juce::Point<float> roomC (ox + 872.0f, ky + 64.0f);
    path ({ { (float) space.slider.getBounds().getCentreX() + 56.0f, ky + 4.0f }, { ox + 846.0f, ky + 30.0f }, roomC - juce::Point<float> (0.0f, 14.0f) });
    path ({ roomB + juce::Point<float> (0.0f, 14.0f), { ox + 880.0f, ky - 40.0f }, { ox + 900.0f, ky - 10.0f } });

    for (auto p : { roomA, roomB, roomC })
        Hw::drawTinyRoom (g, p, 15.0f, Colours::ink.withAlpha (0.55f));
    Hw::drawTinyRoom (g, { ox + 470.0f, oy + 532.0f }, 12.0f, Colours::ink.withAlpha (0.28f));
}

void RoomAudioProcessorEditor::resized()
{
    const auto scale = (float) getWidth() / (float) baseWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
    proc.editorScale = scale;
}

void RoomAudioProcessorEditor::layoutContent()
{
    const auto full = juce::Rectangle<int> (0, 0, baseWidth, baseHeight);
    browser.setBounds (full);
    roomBrowser.setBounds (full);
    saveDialog.setBounds (full);
    roomSaveDialog.setBounds (full);

    const auto c = consoleArea.toNearestInt();

    // --- Top bar ---
    presetBar.setBounds (c.getX() + 290, c.getY() + 32, 370, 40);
    randomButton.setBounds (c.getX() + 678, c.getY() + 33, 40, 38);
    for (auto [k, x] : { std::pair { &mix, 760 }, { &output, 816 } })
    {
        k->slider.setBounds (juce::Rectangle<int> (54, 48).withCentre ({ c.getX() + x, c.getY() + 46 }));
        k->label.setBounds (juce::Rectangle<int> (54, 16).withCentre ({ c.getX() + x, c.getY() + 80 }));
    }
    bypassButton.setBounds (c.getX() + 846, c.getY() + 44, 36, 30);

    // --- Row 1: three identical bays ---
    const int bayW = 268, bayH = 216, bayY = c.getY() + 124, gap = 20;
    for (int i = 0; i < 3; ++i)
        bays[i] = { c.getX() + 28 + i * (bayW + gap), bayY, bayW, bayH };
    const auto screenOf = [] (juce::Rectangle<int> bay) { return bay.reduced (14).withTrimmedTop (6); };
    {
        auto s = screenOf (bays[0]);
        auto keys = s.removeFromBottom (40);
        s.removeFromBottom (8);
        loadScreen.setBounds (s);
        recordButton.setBounds (keys.getX() + 8, keys.getY() + 2, 38, 36);
        roomSaveButton.setBounds (keys.getX() + 126, keys.getY() + 2, 38, 36);
    }
    roomScreen.setBounds (screenOf (bays[1]));
    toneScreen.setBounds (screenOf (bays[2]));

    // --- Row 2: the four knobs ---
    const int kx0 = c.getX() + 28, kw = (c.getWidth() - 56) / 4, ky = c.getY() + 444;
    Knob* knobs[4] = { &intensity, &balance, &tail, &space };
    for (int i = 0; i < 4; ++i)
    {
        const auto cx = kx0 + kw * i + kw / 2;
        knobs[i]->slider.setBounds (juce::Rectangle<int> (150, 124).withCentre ({ cx, ky }));
        knobs[i]->label.setBounds (juce::Rectangle<int> (140, 22).withCentre ({ cx, ky + 70 }));
    }
    autoBalance.setBounds (juce::Rectangle<int> (60, 18).withCentre ({ kx0 + kw + kw / 2 + 4, ky + 92 }));

    // --- Advanced (attached on the right) ---
    advancedPanel.setBounds (advancedArea.toNearestInt());

    // --- Finish ---
    const auto f = finishArea.toNearestInt();
    eqPanel.setBounds (f.getX() + 32, f.getY() + 58, 560, f.getHeight() - 84);
    for (auto [k, x] : { std::pair { &tape, 660 }, { &autoEq, 770 } })
    {
        k->slider.setBounds (juce::Rectangle<int> (110, 100).withCentre ({ f.getX() + x, f.getY() + 120 }));
        k->label.setBounds (juce::Rectangle<int> (100, 20).withCentre ({ f.getX() + x, f.getY() + 182 }));
    }
}

//==============================================================================
// Randomize menu (right-click the Randomize button)

void RoomAudioProcessorEditor::showRandomizeMenu()
{
    using P = RoomAudioProcessor;
    const std::pair<int, const char*> groups[] = {
        { P::randomRoom,     "room knobs  (intensity, balance, tail, space)" },
        { P::randomAdvanced, "advanced  (pre-delay, size, damping, ducking)" },
        { P::randomEq,       "eq bands" },
        { P::randomFinish,   "tape + auto eq" },
        { P::randomLfos,     "lfo settings  (shape, rate)" },
    };

    juce::PopupMenu menu;
    menu.addSectionHeader ("randomize");
    menu.addItem ("randomize now", proc.randomizeMask != 0, false, [this] { proc.randomizeKnobs (random); });
    menu.addSeparator();
    for (auto& [bit, name] : groups)
        menu.addItem (name, true, (proc.randomizeMask & bit) != 0, [this, bit = bit]
        {
            proc.randomizeMask ^= bit;
            showRandomizeMenu();   // stay open so several can be ticked in one go
        });
    menu.addSeparator();
    menu.addItem ("all", true, false, [this] { proc.randomizeMask = RoomAudioProcessor::randomAll; showRandomizeMenu(); });
    menu.addItem ("none", true, false, [this] { proc.randomizeMask = 0; showRandomizeMenu(); });
    menu.addItem ("back to default", true, false, [this] { proc.randomizeMask = RoomAudioProcessor::randomDefault; showRandomizeMenu(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&randomButton));
}

//==============================================================================
// STUDIOS (full presets, top bar) and ROOMS (the room only, tier 1)

void RoomAudioProcessorEditor::loadRoom (const juce::String& key)
{
    auto& lib = proc.getRoomLibrary();
    const auto* e = lib.find (key);
    if (e == nullptr)
        return;
    const auto tree = lib.loadPreset (*e);
    if (! tree.isValid())
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("ROOMS")
                                          .withMessage ("\"" + e->meta.name + "\" couldn't be read.").withButton ("OK"), nullptr);
        return;
    }
    proc.applyRoom (tree, key);
}

void RoomAudioProcessorEditor::openRoomSaveDialog()
{
    const auto a = proc.getAnalysis();
    if (! a.valid)
        return;
    auto& lib = proc.getRoomLibrary();
    room::presets::PresetMeta meta;
    juce::String pack = room::presets::PresetLibrary::userPack;
    if (const auto* e = lib.find (proc.getCurrentRoomKey()))
    {
        meta = e->meta;
        meta.created = {};
        if (e->factory)
            meta.author = {};
        else
            pack = e->pack;
    }
    else
    {
        meta.name = a.sourceName;
        // A room that came with a STUDIO keeps that STUDIO's room type as a starting point.
        if (const auto* s = proc.getLibrary().find (proc.getCurrentPresetKey()))
            meta.type = s->meta.type;
    }

    const auto tags = room::presets::autoTags (a.toValueTree(), "File");
    editingRoomKey.clear();
    roomSaveDialog.open (room::presets::SaveDialog::Mode::save, meta, lib.getPacks(), pack,
                         "auto tags:  " + juce::StringArray { tags.decay, tags.tone, tags.source }.joinIntoString ("  \xc2\xb7  ").toLowerCase());
}

juce::String RoomAudioProcessorEditor::submitRoomSaveDialog (room::presets::SaveDialog::Mode mode,
                                                             const room::presets::PresetMeta& metaIn,
                                                             const juce::String& pack, bool overwrite)
{
    using room::presets::PresetLibrary;
    auto& lib = proc.getRoomLibrary();
    auto meta = metaIn;

    if (meta.name.isEmpty())
        return "give the room a name.";
    if (meta.type.isEmpty())
        return "pick a type.";
    if (! PresetLibrary::isValidPackName (pack))
        return "choose or name a pack (not factory).";

    juce::String err;
    if (! lib.getPacks().contains (pack) && ! lib.createPack (pack, err))
        return err;

    juce::String key;
    if (mode == room::presets::SaveDialog::Mode::save)
    {
        if (meta.author.isEmpty())
            meta.author = "User";
        meta.created = juce::Time::getCurrentTime();
        const auto tree = proc.captureRoom (meta);
        if (! tree.isValid())
            return "there's no room to save yet (drop, record or load one first).";
        if (! lib.save (tree, pack, overwrite, key, err))
            return err;
        // The room now goes by its saved name.
        proc.applyRoom (lib.loadPreset (*lib.find (key)), key);
    }
    else
    {
        if (! lib.updateMeta (editingRoomKey, meta, pack, key, err))
            return err;
        if (editingRoomKey == proc.getCurrentRoomKey())
            proc.setCurrentRoomKey (key);
    }

    if (roomBrowser.isVisible())
        roomBrowser.refresh();
    return {};
}

void RoomAudioProcessorEditor::loadPreset (const juce::String& key)
{
    auto& lib = proc.getLibrary();
    const auto* e = lib.find (key);
    if (e == nullptr)
        return;
    const auto tree = lib.loadPreset (*e);
    if (! tree.isValid())
    {
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("STUDIOS")
                                          .withMessage ("\"" + e->meta.name + "\" couldn't be read.").withButton ("OK"), nullptr);
        return;
    }
    proc.applyPreset (tree, key);
    browser.repaint();
}

void RoomAudioProcessorEditor::stepPreset (int delta)
{
    auto& lib = proc.getLibrary();
    const auto list = browser.currentList();
    if (list.empty())
        return;

    int index = -1;
    for (int i = 0; i < (int) list.size(); ++i)
        if (lib.getEntries()[(size_t) list[(size_t) i]].key == proc.getCurrentPresetKey())
            index = i;

    const auto n = (int) list.size();
    const auto next = index < 0 ? (delta > 0 ? 0 : n - 1) : ((index + delta) % n + n) % n;
    loadPreset (lib.getEntries()[(size_t) list[(size_t) next]].key);
}

void RoomAudioProcessorEditor::openSaveDialog()
{
    auto& lib = proc.getLibrary();
    room::presets::PresetMeta meta;
    juce::String pack = room::presets::PresetLibrary::userPack;
    if (const auto* e = lib.find (proc.getCurrentPresetKey()))
    {
        meta = e->meta;
        meta.created = {};
        if (e->factory)
            meta.author = {};   // a copy of a factory STUDIO becomes yours
        else
            pack = e->pack;
    }
    else
        meta.name = proc.getCurrentPresetName().isNotEmpty() ? proc.getCurrentPresetName() : proc.getAnalysis().sourceName;
    // The STUDIO remembers its room's type (from the ROOM it came from, if it was loaded from ROOMS).
    if (const auto* r = proc.getRoomLibrary().find (proc.getCurrentRoomKey()))
        meta.type = r->meta.type;

    editingKey.clear();
    saveDialog.open (room::presets::SaveDialog::Mode::save, meta, lib.getPacks(), pack);
}

juce::String RoomAudioProcessorEditor::submitSaveDialog (room::presets::SaveDialog::Mode mode,
                                                         const room::presets::PresetMeta& metaIn,
                                                         const juce::String& pack, bool overwrite)
{
    using room::presets::PresetLibrary;
    auto& lib = proc.getLibrary();
    auto meta = metaIn;

    if (meta.name.isEmpty())
        return "give the studio a name.";
    if (! PresetLibrary::isValidPackName (pack))
        return "choose or name a pack (not factory).";

    juce::String err;
    if (! lib.getPacks().contains (pack) && ! lib.createPack (pack, err))
        return err;

    juce::String key;
    if (mode == room::presets::SaveDialog::Mode::save)
    {
        if (meta.author.isEmpty())
            meta.author = "User";
        meta.created = juce::Time::getCurrentTime();
        if (! lib.save (proc.capturePreset (meta), pack, overwrite, key, err))
            return err;
        proc.setCurrentPreset (key, meta.name);
    }
    else
    {
        if (! lib.updateMeta (editingKey, meta, pack, key, err))
            return err;
        if (editingKey == proc.getCurrentPresetKey())
            proc.relabelCurrentPreset (key, meta.name);
    }

    if (browser.isVisible())
        browser.refresh();
    return {};
}
