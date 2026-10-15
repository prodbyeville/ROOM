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
    setTooltip ("drop any sound here to build a room from it. click for the ROOMS library, right-click for more.");
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
        EvilleLookAndFeel::showMenu (m, *this);
        return;
    }
    if (onOpenLibrary)
        onOpenLibrary();
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

    // The room's fingerprint: decay per band over time
    const auto bodyArea = juce::Rectangle<float> (glass.getX(), r.getY(), glass.getWidth(), glass.getBottom() - r.getY());
    if (status != RoomAudioProcessor::Status::recording && status != RoomAudioProcessor::Status::empty)
        paintFingerprint (g, bodyArea, t, status == RoomAudioProcessor::Status::analyzing);

    if (status == RoomAudioProcessor::Status::recording)
    {
        auto meter = bodyArea.reduced (14.0f, 0.0f).withHeight (4.0f).withY (bodyArea.getCentreY());
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
        g.drawFittedText ("(record one, or click for rooms)", bodyArea.reduced (10.0f).toNearestInt(),
                          juce::Justification::centred, 2);
    }

    Hw::drawScreenOverlay (g, glass);
    if (hovering)
    {
        g.setColour (Colours::crimson);
        g.drawRoundedRectangle (glass.reduced (1.0f), 4.5f, 2.0f);
    }
}

void LoadScreen::paintFingerprint (juce::Graphics& g, juce::Rectangle<float> body, float t, bool listening)
{
    // Lows at the bottom, highs at the top, time to the right. Each band glows
    // white, then crimson, and goes dark as it dies away (60 dB down), so every
    // room has its own shape.
    const auto& a = proc.getAnalysis();
    if (! a.valid)
        return;
    const auto nb = a.numBands();
    float maxT = 0.3f;
    for (auto v : a.bandT60s) maxT = std::max (maxT, v);
    const auto span = juce::jlimit (0.6f, 6.0f, maxT * 1.1f);   // seconds across the screen
    const auto dot = juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
    const auto caption = juce::String (a.steady ? "steady" : "a hit") + dot + juce::String (a.durationS, 1) + " s" + dot
                         + (a.origin.isEmpty() ? juce::String ("file") : a.origin.toLowerCase());

    auto inner = body.reduced (12.0f, 8.0f);
    auto cap = inner.removeFromBottom (14.0f);
    auto area = inner.withTrimmedBottom (2.0f);
    const int cols = 40;
    const auto cw = area.getWidth() / (float) cols, rh = area.getHeight() / (float) std::max (1, nb);
    const auto sweep = listening ? std::fmod (t * 0.6f, 1.0f) : 2.0f;   // listening: a column of light runs across
    for (int b = 0; b < nb; ++b)
        for (int c = 0; c < cols; ++c)
        {
            const auto x = ((float) c + 0.5f) / (float) cols;
            const auto v = juce::jlimit (0.0f, 1.0f, 1.0f - span * x / std::max (0.05f, a.bandT60s[(size_t) b]));
            auto col = v > 0.5f ? Colours::crimson.interpolatedWith (Colours::screenText, (v - 0.5f) * 2.0f)
                                : Colours::glass.interpolatedWith (Colours::crimson, v * 2.0f);
            auto alpha = a.bandConfident[(size_t) b] ? 1.0f : 0.55f;   // bands it couldn't measure read fainter
            if (listening)
                alpha *= 0.35f + 0.65f * std::exp (-std::pow ((x - sweep) * 9.0f, 2.0f));
            g.setColour (col.withAlpha (alpha));
            g.fillRect (juce::Rectangle<float> (area.getX() + (float) c * cw, area.getBottom() - (float) (b + 1) * rh, cw, rh).reduced (0.6f));
        }
    g.setFont (Fonts::mono (10.5f));
    g.setColour (Colours::screenDim);
    g.drawText (caption, cap, juce::Justification::centredLeft);
    g.drawText (juce::String (span, 1) + " s", cap, juce::Justification::centredRight);
}

//==============================================================================
// Room screen

RoomScreen::RoomScreen (RoomAudioProcessor& p) : proc (p)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip ("the room in 3d: space sets its width, size its depth, and the floor shows how each band dies away. click for its numbers.");
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
    if (! frozen)
        yaw += 0.0035f;   // the box turns slowly (about 6 degrees a second); freeze holds it

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

    // --- the room: a box in 3D, turning slowly. space = width, size = depth and height ---
    const auto hw = 0.55f + 0.45f * juce::jlimit (0.0f, 1.0f, space.norm);
    const auto hd = 0.50f + 0.60f * juce::jlimit (0.0f, 1.0f, size.norm);
    const auto hh = 0.38f + 0.25f * juce::jlimit (0.0f, 1.0f, size.norm);
    const auto pitch = 0.42f, dist = 7.0f;
    const auto cy = std::cos (yaw), sy = std::sin (yaw), cp = std::cos (pitch), sp = std::sin (pitch);
    const auto view = glass.reduced (10.0f).withTrimmedTop (12.0f);
    const auto f = std::min (view.getWidth() * 0.46f / 1.49f, view.getHeight() * 0.46f / (0.63f * cp + 1.49f * sp)) * dist;
    const juce::Point<float> centre (view.getCentreX(), view.getCentreY() + view.getHeight() * 0.04f);
    struct P { juce::Point<float> xy; float depth; };
    auto project = [&] (float x, float y, float z)
    {
        const auto x1 = x * cy - z * sy, z1 = x * sy + z * cy;
        const auto y2 = y * cp - z1 * sp, z2 = y * sp + z1 * cp;
        const auto d = dist - z2;
        return P { { centre.x + f * x1 / d, centre.y - f * y2 / d }, z2 };
    };
    const auto duckDim = 1.0f - 0.6f * duck.norm * levelSmooth;
    const auto glow = 0.3f + 0.7f * juce::jlimit (0.0f, 1.0f, intensity.norm * 1.4f);

    // The floor the box stands on: a faint grid that turns with it.
    {
        juce::Path grid;
        for (int i = -4; i <= 4; ++i)
        {
            const auto t = 1.7f * (float) i / 4.0f;
            grid.addLineSegment ({ project (t, -hh, -1.7f).xy, project (t, -hh, 1.7f).xy }, 0.0f);
            grid.addLineSegment ({ project (-1.7f, -hh, t).xy, project (1.7f, -hh, t).xy }, 0.0f);
        }
        juce::ColourGradient fade (Colours::screenText.withAlpha (0.07f), centre.x, centre.y,
                                   Colours::screenText.withAlpha (0.0f), centre.x + view.getWidth() * 0.5f, centre.y, true);
        g.setGradientFill (fade);
        g.strokePath (grid, juce::PathStrokeType (0.8f));
    }

    // The box: twelve edges, the far ones dimmer.
    const float xs[2] = { -hw, hw }, ys[2] = { -hh, hh }, zs[2] = { -hd, hd };
    const auto wallAlpha = (valid ? 0.45f + 0.45f * balance : 0.3f) * duckDim * (0.6f + 0.4f * glow);
    auto edge = [&] (P a, P b)
    {
        const auto near = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * (a.depth + b.depth) * 0.5f / std::max (hw, hd));
        juce::Path e;
        e.startNewSubPath (a.xy);
        e.lineTo (b.xy);
        Hw::strokeGlow (g, e, Colours::screenText.withAlpha (wallAlpha * (0.35f + 0.65f * near)), 1.1f, 0.35f + 0.5f * near * glow);
    };
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
        {
            edge (project (xs[0], ys[i], zs[j]), project (xs[1], ys[i], zs[j]));
            edge (project (xs[i], ys[0], zs[j]), project (xs[i], ys[1], zs[j]));
            edge (project (xs[i], ys[j], zs[0]), project (xs[i], ys[j], zs[1]));
        }

    // --- the decay per band, laid across the floor of the box, front to back ---
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

        const auto gap = 0.22f * preDelay.norm;                      // pre-delay: the floor starts further back
        const auto reach = 0.3f + 0.7f * tail.norm;                  // tail: how far back the decay reaches
        const auto dark = damping.norm - 0.5f;                       // damping: darker / brighter red
        const auto baseCol = dark > 0.0f ? Colours::crimson.interpolatedWith (juce::Colour (0xff5e1010), dark * 1.4f)
                                         : Colours::crimson.interpolatedWith (juce::Colour (0xffff8a7a), -dark * 1.2f);
        auto zAt = [&] (float t) { return hd - 2.0f * hd * juce::jlimit (0.0f, 1.0f, t); };   // t: 0 front, 1 back

        // Warm haze on the floor where the decay lives
        {
            const auto z0 = zAt (gap), z1 = zAt (gap + reach);
            juce::Path floor;
            floor.startNewSubPath (project (-hw, -hh, z0).xy);
            floor.lineTo (project (hw, -hh, z0).xy);
            floor.lineTo (project (hw, -hh, z1).xy);
            floor.lineTo (project (-hw, -hh, z1).xy);
            floor.closeSubPath();
            g.setColour (baseCol.withAlpha (0.10f * glow * duckDim));
            g.fillPath (floor);
        }

        for (int b = 0; b < nb; ++b)
        {
            const auto t = gap + reach * ((float) b + 0.6f) / (float) nb;
            const auto z = zAt (t);
            auto a = (0.25f + 0.75f * decay[(size_t) b] / maxT) * glow * duckDim;
            a *= 1.0f - std::max (0.0f, dark) * 1.3f * t;            // dark rooms lose their highs early
            if (frozen)
                a *= 0.75f + 0.25f * std::sin (phase * 3.0f);
            else
                a *= 0.86f + 0.14f * std::sin (phase * 2.0f + (float) b * 0.8f);
            a += 0.8f * std::exp (-std::pow (t - pulsePos, 2.0f) / 0.004f) * glow;   // the pulse from incoming sound
            if (! analysis.bandConfident[(size_t) b])
                a *= 0.6f;
            juce::Path line;
            line.startNewSubPath (project (-hw * 0.94f, -hh, z).xy);
            line.lineTo (project (hw * 0.94f, -hh, z).xy);
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
    setTooltip ("the tone this room puts on a sound. the red dot is the note it rings at most.");
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
    auto r = getLocalBounds().toFloat();
    const auto latched = icon == Icon::advanced && getToggleState();   // open: the key stays down, its legend lit
    down = down || latched;
    Hw::drawKey (g, r, true, over, down);
    r = Hw::keyCap (r).translated (0.0f, down ? 0.6f : 0.0f);
    if (badge)
    {
        const auto d = juce::Point<float> (r.getRight() - 7.0f, r.getY() + 7.0f);
        g.setColour (Colours::crimson.withAlpha (0.3f));
        g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (d));
        g.setColour (Colours::crimson);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (d));
    }
    const auto c = r.getCentre().translated (0.0f, -0.5f);
    const auto s = std::min (r.getWidth(), r.getHeight()) * 0.5f;
    g.setColour (Colours::ink.withAlpha (0.88f));
    juce::PathStrokeType stroke (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    switch (icon)
    {
        case Icon::record:
        {
            const auto on = getToggleState();
            g.drawEllipse (juce::Rectangle<float> (s * 1.1f, s * 1.1f).withCentre (c), 1.8f);
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
        case Icon::advanced:
        {
            // the unit with its side section: a frame with the right part filled.
            // Open: the legend is backlit crimson, like the bypass key.
            const auto box = juce::Rectangle<float> (s * 1.3f, s * 0.95f).withCentre (c);
            const auto side = box.withTrimmedLeft (box.getWidth() * 0.64f).reduced (2.5f);
            if (latched)
            {
                g.setColour (Colours::crimson.withAlpha (0.25f));
                g.drawRoundedRectangle (box, 2.0f, 4.0f);
                g.fillRoundedRectangle (side.expanded (2.0f), 2.0f);
                g.setColour (Colours::crimson.brighter (0.25f));
            }
            g.drawRoundedRectangle (box, 2.0f, 1.5f);
            g.fillRoundedRectangle (side, 1.0f);
            return;
        }
        case Icon::menu:
        {
            for (int i = -1; i <= 1; ++i)
                p.addLineSegment ({ c.x - s * 0.6f, c.y + (float) i * s * 0.38f, c.x + s * 0.6f, c.y + (float) i * s * 0.38f }, 0.0f);
            break;
        }
        case Icon::randomize:
        {
            // two arrows chasing each other round a circle
            const auto rr = s * 0.52f;
            const auto pi = juce::MathConstants<float>::pi;
            for (int k = 0; k < 2; ++k)
            {
                const auto a0 = (float) k * pi + 0.35f, a1 = a0 + pi - 0.7f;
                p.addCentredArc (c.x, c.y, rr, rr, 0.0f, a0, a1, true);
                const juce::Point<float> tip (c.x + rr * std::sin (a1), c.y - rr * std::cos (a1));
                const auto tangent = a1 + pi * 0.5f;   // direction of travel at the tip
                for (auto side : { -1.0f, 1.0f })
                {
                    const auto back = tangent + pi + side * 0.55f;
                    p.startNewSubPath (tip);
                    p.lineTo (tip.x + s * 0.24f * std::sin (back), tip.y - s * 0.24f * std::cos (back));
                }
            }
            break;
        }
    }
    g.strokePath (p, stroke);
}

void BypassSwitch::paintButton (juce::Graphics& g, bool over, bool down)
{
    // Lit crimson while ROOM is running, dark when bypassed.
    Hw::drawGlowKey (g, getLocalBounds().toFloat(), ! getToggleState(), over, down);
}

//==============================================================================
// First-run tour

TourOverlay::TourOverlay()
{
    setInterceptsMouseClicks (true, true);
    for (auto* b : { &back, &next })
    {
        b->getProperties().set ("key", "dark");
        addAndMakeVisible (b);
    }
    skip.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    skip.setColour (juce::TextButton::textColourOffId, Colours::screenDim);
    addAndMakeVisible (skip);
    back.onClick = [this] { showStep (step - 1); };
    next.onClick = [this] { showStep (step + 1); };
    skip.onClick = [this] { setVisible (false); if (onFinished) onFinished(); };
}

void TourOverlay::start (std::vector<Step> s)
{
    steps = std::move (s);
    setVisible (true);
    toFront (false);
    showStep (0);
}

void TourOverlay::showStep (int i)
{
    if (i >= (int) steps.size())
    {
        setVisible (false);
        if (onFinished) onFinished();
        return;
    }
    step = juce::jlimit (0, (int) steps.size() - 1, i);
    back.setVisible (step > 0);
    next.setButtonText (step == (int) steps.size() - 1 ? "done" : "next");
    resized();
    repaint();
}

juce::Rectangle<int> TourOverlay::cardArea() const
{
    if (steps.empty())
        return {};
    const auto spot = steps[(size_t) step].spot;
    const int w = 330, h = 168;
    // below the spotlight if there's room, otherwise above it
    auto y = spot.getBottom() + 18;
    if (y + h > getHeight() - 10)
        y = spot.getY() - 18 - h;
    const auto x = juce::jlimit (12, getWidth() - w - 12, spot.getCentreX() - w / 2);
    return { x, juce::jlimit (12, getHeight() - h - 12, y), w, h };
}

void TourOverlay::resized()
{
    auto c = cardArea().reduced (20, 16);
    auto row = c.removeFromBottom (32);
    next.setBounds (row.removeFromRight (80));
    row.removeFromRight (8);
    back.setBounds (row.removeFromRight (80));
    skip.setBounds (row.removeFromLeft (60));
}

void TourOverlay::paint (juce::Graphics& g)
{
    if (steps.empty())
        return;
    const auto& st = steps[(size_t) step];
    // Dim everything but the spotlight.
    juce::Path dim;
    dim.addRectangle (getLocalBounds().toFloat());
    dim.addRoundedRectangle (st.spot.toFloat().expanded (6.0f), 10.0f);
    dim.setUsingNonZeroWinding (false);
    g.setColour (juce::Colours::black.withAlpha (0.58f));
    g.fillPath (dim);
    g.setColour (Colours::crimson.withAlpha (0.85f));
    g.drawRoundedRectangle (st.spot.toFloat().expanded (6.0f), 10.0f, 1.5f);

    // The card: black glass, like the dialogs
    const auto card = cardArea().toFloat();
    for (int i = 0; i < 6; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.06f));
        g.fillRoundedRectangle (card.translated (0.0f, 3.0f).expanded ((float) i * 1.4f), 10.0f + (float) i * 1.4f);
    }
    juce::ColourGradient body (juce::Colour (0xff1d1d1d), 0.0f, card.getY(), juce::Colour (0xff0e0e0e), 0.0f, card.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (card, 10.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawLine (card.getX() + 10.0f, card.getY() + 1.5f, card.getRight() - 10.0f, card.getY() + 1.5f, 1.0f);

    auto r = card.reduced (20.0f, 16.0f);
    auto top = r.removeFromTop (22.0f);
    g.setFont (Fonts::sans (19.0f, Fonts::bold));
    g.setColour (Colours::dustWhite);
    g.drawText (st.title, top, juce::Justification::centredLeft);
    g.setFont (Fonts::mono (11.0f));
    g.setColour (Colours::screenDim);
    g.drawText (juce::String (step + 1) + " of " + juce::String ((int) steps.size()), top, juce::Justification::centredRight);
    r.removeFromTop (8.0f);
    r.removeFromBottom (40.0f);
    g.setFont (Fonts::sans (14.5f));
    g.setColour (Colours::dustWhite.withAlpha (0.88f));
    g.drawFittedText (st.text, r.toNearestInt(), juce::Justification::topLeft, 5, 1.0f);
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

    paperTexture = Hw::asset ("tex_paper.jpg");    // painted steel and powder coat, made in Higgsfield
    blackTexture = Hw::asset ("tex_black.jpg");

    // --- Top bar ---
    content.addAndMakeVisible (presetBar);
    content.addAndMakeVisible (randomButton);
    randomButton.onClick = [this] { proc.randomizeKnobs (random); };
    randomButton.onRightClick = [this] { showRandomizeMenu(); };
    content.addAndMakeVisible (menuButton);
    menuButton.onClick = [this] { showMainMenu(); };
    updates.onResult = [this] { menuButton.setBadge (updates.isUpdateAvailable()); };
    menuButton.setBadge (updates.isUpdateAvailable());
    content.addAndMakeVisible (advancedButton);
    advancedButton.onClick = [this] { setAdvancedOpen (! proc.advancedOpen); };
    content.addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::bypass, bypassButton);
    setupKnob (mix, ParamIDs::mix, "wet", 15.0f, "", "", "", false,
              "how much of the room you hear against your dry sound.");
    setupKnob (output, ParamIDs::output, "out", 15.0f, "", "", "", false,
              "the final level, after everything.");

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
    setupKnob (intensity, ParamIDs::intensity, "intensity", 37.0f, "0", "100", "200", false,
              "how much of the room you hear: its decay, tone and resonances. 100 is the room as measured.");
    setupKnob (balance, ParamIDs::balance, "balance", 37.0f, "tone", "", "space", false,
              "tone vs space. left keeps the room's colour (its eq and rings), right keeps its sense of space (decay and width). auto uses what room detected.");
    setupKnob (tail, ParamIDs::tail, "tail", 37.0f, "0.3", "", "6 s", false,
              "how long the room rings out.");
    setupKnob (space, ParamIDs::space, "space", 37.0f, "mono", "", "wide", false,
              "how wide the room sounds, from mono to wide.");
    autoBalance.getProperties().set ("style", "lightkey");
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
    setupKnob (tape, ParamIDs::tape, "tape", 28.0f, "0", "", "100", true,
              "warm tape saturation on the way out.");
    setupKnob (autoEq, ParamIDs::autoEq, "auto eq", 28.0f, "0", "", "100", true,
              "gently evens out the tone so the room sits in the mix.");

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

    setTipsEnabled (proc.tipsEnabled);

    content.addChildComponent (tour);
    tour.onFinished = []
    {
        if (auto s = UpdateChecker::openSettings())
        {
            s->setValue ("tourDone", true);
            s->saveIfNeeded();
        }
    };

    // Resizable, keeping the shape: everything scales together.
    setResizable (true, true);
    setAdvancedOpen (proc.advancedOpen);

    startTimerHz (15);
}

RoomAudioProcessorEditor::~RoomAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void RoomAudioProcessorEditor::setupKnob (Knob& k, const juce::String& id, const juce::String& name, float radius,
                                          const juce::String& lo, const juce::String& mid, const juce::String& hi, bool onBlack,
                                          const juce::String& tip)
{
    k.slider.setTooltip (tip);
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

void RoomAudioProcessorEditor::setTipsEnabled (bool on)
{
    proc.tipsEnabled = on;
    if (on && tooltips == nullptr)
        tooltips = std::make_unique<juce::TooltipWindow> (this, 700);
    else if (! on)
        tooltips.reset();
}

void RoomAudioProcessorEditor::newPreset()
{
    proc.resetToInit();
    for (auto* k : { &intensity, &balance, &tail, &space, &mix, &output, &tape, &autoEq })
        k->slider.refreshModulation();
    advancedPanel.refreshModulation();
    content.repaint();
}

void RoomAudioProcessorEditor::showTour (int startStep)
{
    const auto knobs = intensity.slider.getBounds().getUnion (space.slider.getBounds()).getUnion (autoBalance.getBounds())
                           .withTop (intensity.slider.getY());
    tour.setBounds (0, 0, contentWidth(), baseHeight);
    tour.start ({
        { bays[0], "load",
          "drop any sound here, or press record and play one. room listens for the space inside it: how it decays, the tone it adds, the notes that ring." },
        { knobs, "shape",
          "intensity is how much of that room you hear. balance leans to its tone or its space, tail sets how long it rings, space how wide." },
        { finishArea.toNearestInt().reduced (14, 10), "finish",
          "eq, tape and auto eq polish the result. advanced (top right) has pre-delay, size, ducking, freeze and two lfos." } });
    tour.showStep (startStep);
}

void RoomAudioProcessorEditor::showMainMenu()
{
    const auto version = UpdateChecker::currentVersion();
    juce::PopupMenu m;
    m.addItem ("new preset", [this] { newPreset(); });
    m.addSeparator();
    m.addItem ("tips", true, areTipsEnabled(), [this] { setTipsEnabled (! areTipsEnabled()); });
    m.addItem ("show the tour", [this] { showTour(); });
    m.addSeparator();
    if (updates.isUpdateAvailable())
    {
        juce::PopupMenu::Item item (juce::String (juce::CharPointer_UTF8 ("update available \xc2\xb7 v")) + updates.getLatestVersion());
        item.colour = Colours::crimson.brighter (0.3f);
        item.action = [this] { juce::URL (updates.getDownloadUrl()).launchInDefaultBrowser(); };
        m.addItem (std::move (item));
    }
    else
        m.addItem ("check for updates", [this] { juce::URL (UpdateChecker::websiteUrl).launchInDefaultBrowser(); });
    m.addItem ("visit website", [] { juce::URL (UpdateChecker::websiteUrl).launchInDefaultBrowser(); });
    m.addItem ("get support", [version]
    {
        juce::URL ("mailto:" + juce::String (UpdateChecker::supportEmail) + "?subject=room%20v" + version).launchInDefaultBrowser();
    });
    m.addItem (juce::String (juce::CharPointer_UTF8 ("about room\xe2\x80\xa6")), [this] { showAbout(); });
    m.addSeparator();
    m.addItem ("room v" + version, false, false, nullptr);
    EvilleLookAndFeel::showMenu (m, menuButton);
}

void RoomAudioProcessorEditor::showAbout()
{
    const auto dot = juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 "));
    const auto message = "by eville" + dot + "v" + UpdateChecker::currentVersion() + "\n\n"
        "every sound carries the space it was made in. room pulls that space out and lets you put anything else inside it.\n\n"
        "drop in any sound (a clap in a stairwell, a voice memo, a tap on a glass bottle, a few seconds of room tone) "
        "or record one. room listens for the space inside it: how it decays in every band, the tone it puts on things, "
        "the notes that ring. then it rebuilds that space so your own sounds can live in it.\n\n"
        "shape it (how much of the room, tone vs space, tail, width), finish it (eq, tape, auto eq), "
        "and keep it: a ROOM is just the space, a STUDIO is the whole sound.\n\n"
        + juce::String (juce::CharPointer_UTF8 ("\xc2\xa9 ")) + juce::String (juce::Time::getCurrentTime().getYear()) + " eville" + dot
        + "prodbyeville.com" + dot + UpdateChecker::supportEmail + "\n"
        "fonts: satoshi (indian type foundry), cormorant garamond (sil open font license)";
    EvilleLookAndFeel::showDialog (content, "room", message, { "close", "visit website" }, {},
                                   [] (int result, const juce::String&)
                                   {
                                       if (result == 2)
                                           juce::URL (UpdateChecker::websiteUrl).launchInDefaultBrowser();
                                   });
}

void RoomAudioProcessorEditor::setAdvanced (bool showLfos)
{
    if (showLfos && ! proc.advancedOpen)
        setAdvancedOpen (true);
    advancedPanel.showModulationTab (showLfos);
}

void RoomAudioProcessorEditor::setAdvancedOpen (bool open)
{
    // Keep the current scale; the window gets wider or narrower around the unit.
    const auto scale = juce::jlimit (0.6f, 1.4f, getHeight() > 0 ? (float) getHeight() / (float) baseHeight : proc.editorScale);
    proc.advancedOpen = open;
    advancedButton.setToggleState (open, juce::dontSendNotification);
    advancedPanel.setVisible (open);
    if (! open)
        advancedPanel.showModulationTab (false);
    const auto w = contentWidth();
    content.setBounds (0, 0, w, baseHeight);
    staticLayer = {};
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) w / baseHeight);
    setResizeLimits (w * 3 / 5, baseHeight * 3 / 5, w * 7 / 5, baseHeight * 7 / 5);
    setSize (juce::roundToInt ((float) w * scale), juce::roundToInt ((float) baseHeight * scale));
    content.repaint();
}

void RoomAudioProcessorEditor::timerCallback()
{
    if (ticksUntilUpdateCheck > 0 && --ticksUntilUpdateCheck == 0)
        updates.checkIfDue();
    if (ticksUntilTour > 0 && --ticksUntilTour == 0 && autoShowTour)
        if (auto settings = UpdateChecker::openSettings(); settings != nullptr && ! settings->getBoolValue ("tourDone", false))
            showTour();

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
// Painting: the desk, the unit sitting on it, the print on the console

void RoomAudioProcessorEditor::paintContent (juce::Graphics& g)
{
    // Everything that doesn't move is drawn once into an image at the window's
    // real pixel size (the soft shadows are expensive), then reused.
    const auto scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! staticLayer.isValid() || std::abs (scale - staticScale) > 0.001f)
    {
        staticScale = scale;
        staticLayer = juce::Image (juce::Image::RGB, std::max (1, juce::roundToInt ((float) contentWidth() * scale)),
                                   std::max (1, juce::roundToInt (baseHeight * scale)), true);
        juce::Graphics sg (staticLayer);
        sg.addTransform (juce::AffineTransform::scale (scale));
        paintUnit (sg);
    }
    g.drawImage (staticLayer, juce::Rectangle<float> (0.0f, 0.0f, (float) contentWidth(), (float) baseHeight));

    // The power LED on finish follows bypass.
    Hw::drawLed (g, finishLed, ! (proc.apvts.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f), 4.5f);
}

void RoomAudioProcessorEditor::paintUnit (juce::Graphics& g)
{
    const auto open = proc.advancedOpen;
    const auto unit = open ? consoleArea.getUnion (advancedArea).getUnion (finishArea) : consoleArea.getUnion (finishArea);

    // --- the three panels fill the window ---
    g.fillAll (juce::Colour (0xff161616));
    g.saveState();
    auto panel = [&] (juce::Rectangle<float> r, bool black)
    {
        g.setColour (black ? juce::Colour (0xff161616) : Colours::paper);
        g.fillRect (r);
        if (const auto& tex = black ? blackTexture : paperTexture; tex.isValid())
        {
            // One piece of the photo per panel (no tiling, so no seams), kept
            // subtle: fine powder coat, satin paint.
            const auto src = juce::Rectangle<int> (std::min (tex.getWidth(), (int) r.getWidth()), std::min (tex.getHeight(), (int) r.getHeight()));
            g.setOpacity (black ? 0.30f : 0.55f);
            g.drawImage (tex, r.getX(), r.getY(), r.getWidth(), r.getHeight(), src.getX(), src.getY(), src.getWidth(), src.getHeight());
            g.setOpacity (1.0f);
        }
        // light falls off a touch toward the bottom
        juce::ColourGradient shade (juce::Colours::white.withAlpha (black ? 0.035f : 0.09f), 0.0f, unit.getY(),
                                    juce::Colours::black.withAlpha (black ? 0.22f : 0.05f), 0.0f, unit.getBottom(), false);
        g.setGradientFill (shade);
        g.fillRect (r);
        // the panel's edges: lit along the top and left, in shade at the bottom and right
        g.setColour (juce::Colours::white.withAlpha (black ? 0.09f : 0.8f));
        g.fillRect (r.withHeight (1.0f));
        g.fillRect (r.withWidth (1.0f));
        g.setColour (juce::Colours::black.withAlpha (black ? 0.6f : 0.16f));
        g.fillRect (r.withTop (r.getBottom() - 1.0f));
        g.fillRect (r.withLeft (r.getRight() - 1.0f));
    };
    panel (consoleArea, false);
    panel (finishArea, true);
    if (open)
        panel (advancedArea, true);

    // The joints between the panels: a dark hairline gap.
    g.setColour (juce::Colour (0xff050505));
    g.fillRect (juce::Rectangle<float> (consoleArea.getX(), consoleArea.getBottom() - 0.5f, consoleArea.getWidth(), 1.5f));
    if (open)
        g.fillRect (juce::Rectangle<float> (advancedArea.getX() - 0.5f, unit.getY(), 1.5f, unit.getHeight()));
    g.restoreState();

    paintConsoleArt (g);

    // Screws: four on each panel, each at its own angle.
    for (auto r : { consoleArea, advancedArea, finishArea })
    {
        if (r == advancedArea && ! open)
            continue;
        const auto black = r != consoleArea;
        const auto in = r.reduced (15.0f);
        for (auto p : { in.getTopLeft(), in.getTopRight(), in.getBottomLeft(), in.getBottomRight() })
            Hw::drawScrew (g, p, black, 13.0f);
    }

    // Finish: the out jack and the power light's label
    Hw::drawJack (g, finishJack, 34.0f);
    g.setColour (Colours::dustWhite);
    g.setFont (Fonts::sans (13.0f));
    g.drawText ("out", juce::Rectangle<float> (60.0f, 18.0f).withCentre (finishJack.translated (0.0f, 34.0f)), juce::Justification::centred);
    g.drawText ("on", juce::Rectangle<float> (60.0f, 18.0f).withCentre (finishLed.translated (0.0f, -20.0f)), juce::Justification::centred);
}

void RoomAudioProcessorEditor::paintConsoleArt (juce::Graphics& g)
{
    const auto c = consoleArea;

    // The logo (Ethan's artwork), the height of the top-bar keys
    if (auto logo = Hw::asset ("logo.png"); logo.isValid())
    {
        const auto h = 42.0f, w = h * (float) logo.getWidth() / (float) logo.getHeight();
        g.setOpacity (1.0f);
        g.drawImage (logo, juce::Rectangle<float> (w, h).withPosition (c.getX() + 86.0f, c.getY() + 31.0f),
                     juce::RectanglePlacement::stretchToFit);
    }

    // Tracking marks: very faint line art printed on the panel, like a motion
    // tracker following the room. Hairline dashes, small nodes, corner brackets
    // with the place they sit on the panel. Sparse on purpose.
    {
        const auto line = Colours::ink.withAlpha (0.16f);
        const float dashes[] = { 4.0f, 5.0f };
        auto dashed = [&] (const juce::Path& path)
        {
            juce::Path d;
            juce::PathStrokeType (0.8f).createDashedStroke (d, path, dashes, 2);
            g.setColour (line);
            g.fillPath (d);
        };
        auto node = [&] (float x, float y)
        {
            g.setColour (Colours::ink.withAlpha (0.28f));
            g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
        };
        auto corners = [&] (juce::Rectangle<float> r, float arm)
        {
            juce::Path p;
            for (auto [pt, dx, dy] : { std::tuple { r.getTopLeft(), 1.0f, 1.0f }, { r.getTopRight(), -1.0f, 1.0f },
                                       { r.getBottomLeft(), 1.0f, -1.0f }, { r.getBottomRight(), -1.0f, -1.0f } })
            {
                p.startNewSubPath (pt.x + dx * arm, pt.y);
                p.lineTo (pt);
                p.lineTo (pt.x, pt.y + dy * arm);
            }
            g.setColour (Colours::ink.withAlpha (0.30f));
            g.strokePath (p, juce::PathStrokeType (0.9f));
        };
        auto tag = [&] (juce::Rectangle<float> r, bool twoLines)
        {
            corners (r, 5.0f);
            g.setFont (Fonts::mono (8.0f));
            g.setColour (Colours::ink.withAlpha (0.38f));
            const auto xs = "x: " + juce::String (juce::roundToInt (r.getCentreX())), ys = "y: " + juce::String (juce::roundToInt (r.getCentreY()));
            if (twoLines)
            {
                g.drawText (xs, r.reduced (3.0f, 2.0f).removeFromTop (r.getHeight() * 0.5f - 1.0f), juce::Justification::centredLeft);
                g.drawText (ys, r.reduced (3.0f, 2.0f).removeFromBottom (r.getHeight() * 0.5f - 1.0f), juce::Justification::centredLeft);
            }
            else
                g.drawText (xs + "  " + ys, r.reduced (4.0f, 0.0f), juce::Justification::centredLeft);
        };
        const auto x0 = c.getX(), y0 = c.getY(), w = c.getWidth();
        const auto under = (float) bays[0].getBottom() + 12.0f;   // the band between the bays and the knobs

        // A loose orbit around the panel: in from the left margin, along under the
        // bays, out the right margin; it breaks where it would touch anything.
        {
            juce::Path p;
            p.startNewSubPath (x0 + 12.0f, y0 + 150.0f);
            p.cubicTo (x0 + 6.0f, y0 + 250.0f, x0 + 10.0f, under - 20.0f, x0 + 40.0f, under);
            p.cubicTo (x0 + 200.0f, under + 14.0f, x0 + 330.0f, under - 6.0f, x0 + w * 0.5f, under + 2.0f);
            dashed (p);
            juce::Path q;
            q.startNewSubPath (x0 + w * 0.5f + 40.0f, under + 3.0f);
            q.cubicTo (x0 + w - 300.0f, under + 10.0f, x0 + w - 120.0f, under - 10.0f, x0 + w - 36.0f, under - 2.0f);
            q.cubicTo (x0 + w - 8.0f, under + 6.0f, x0 + w - 8.0f, y0 + 470.0f, x0 + w - 14.0f, y0 + 540.0f);
            dashed (q);
            // the bottom margin, under the knobs
            juce::Path r;
            r.startNewSubPath (x0 + 60.0f, c.getBottom() - 10.0f);
            r.cubicTo (x0 + 240.0f, c.getBottom() - 18.0f, x0 + 300.0f, c.getBottom() - 4.0f, x0 + 380.0f, c.getBottom() - 9.0f);
            dashed (r);
            juce::Path t;   // and a short arc over the top band, right of the logo
            t.startNewSubPath (x0 + 300.0f, y0 + 116.0f);
            t.cubicTo (x0 + 420.0f, y0 + 108.0f, x0 + 520.0f, y0 + 124.0f, x0 + 640.0f, y0 + 114.0f);
            dashed (t);
            for (auto pt : { juce::Point<float> (x0 + 9.0f, y0 + 215.0f), { x0 + 186.0f, under + 7.0f }, { x0 + w * 0.5f, under + 2.0f },
                             { x0 + w - 120.0f, under - 3.0f }, { x0 + w - 11.0f, y0 + 470.0f }, { x0 + 300.0f, c.getBottom() - 7.0f },
                             { x0 + 470.0f, y0 + 115.0f } })
                node (pt.x, pt.y);
        }
        tag ({ x0 + 676.0f, y0 + 108.0f, 92.0f, 14.0f }, false);
        tag ({ x0 + 4.0f, under + 46.0f, 40.0f, 24.0f }, true);
        tag ({ x0 + w - 44.0f, under + 120.0f, 40.0f, 24.0f }, true);
        tag ({ x0 + w - 300.0f, c.getBottom() - 19.0f, 92.0f, 14.0f }, false);

        // The knobs: brackets and crosshairs on a few of them, not all.
        juce::Component* ks[4] = { &intensity.slider, &balance.slider, &tail.slider, &space.slider };
        const bool brackets[4] = { true, false, true, false }, cross[4] = { true, true, false, true };
        for (int i = 0; i < 4; ++i)
        {
            const auto kc = ks[i]->getBounds().toFloat().getCentre();
            if (brackets[i])
                corners (juce::Rectangle<float> (138.0f, 132.0f).withCentre (kc.translated (0.0f, 3.0f)), 9.0f);
            if (cross[i])
            {
                const juce::Point<float> p (kc.x, kc.y - 74.0f);
                g.setColour (Colours::ink.withAlpha (0.32f));
                g.drawLine (p.x - 5.0f, p.y, p.x + 5.0f, p.y, 0.9f);
                g.drawLine (p.x, p.y - 5.0f, p.x, p.y + 5.0f, 0.9f);
            }
        }
        // a faint arc tying the first and last tracked knobs together over the top
        {
            const auto a0 = ks[0]->getBounds().toFloat().getCentre().translated (70.0f, -66.0f);
            const auto a1 = ks[2]->getBounds().toFloat().getCentre().translated (-70.0f, -66.0f);
            juce::Path p;
            p.startNewSubPath (a0);
            p.cubicTo (a0.translated (60.0f, -26.0f), a1.translated (-60.0f, -26.0f), a1);
            dashed (p);
        }
    }

    // Top bar print and the crease under it
    g.setFont (Fonts::sans (13.0f));
    g.setColour (Colours::ink);
    for (auto [b, name] : { std::pair<juce::Component*, const char*> { &randomButton, "randomize" }, { &advancedButton, "advanced" },
                            { &bypassButton, "bypass" } })
        g.drawText (name, juce::Rectangle<int> (70, 16).withCentre ({ b->getBounds().getCentreX(), b->getBottom() + 10 }),
                    juce::Justification::centred);
    Hw::drawGroove (g, { c.getX() + 26.0f, c.getY() + 104.0f }, { c.getRight() - 26.0f, c.getY() + 104.0f }, false);

    // Bay frames pressed into the panel, with their names on the frame line
    const char* names[3] = { "load", "room", "tone" };
    for (int i = 0; i < 3; ++i)
    {
        const auto b = bays[i].toFloat();
        g.setFont (Fonts::sans (16.0f));
        const auto tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), names[i]);
        const auto label = juce::Rectangle<float> (b.getX() + 14.0f, b.getY() - 10.0f, tw + 12.0f, 20.0f);
        g.saveState();
        g.excludeClipRegion (label.toNearestInt());
        Hw::drawGrooveRect (g, b, 8.0f, false);
        g.restoreState();
        g.setColour (Colours::ink);
        g.drawText (names[i], label, juce::Justification::centred);
    }

    // Labels beside the record / save keys
    g.setFont (Fonts::sans (12.5f));
    g.setColour (Colours::ink);
    for (auto* b : { (juce::Component*) &recordButton, (juce::Component*) &roomSaveButton })
        g.drawText (b == &recordButton ? "record" : "save",
                    juce::Rectangle<int> (70, 14).withCentre ({ b->getBounds().getCentreX(), b->getBottom() + 8 }),
                    juce::Justification::centred);

    // The one printed path left: intensity -> balance -> tail -> space,
    // crimson dots with a faint black companion.
    const auto red = Colours::crimson.withAlpha (0.85f);
    const auto blk = Colours::ink.withAlpha (0.45f);
    const auto ky = (float) intensity.slider.getBounds().getCentreY();
    juce::Component* knobs[4] = { &intensity.slider, &balance.slider, &tail.slider, &space.slider };
    for (int i = 0; i < 3; ++i)
    {
        const auto x0 = (float) knobs[i]->getBounds().getCentreX() + 58.0f, x1 = (float) knobs[i + 1]->getBounds().getCentreX() - 58.0f;
        const juce::Point<float> v[4] = { { x0, ky + 4.0f }, { juce::jmap (0.33f, x0, x1), ky + 16.0f }, { juce::jmap (0.66f, x0, x1), ky - 8.0f }, { x1, ky + 4.0f } };
        juce::Path p;   // smooth curve through the points (Catmull-Rom as cubic beziers)
        p.startNewSubPath (v[0]);
        for (int k = 0; k < 3; ++k)
        {
            const auto p0 = v[std::max (0, k - 1)], p1 = v[k], p2 = v[k + 1], p3 = v[std::min (3, k + 2)];
            p.cubicTo (p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
        }
        Hw::drawDottedPath (g, p, red, 5.0f, 1.0f);
        p.applyTransform (juce::AffineTransform::translation (0.0f, 4.0f));
        Hw::drawDottedPath (g, p, blk, 7.0f, 0.6f);
    }
}

void RoomAudioProcessorEditor::resized()
{
    const auto scale = (float) getHeight() / (float) baseHeight;
    content.setTransform (juce::AffineTransform::scale (scale));
    proc.editorScale = scale;
}

void RoomAudioProcessorEditor::layoutContent()
{
    const auto full = juce::Rectangle<int> (0, 0, contentWidth(), baseHeight);
    browser.setBounds (full);
    tour.setBounds (full);
    roomBrowser.setBounds (full);
    saveDialog.setBounds (full);
    roomSaveDialog.setBounds (full);

    const auto c = consoleArea.toNearestInt();
    staticLayer = {};   // the print follows the layout

    // --- Top bar ---
    // Every key up here is the same size; labels sit under them on one line.
    const int key = 44, keyY = c.getY() + 30;
    menuButton.setBounds (c.getX() + 30, keyY, key, key);
    presetBar.setBounds (c.getX() + 252, keyY, 344, key);
    randomButton.setBounds (c.getX() + 606, keyY, key, key);
    advancedButton.setBounds (c.getX() + 658, keyY, key, key);
    for (auto [k, x] : { std::pair { &mix, 744 }, { &output, 796 } })
    {
        k->slider.setBounds (juce::Rectangle<int> (54, 50).withCentre ({ c.getX() + x, keyY + key / 2 }));
        k->label.setBounds (juce::Rectangle<int> (54, 16).withCentre ({ c.getX() + x, keyY + key + 10 }));
    }
    bypassButton.setBounds (c.getX() + 832, keyY, key, key);

    // --- Row 1: three identical bays ---
    const int bayW = 264, bayH = 220, bayY = c.getY() + 128, gap = 20;
    const int bayX = c.getX() + (c.getWidth() - (bayW * 3 + gap * 2)) / 2;
    for (int i = 0; i < 3; ++i)
        bays[i] = { bayX + i * (bayW + gap), bayY, bayW, bayH };
    const auto screenOf = [] (juce::Rectangle<int> bay) { return bay.reduced (14).withTrimmedTop (6); };
    {
        auto s = screenOf (bays[0]);
        // record and save: key on top, its label underneath
        auto keys = s.removeFromBottom (58);
        s.removeFromBottom (4);
        loadScreen.setBounds (s);
        recordButton.setBounds (juce::Rectangle<int> (42, 42).withCentre ({ keys.getX() + keys.getWidth() / 4, keys.getY() + 21 }));
        roomSaveButton.setBounds (juce::Rectangle<int> (42, 42).withCentre ({ keys.getX() + keys.getWidth() * 3 / 4, keys.getY() + 21 }));
    }
    roomScreen.setBounds (screenOf (bays[1]));
    toneScreen.setBounds (screenOf (bays[2]));

    // --- Row 2: the four knobs ---
    // Centred between the bays and the seam: the block runs from the top scale
    // number (ky - 66) to the bottom of "auto" (ky + 104).
    const int kx0 = c.getX() + 28, kw = (c.getWidth() - 56) / 4;
    const int ky = (bays[0].getBottom() + c.getBottom() + 66 - 135) / 2;   // the block runs to the bottom of "auto" (ky + 135)
    Knob* knobs[4] = { &intensity, &balance, &tail, &space };
    for (int i = 0; i < 4; ++i)
    {
        const auto cx = kx0 + kw * i + kw / 2;
        knobs[i]->slider.setBounds (juce::Rectangle<int> (150, 128).withCentre ({ cx, ky }));
        knobs[i]->label.setBounds (juce::Rectangle<int> (140, 22).withCentre ({ cx, ky + 72 }));
    }
    autoBalance.setBounds (juce::Rectangle<int> (58, 54).withCentre ({ kx0 + kw + kw / 2, ky + 108 }));

    // --- Advanced (the right side, full height) ---
    advancedPanel.setBounds (advancedArea.toNearestInt());

    // --- Finish (along the bottom) ---
    const auto f = finishArea.toNearestInt();
    // Finish: one compact row under the console (no title)
    eqPanel.setBounds (f.getX() + 34, f.getY() + 22, 520, f.getHeight() - 44);
    for (auto [k, x] : { std::pair { &tape, 628 }, { &autoEq, 748 } })
    {
        k->slider.setBounds (juce::Rectangle<int> (124, 116).withCentre ({ f.getX() + x, f.getY() + 80 }));
        k->label.setBounds (juce::Rectangle<int> (100, 20).withCentre ({ f.getX() + x, f.getY() + 144 }));
    }
    finishLed = { finishArea.getRight() - 54.0f, finishArea.getY() + 56.0f };
    finishJack = { finishArea.getRight() - 54.0f, finishArea.getY() + 106.0f };
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
        { P::randomFinish,   "tape and auto eq" },
        { P::randomLfos,     "lfo settings  (shape, rate)" },
    };

    juce::PopupMenu menu;
    menu.addSectionHeader ("randomize");
    menu.addItem ("randomize now", proc.randomizeMask != 0, false, [this] { proc.randomizeKnobs (random); });
    menu.addSeparator();
    menu.addSectionHeader ("what moves");
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
    EvilleLookAndFeel::showMenu (menu, randomButton);
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
        EvilleLookAndFeel::showDialog (content, "rooms", "\"" + e->meta.name + "\" couldn't be read.", { "ok" });
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
        EvilleLookAndFeel::showDialog (content, "studios", "\"" + e->meta.name + "\" couldn't be read.", { "ok" });
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
