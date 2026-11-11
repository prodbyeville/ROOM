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
// The stage

LoadScreen::LoadScreen (RoomAudioProcessor& p) : proc (p)
{
    setTooltip ("drop any sound here to build a room from it. click a card to load that room, right-click for more.");
    startTimerHz (30);
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

juce::Rectangle<int> LoadScreen::boxArea() const
{
    const auto b = getLocalBounds();
    return juce::Rectangle<int> (juce::roundToInt (b.getWidth() * 0.30f), juce::roundToInt (b.getHeight() * 0.52f))
               .withCentre ({ b.getCentreX(), juce::roundToInt (b.getHeight() * 0.44f) });
}

juce::StringArray LoadScreen::cardKeys() const
{
    juce::StringArray keys;
    for (auto& c : cards)
        keys.add (c.key);
    return keys;
}

void LoadScreen::refreshCards()
{
    // Favourites first, then the rest by name; the loaded room is always there.
    auto& lib = proc.getRoomLibrary();
    room::presets::PresetLibrary::Query q;
    const auto all = lib.query (q);
    std::vector<int> order;
    for (auto i : all) if (lib.getEntries()[(size_t) i].favorite) order.push_back (i);
    for (auto i : all) if (! lib.getEntries()[(size_t) i].favorite) order.push_back (i);
    const auto current = proc.getCurrentRoomKey();
    std::vector<Card> next;
    auto add = [&] (const room::presets::PresetLibrary::Entry& e)
    {
        for (auto& c : next)
            if (c.key == e.key)
                return;
        next.push_back ({ e.key, e.meta.name, e.tags.decay, e.tags.tone, {}, 0.0f });
    };
    if (auto* e = lib.find (current))
        add (*e);
    for (auto i : order)
    {
        if (next.size() >= 8)
            break;
        add (lib.getEntries()[(size_t) i]);
    }
    cards = std::move (next);
}

void LoadScreen::timerCallback()
{
    if (++libraryStamp % 30 == 0 || cards.empty())
        refreshCards();
    // One lap a minute; it holds still while the mouse is over the stage.
    if (! isMouseOverOrDragging (true) && ! hovering)
        orbitPhase += juce::MathConstants<float>::twoPi / (60.0f * 30.0f);
    repaint();
}

void LoadScreen::mouseMove (const juce::MouseEvent& e)
{
    juce::String key;
    for (auto it = cards.rbegin(); it != cards.rend(); ++it)   // front cards win
        if (it->bounds.contains (e.position)) { key = it->key; break; }
    if (key != hoverKey)
    {
        hoverKey = key;
        setMouseCursor (key.isNotEmpty() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
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
    // Sorted by depth when painted: the last hit is the front-most card.
    const Card* hit = nullptr;
    for (auto& c : cards)
        if (c.bounds.contains (e.position) && (hit == nullptr || c.depth > hit->depth))
            hit = &c;
    if (hit != nullptr && onLoadRoom)
        onLoadRoom (hit->key);
}

namespace
{
    const char* cardTexture (const juce::String& key)
    {
        static const char* names[5] = { "card_red.jpg", "card_plum.jpg", "card_charcoal.jpg", "card_plaster.jpg", "card_rose.jpg" };
        return names[(size_t) (key.hashCode() & 0x7fffffff) % 5];
    }
}

void LoadScreen::paintCard (juce::Graphics& g, const Card& c, float scale, float angle, float alpha, bool loaded, bool hot)
{
    const auto r = c.bounds;
    juce::Graphics::ScopedSaveState save (g);
    g.addTransform (juce::AffineTransform::rotation (angle, r.getCentreX(), r.getCentreY()));
    const auto corner = 7.0f * scale;
    const auto texName = cardTexture (c.key);
    const auto light = juce::String (texName) == "card_plaster.jpg";

    // The card: a crop of its texture, a little shadow under it
    g.setColour (juce::Colours::black.withAlpha (0.35f * alpha));
    g.fillRoundedRectangle (r.translated (0.0f, 3.0f * scale), corner);
    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path shape;
        shape.addRoundedRectangle (r, corner);
        g.reduceClipRegion (shape);
        if (auto tex = Hw::asset (texName); tex.isValid())
        {
            const auto seed = (int) (c.key.hashCode() & 0xffff);
            const auto cw = tex.getWidth() / 2, ch = tex.getHeight() / 2;
            g.setOpacity (alpha);
            g.drawImage (tex, r.getX(), r.getY(), r.getWidth(), r.getHeight(),
                         seed % (tex.getWidth() - cw), (seed / 7) % (tex.getHeight() - ch), cw, ch);
        }
    }
    if (loaded || hot)
    {
        g.setColour ((loaded ? Colours::crimson.brighter (0.15f) : Colours::dustWhite.withAlpha (0.5f)).withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, loaded ? 1.6f : 1.0f);
        if (loaded)
            g.fillRect (r.withWidth (3.0f * scale).reduced (0.0f, corner * 0.6f));
    }

    // Its decay as a hairline with a few dots: steep for short rooms, long for
    // long ones; dark rooms sag at the end, bright ones keep a little lift.
    const auto k = c.decay == "Short" ? 7.0f : c.decay == "Long" ? 1.9f : 3.6f;
    const auto tone = c.tone == "Dark" ? -1.0f : c.tone == "Bright" ? 1.0f : 0.0f;
    const auto plot = r.reduced (r.getWidth() * 0.1f, r.getHeight() * 0.16f);
    auto yAt = [&] (float x)
    {
        auto v = std::exp (-k * x) + tone * 0.08f * std::sin (x * 3.14159f);
        return plot.getBottom() - plot.getHeight() * juce::jlimit (0.0f, 1.0f, v * 0.92f + 0.04f);
    };
    juce::Path curve;
    curve.startNewSubPath (plot.getX(), yAt (0.0f));
    for (int i = 1; i <= 40; ++i)
    {
        const auto x = (float) i / 40.0f;
        curve.lineTo (plot.getX() + plot.getWidth() * x, yAt (x));
    }
    const auto ink = light ? juce::Colour (0xff1c1a18) : Colours::dustWhite;
    g.setColour (ink.withAlpha (0.85f * alpha));
    g.strokePath (curve, juce::PathStrokeType (std::max (0.9f, 1.1f * scale)));
    g.drawLine (plot.getX(), plot.getY(), plot.getX(), plot.getBottom(), std::max (0.6f, 0.7f * scale));
    for (auto x : { 0.0f, 0.38f, 0.62f, 0.86f })
    {
        const auto d = (x == 0.0f ? 3.4f : 3.0f) * scale;
        g.setColour (ink.withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (d * 2.0f, d * 2.0f).withCentre ({ plot.getX() + plot.getWidth() * x, yAt (x) }));
    }

    // The name under the card
    EvilleLookAndFeel::drawCaps (g, c.name, r.withY (r.getBottom() + 5.0f * scale).withHeight (16.0f * scale).expanded (30.0f, 0.0f),
                                 std::max (8.5f, 11.0f * scale), Colours::dustWhite.withAlpha ((hot || loaded ? 1.0f : 0.78f) * alpha),
                                 juce::Justification::centred);
}

void LoadScreen::paintOverChildren (juce::Graphics& g)
{
    if (! hovering)
        return;
    // A file over the stage: a dark veil, a red dashed outline, what happens on drop.
    const auto area = getLocalBounds().toFloat().reduced (2.0f);
    g.setColour (Colours::window.withAlpha (0.82f));
    g.fillRoundedRectangle (area, 10.0f);
    juce::Path outline, dashed;
    outline.addRoundedRectangle (area.reduced (8.0f), 9.0f);
    const float dashes[] = { 8.0f, 6.0f };
    juce::PathStrokeType (1.6f).createDashedStroke (dashed, outline, dashes, 2);
    g.setColour (Colours::crimson.brighter (0.15f));
    g.fillPath (dashed);
    EvilleLookAndFeel::drawCaps (g, "drop to build a room", area, 16.0f, Colours::dustWhite, juce::Justification::centred);
}

void LoadScreen::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto status = proc.getStatus();

    // The module and the colour field: very faint, stronger with intensity.
    g.setColour (Colours::module);
    g.fillRoundedRectangle (bounds, 10.0f);
    {
        juce::Graphics::ScopedSaveState clip (g);
        juce::Path shape;
        shape.addRoundedRectangle (bounds, 10.0f);
        g.reduceClipRegion (shape);
        if (auto field = Hw::asset ("ui_stage.jpg"); field.isValid())
        {
            const auto amount = juce::jlimit (0.0f, 1.0f, liveParam (proc, ParamIDs::intensity).norm);
            g.setOpacity (proc.getAnalysis().valid ? 0.07f + 0.78f * amount * amount : 0.05f);
            const auto fr = juce::Rectangle<float> (bounds.getWidth() * 1.05f, bounds.getWidth() * 1.05f * (float) field.getHeight() / (float) field.getWidth())
                                .withCentre (bounds.getCentre().translated (0.0f, -bounds.getHeight() * 0.04f));
            g.drawImage (field, fr, juce::RectanglePlacement::stretchToFit);
            g.setOpacity (1.0f);
        }
    }
    g.setColour (Colours::moduleEdge);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 10.0f, 1.0f);

    // The cards: a tilted ring around the box, the far side small and faint.
    const auto box = boxArea().toFloat();
    const auto centre = juce::Point<float> (bounds.getCentreX(), box.getCentreY() + box.getHeight() * 0.06f);
    const auto rx = bounds.getWidth() * 0.34f, ry = bounds.getHeight() * 0.30f;
    const auto n = (int) cards.size();
    const auto current = proc.getCurrentRoomKey();
    const auto t = (float) (juce::Time::getMillisecondCounter() % 1000000) * 0.001f;
    for (int i = 0; i < n; ++i)
    {
        auto& c = cards[(size_t) i];
        const auto a = orbitPhase + juce::MathConstants<float>::twoPi * (float) i / (float) std::max (1, n);
        c.depth = std::cos (a);   // 1 = front (bottom), -1 = back (top)
        const auto scale = 0.62f + 0.38f * (c.depth * 0.5f + 0.5f);
        const auto bob = std::sin (t * 0.7f + (float) i * 1.7f) * 3.0f;
        const auto w = 132.0f * scale, h = 84.0f * scale;
        c.bounds = juce::Rectangle<float> (w, h).withCentre ({ centre.x + rx * std::sin (a), centre.y + ry * c.depth + bob - 8.0f });
    }
    std::vector<int> order ((size_t) n);
    for (int i = 0; i < n; ++i) order[(size_t) i] = i;
    std::sort (order.begin(), order.end(), [this] (int x, int y) { return cards[(size_t) x].depth < cards[(size_t) y].depth; });
    for (auto i : order)
    {
        const auto& c = cards[(size_t) i];
        const auto scale = 0.62f + 0.38f * (c.depth * 0.5f + 0.5f);
        const auto angle = 0.10f * std::sin ((float) i * 2.3f + orbitPhase * 0.5f) * (1.0f - 0.5f * (c.depth * 0.5f + 0.5f));
        const auto alpha = 0.45f + 0.55f * (c.depth * 0.5f + 0.5f);
        paintCard (g, c, scale, angle, alpha, c.key == current, c.key == hoverKey);
    }

    // A standing hint: the whole stage takes audio.
    if (status != RoomAudioProcessor::Status::empty)
        EvilleLookAndFeel::drawCaps (g, "drop any audio here to make a room", bounds.reduced (20.0f, 12.0f).removeFromBottom (20.0f).removeFromLeft (300.0f),
                                     9.5f, Colours::textDim.withMultipliedAlpha (0.8f), juce::Justification::centredLeft, Fonts::medium);

    // Along the bottom: the room's name, or what's happening
    auto line = bounds.reduced (20.0f, 10.0f).removeFromBottom (22.0f);
    const auto dot = juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  "));
    switch (status)
    {
        case RoomAudioProcessor::Status::recording:
        {
            EvilleLookAndFeel::drawCaps (g, "recording  " + juce::String (proc.getRecordedSeconds(), 1) + " s  -  press record again to stop",
                                         line, 12.0f, Colours::redLight, juce::Justification::centred);
            auto meter = line.withY (line.getY() - 10.0f).withHeight (3.0f).withSizeKeepingCentre (220.0f, 3.0f);
            g.setColour (Colours::chip);
            g.fillRoundedRectangle (meter, 1.5f);
            const auto lvl = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (proc.getRecordLevel(), -60.0f) + 60.0f) / 60.0f);
            g.setColour (Colours::crimson);
            g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * lvl), 1.5f);
            break;
        }
        case RoomAudioProcessor::Status::analyzing:
            EvilleLookAndFeel::drawCaps (g, "listening...", line, 12.0f, Colours::dustWhite, juce::Justification::centred);
            break;
        case RoomAudioProcessor::Status::empty:
            EvilleLookAndFeel::drawCaps (g, "drop any audio here, or record one, to make a room", line, 12.0f, Colours::dustWhite.withAlpha (0.8f),
                                         juce::Justification::centred);
            break;
        case RoomAudioProcessor::Status::failed:
            EvilleLookAndFeel::drawCaps (g, proc.getStatusMessage(), line, 12.0f, Colours::redLight, juce::Justification::centred);
            break;
        case RoomAudioProcessor::Status::ready:
        {
            const auto& a = proc.getAnalysis();
            juce::String name = proc.getStatusMessage();
            if (auto* e = proc.getRoomLibrary().find (current))
                name = e->meta.name + dot + e->tags.decay + dot + e->tags.tone;
            else if (a.valid)
                name = name + dot + juce::String (a.medianT60, 1) + " s";
            EvilleLookAndFeel::drawCaps (g, name, line, 12.5f, Colours::dustWhite.withAlpha (0.92f), juce::Justification::centred);
            break;
        }
    }
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
    // Transparent: the box floats on the stage, in plain hairlines (no glow).
    const auto glass = getLocalBounds().toFloat();
    if (showInfo)
    {
        g.setColour (Colours::window.withAlpha (0.86f));
        g.fillRoundedRectangle (glass, 8.0f);
        g.setColour (Colours::moduleEdge);
        g.drawRoundedRectangle (glass.reduced (0.5f), 8.0f, 1.0f);
        paintInfo (g, glass.reduced (6.0f));
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
        juce::ColourGradient fade (Colours::screenText.withAlpha (0.16f), centre.x, centre.y,
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
        g.setColour (Colours::dustWhite.withAlpha (juce::jlimit (0.0f, 1.0f, (wallAlpha + 0.25f) * (0.45f + 0.55f * near))));
        g.strokePath (e, juce::PathStrokeType (1.1f));
    };
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j)
        {
            edge (project (xs[0], ys[i], zs[j]), project (xs[1], ys[i], zs[j]));
            edge (project (xs[i], ys[0], zs[j]), project (xs[i], ys[1], zs[j]));
            edge (project (xs[i], ys[j], zs[0]), project (xs[i], ys[j], zs[1]));
        }
    // A doorway in the back wall
    {
        const auto z = -hd;
        const auto x0 = hw * 0.25f, x1 = hw * 0.55f, top = -hh + 2.0f * hh * 0.62f;
        juce::Path door;
        door.startNewSubPath (project (x0, -hh, z).xy);
        door.lineTo (project (x0, top, z).xy);
        door.lineTo (project (x1, top, z).xy);
        door.lineTo (project (x1, -hh, z).xy);
        g.setColour (Colours::dustWhite.withAlpha (0.55f * duckDim));
        g.strokePath (door, juce::PathStrokeType (1.0f));
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
            g.setColour (baseCol.withAlpha (0.08f * glow * duckDim));
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
            g.setColour (baseCol.brighter (0.2f).withAlpha (juce::jlimit (0.0f, 1.0f, a)));
            g.strokePath (line, juce::PathStrokeType (1.4f));
        }

        juce::ignoreUnused (median);
    }
    else
    {
        juce::ignoreUnused (glass);
    }
    if (frozen)
    {
        EvilleLookAndFeel::drawCaps (g, "freeze", glass.withHeight (16.0f), 10.5f, Colours::redLight, juce::Justification::centred);
    }
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
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const auto latched = (icon == Icon::advanced || icon == Icon::record) && getToggleState();
    const auto ink = Colours::dustWhite.withAlpha (isEnabled() ? (over ? 1.0f : 0.88f) : 0.35f);

    if (chipText.isNotEmpty())
    {
        EvilleLookAndFeel::drawChip (g, r, latched, over, down);
        auto text = r;
        if (icon == Icon::record)
        {
            const auto on = getToggleState();
            const auto blink = ! on || (juce::Time::getMillisecondCounter() / 450) % 2 == 0;
            const auto dot = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ r.getX() + 18.0f, r.getCentreY() });
            g.setColour (Colours::crimson.brighter (0.2f).withAlpha (blink ? 1.0f : 0.4f));
            g.fillEllipse (dot);
            text.removeFromLeft (26.0f);
        }
        EvilleLookAndFeel::drawCaps (g, chipText, text, 11.5f, latched ? Colours::redLight : ink,
                                     icon == Icon::record ? juce::Justification::centredLeft : juce::Justification::centred);
        return;
    }

    if (icon != Icon::menu)
        EvilleLookAndFeel::drawChip (g, r, latched, over, down);
    if (badge)
    {
        const auto d = juce::Point<float> (r.getRight() - 4.0f, r.getY() + 4.0f);
        g.setColour (Colours::crimson.brighter (0.2f));
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (d));
    }
    const auto c = r.getCentre();
    const auto s = std::min (r.getWidth(), r.getHeight()) * 0.5f;
    g.setColour (ink);
    juce::PathStrokeType stroke (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    switch (icon)
    {
        case Icon::record:
            g.fillEllipse (juce::Rectangle<float> (s * 0.5f, s * 0.5f).withCentre (c));
            return;
        case Icon::save:
        {
            // a floppy, like WARP's save
            const auto box = juce::Rectangle<float> (s * 1.05f, s * 1.05f).withCentre (c);
            p.addRoundedRectangle (box, 2.0f);
            p.addRectangle (box.withTrimmedLeft (box.getWidth() * 0.25f).withTrimmedRight (box.getWidth() * 0.25f)
                                .withHeight (box.getHeight() * 0.3f));
            p.addRectangle (box.reduced (box.getWidth() * 0.2f, 0.0f).withTrimmedTop (box.getHeight() * 0.58f).withTrimmedBottom (box.getHeight() * 0.1f));
            break;
        }
        case Icon::advanced:
        {
            const auto box = juce::Rectangle<float> (s * 1.3f, s * 0.95f).withCentre (c);
            g.setColour (latched ? Colours::redLight : ink);
            g.drawRoundedRectangle (box, 2.0f, 1.5f);
            g.fillRoundedRectangle (box.withTrimmedLeft (box.getWidth() * 0.64f).reduced (2.5f), 1.0f);
            return;
        }
        case Icon::menu:
        {
            for (int i = -1; i <= 1; ++i)
                p.addLineSegment ({ c.x - s * 0.62f, c.y + (float) i * s * 0.36f, c.x + s * 0.62f, c.y + (float) i * s * 0.36f }, 0.0f);
            stroke = juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
            break;
        }
        case Icon::randomize:
        {
            // two arrows chasing each other round a circle
            const auto rr = s * 0.5f;
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
    // A round power switch: filled red while ROOM is running, a dark chip when bypassed.
    const auto on = ! getToggleState();
    const auto d = std::min (getWidth(), getHeight()) - 2.0f;
    const auto r = juce::Rectangle<float> (d, d).withCentre (getLocalBounds().toFloat().getCentre());
    auto fill = on ? Colours::crimson : Colours::chip;
    if (over) fill = fill.brighter (0.08f);
    if (down) fill = fill.darker (0.1f);
    g.setColour (fill);
    g.fillEllipse (r);
    g.setColour (on ? juce::Colours::white.withAlpha (0.12f) : Colours::chipEdge);
    g.drawEllipse (r.reduced (0.5f), 1.0f);
    const auto c = r.getCentre();
    const auto rr = d * 0.22f;
    juce::Path p;
    p.addCentredArc (c.x, c.y + 1.0f, rr, rr, 0.0f, 0.65f, juce::MathConstants<float>::twoPi - 0.65f, true);
    p.startNewSubPath (c.x, c.y - rr - 2.0f);
    p.lineTo (c.x, c.y - 0.5f);
    g.setColour (on ? Colours::dustWhite : Colours::dustWhite.withAlpha (0.55f));
    g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
// The sign-in page

LicensePage::LicensePage (RoomAudioProcessor& p) : proc (p)
{
    setInterceptsMouseClicks (true, true);   // the controls underneath stay frozen
    setWantsKeyboardFocus (true);

    auto styleField = [] (juce::TextEditor& t, const juce::String& hint)
    {
        t.setFont (Fonts::sans (15.0f));
        t.setIndents (12, 0);
        t.setJustification (juce::Justification::centredLeft);
        t.setColour (juce::TextEditor::backgroundColourId, Colours::glass);
        t.setColour (juce::TextEditor::textColourId, Colours::screenText);
        t.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff050505));
        t.setColour (juce::TextEditor::focusedOutlineColourId, Colours::crimson);
        t.setColour (juce::CaretComponent::caretColourId, Colours::crimson);
        t.setTextToShowWhenEmpty (hint, Colours::screenDim.withAlpha (0.7f));
    };
    styleField (email, "the email you got room with");
    styleField (key, "ROOM-XXXXX-XXXXX-XXXXX-XXXXX");
    key.setInputRestrictions (40, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-");
    email.onReturnKey = [this] { key.grabKeyboardFocus(); };
    key.onReturnKey = [this] { submit(); };
    key.onTextChange = [this]
    {
        // Keys are upper case; keep the caret where it is.
        const auto upper = key.getText().toUpperCase();
        if (upper != key.getText())
        {
            const auto caret = key.getCaretPosition();
            key.setText (upper, false);
            key.setCaretPosition (caret);
        }
    };

    activateButton.getProperties().set ("key", "dark");
    activateButton.onClick = [this] { submit(); };
    for (auto* b : { &getButton, &helpButton })
    {
        b->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b->setColour (juce::TextButton::textColourOffId, Colours::screenDim);
    }
    getButton.onClick = [] { juce::URL (LicenseManager::getUrl).launchInDefaultBrowser(); };
    helpButton.onClick = [] { LicenseManager::emailSupport ("Lost my ROOM license key"); };

    for (auto* c : std::initializer_list<juce::Component*> { &email, &key, &activateButton, &getButton, &helpButton })
        addAndMakeVisible (c);

    proc.getLicensing().addChangeListener (this);
    update();
}

LicensePage::~LicensePage()
{
    proc.getLicensing().removeChangeListener (this);
}

void LicensePage::changeListenerCallback (juce::ChangeBroadcaster*)
{
    proc.licenseChanged();
    update();
    if (onChange) onChange();
}

void LicensePage::update()
{
    const auto wasVisible = isVisible();
    setVisible (proc.isLicenseLocked());
    if (! isVisible())
        return;
    toFront (false);
    if (! wasVisible)
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LicensePage> (this)]
                                         { if (safe != nullptr && safe->isShowing()) safe->focus(); });
}

bool LicensePage::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey)
        submit();
    return true;   // nothing behind the page reacts while it's up
}

void LicensePage::submit()
{
    if (busy)
        return;
    const auto e = email.getText().trim(), k = key.getText().trim();
    if (! e.containsChar ('@') || ! e.fromLastOccurrenceOf ("@", false, false).containsChar ('.'))
        return show ("enter the email address you got room with.", false);
    if (k.length() < 10)
        return show ("enter the license key from your room email.", false);

    busy = true;
    activateButton.setEnabled (false);
    show ("activating...", true);
    auto safe = juce::Component::SafePointer<LicensePage> (this);
    proc.getLicensing().activate (e, k, [safe] (juce::String error)
    {
        if (safe == nullptr)
            return;
        safe->busy = false;
        safe->activateButton.setEnabled (true);
        if (error.isNotEmpty())
            return safe->show (error, false);
        safe->show ("activated. enjoy room.", true);
    });
}

juce::Rectangle<int> LicensePage::cardArea() const
{
    return getLocalBounds().withSizeKeepingCentre (460, 404).translated (0, -10);
}

void LicensePage::resized()
{
    auto c = cardArea().reduced (32, 0);
    c.removeFromTop (28 + 30 + 10 + 40 + 16);   // mark, title, text
    c.removeFromTop (17);
    email.setBounds (c.removeFromTop (38));
    c.removeFromTop (14 + 17);
    key.setBounds (c.removeFromTop (38));
    c.removeFromTop (42);   // the message
    activateButton.setBounds (c.removeFromTop (38).withSizeKeepingCentre (180, 38));
    auto links = cardArea().withTop (cardArea().getBottom() - 44).reduced (24, 8);
    getButton.setBounds (links.removeFromLeft (links.getWidth() / 2).withSizeKeepingCentre (150, 26));
    helpButton.setBounds (links.withSizeKeepingCentre (150, 26));
}

void LicensePage::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.66f));

    const auto card = cardArea().toFloat();
    for (int i = 0; i < 6; ++i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.07f));
        g.fillRoundedRectangle (card.translated (0.0f, 3.0f).expanded ((float) i * 1.6f), 12.0f + (float) i * 1.6f);
    }
    juce::ColourGradient body (juce::Colour (0xff1d1d1d), 0.0f, card.getY(), juce::Colour (0xff0e0e0e), 0.0f, card.getBottom(), false);
    g.setGradientFill (body);
    g.fillRoundedRectangle (card, 12.0f);
    g.setColour (juce::Colours::black);
    g.drawRoundedRectangle (card.reduced (0.5f), 12.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawLine (card.getX() + 12.0f, card.getY() + 1.5f, card.getRight() - 12.0f, card.getY() + 1.5f, 1.0f);

    auto r = card.reduced (32.0f, 0.0f);
    r.removeFromTop (24.0f);
    {
        // The mark: a red dot and the wordmark, as on the faceplate.
        auto mark = r.removeFromTop (16.0f);
        g.setColour (Colours::crimson);
        g.fillEllipse (mark.getX(), mark.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setFont (Fonts::mono (11.0f));
        g.setColour (Colours::screenDim);
        g.drawText ("R O O M", mark.withTrimmedLeft (18.0f), juce::Justification::centredLeft);
    }
    r.removeFromTop (12.0f);
    g.setFont (Fonts::sans (21.0f, Fonts::bold));
    g.setColour (Colours::dustWhite);
    g.drawText ("activate room", r.removeFromTop (30.0f), juce::Justification::centredLeft);
    r.removeFromTop (10.0f);
    g.setFont (Fonts::sans (14.0f));
    g.setColour (Colours::dustWhite.withAlpha (0.8f));
    g.drawFittedText ("sign in with your email and the license key from your room email. "
                      "one key works on up to three computers. audio passes through dry until then.",
                      r.removeFromTop (40.0f).toNearestInt(), juce::Justification::topLeft, 3, 1.0f);

    g.setFont (Fonts::mono (10.5f));
    g.setColour (Colours::screenDim);
    g.drawText ("email", labelFor (email), juce::Justification::centredLeft);
    g.drawText ("license key", labelFor (key), juce::Justification::centredLeft);

    if (message.isNotEmpty())
    {
        g.setFont (Fonts::sans (13.0f));
        g.setColour (messageIsGood ? Colours::dustWhite.withAlpha (0.85f) : Colours::crimson.brighter (0.45f));
        g.drawFittedText (message, key.getBounds().withY (key.getBottom() + 4).withHeight (36), juce::Justification::centred, 2, 1.0f);
    }

    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine (cardArea().getBottom() - 44, card.getX() + 16.0f, card.getRight() - 16.0f);
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
    loadScreen.addAndMakeVisible (recordButton);   // on the stage, so drops over it still reach the stage
    recordButton.setClickingTogglesState (false);
    recordButton.onClick = [this]
    {
        if (proc.isRecording()) proc.stopRecording();
        else                    proc.startRecording();
    };
    loadScreen.addAndMakeVisible (roomSaveButton);   // on the stage, so drops over it still reach the stage
    roomSaveButton.onClick = [this] { openRoomSaveDialog(); };
    recordButton.setChipText ("record");
    roomSaveButton.setChipText ("save room");
    allRoomsButton.setChipText ("all rooms");
    clearRoomButton.setChipText ("clear");
    loadScreen.addAndMakeVisible (clearRoomButton);
    clearRoomButton.onClick = [this] { proc.clearRoom(); };
    advancedButton.setChipText ("advanced");
    loadScreen.addAndMakeVisible (allRoomsButton);   // on the stage, so drops over it still reach the stage
    allRoomsButton.onClick = [this] { roomBrowser.setVisible (true); roomBrowser.toFront (true); };
    loadScreen.onLoadRoom = [this] (const juce::String& key) { loadRoom (key); };
    loadScreen.addAndMakeVisible (roomScreen);   // on the stage, so drops over it still reach the stage
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
    roomBrowser.onLoad = [this] (const juce::String& key) { loadRoom (key); };   // stays open to keep browsing (WARP)
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

    content.addChildComponent (licensePage);
    licensePage.onChange = [this] { repaint(); };

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
    juce::ignoreUnused (onBlack);
    k.label.setText (name.toUpperCase(), juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (Fonts::sans (11.5f, Fonts::bold).withExtraKerningFactor (0.18f));
    k.label.setColour (juce::Label::textColourId, Colours::dustWhite);
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
    const auto knobs = controlsArea.toNearestInt();
    tour.setBounds (0, 0, contentWidth(), baseHeight);
    tour.start ({
        { stageArea.toNearestInt(), "load",
          "drop any sound here, or press record and play one. room listens for the space inside it: how it decays, the tone it adds, the notes that ring. the cards are your rooms: click one to load it." },
        { knobs, "shape",
          "intensity is how much of that room you hear. balance leans to its tone or its space, tail sets how long it rings, space how wide." },
        { finishArea.toNearestInt(), "finish",
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
        m.addItem ("check for updates", [this] { juce::URL (UpdateChecker::downloadPageUrl).launchInDefaultBrowser(); });
    m.addItem ("visit website", [] { juce::URL (UpdateChecker::websiteUrl).launchInDefaultBrowser(); });
    m.addItem ("get support", [version]
    {
        LicenseManager::emailSupport ("room v" + version);
    });
    m.addItem (juce::String (juce::CharPointer_UTF8 ("about room\xe2\x80\xa6")), [this] { showAbout(); });
    m.addSeparator();
    auto& license = proc.getLicensing();
    if (license.isActivated())
    {
        m.addItem ("licensed to " + license.licensedEmail(), false, false, nullptr);
        m.addItem (juce::String (juce::CharPointer_UTF8 ("deactivate this computer\xe2\x80\xa6")), [this] { confirmDeactivate(); });
    }
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

void RoomAudioProcessorEditor::confirmDeactivate()
{
    EvilleLookAndFeel::showDialog (content, "deactivate this computer?",
                                   "room stops here and frees this computer, so your key can go on another one. "
                                   "sign in again any time with your email and key.",
                                   { "deactivate", "cancel" }, {},
                                   [safe = juce::Component::SafePointer<RoomAudioProcessorEditor> (this)] (int result, const juce::String&)
                                   {
                                       if (safe == nullptr || result != 1)
                                           return;
                                       safe->proc.lockLicense();
                                       safe->licensePage.update();
                                       safe->proc.getLicensing().deactivate ([safe] (juce::String error)
                                       {
                                           if (safe != nullptr && error.isNotEmpty())
                                               safe->licensePage.fill ({}, {}, error, false);
                                       });
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
    if (ticksUntilTour > 0 && --ticksUntilTour == 0 && autoShowTour && ! proc.isLicenseLocked())
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
    for (auto* b : { &roomSaveButton, &clearRoomButton })
    {
        b->setEnabled (valid);
        b->setAlpha (valid ? 1.0f : 0.45f);
    }

    const auto isAuto = autoBalance.getToggleState();
    if (balance.slider.isEnabled() == isAuto)
        balance.slider.setEnabled (! isAuto);

    // The live values and the power light
    content.repaint (controlsArea.toNearestInt());
    content.repaint (juce::Rectangle<int> (20, 20).withCentre (finishLed.toInt()));
}

//==============================================================================
// Painting: the desk, the unit sitting on it, the print on the console

void RoomAudioProcessorEditor::paintContent (juce::Graphics& g)
{
    // What doesn't move is drawn once into an image at the window's real pixel
    // size, then reused; the values and the power light are drawn live.
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
    paintValues (g);
}

namespace
{
    void drawModule (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (Colours::module);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (Colours::moduleEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    }
}

void RoomAudioProcessorEditor::paintUnit (juce::Graphics& g)
{
    // A solid near-black window; modules are recessed panels with a hairline edge.
    g.fillAll (Colours::window);
    drawModule (g, controlsArea);
    drawModule (g, finishArea);
    if (proc.advancedOpen)
    {
        const auto r = advancedArea.reduced (0.0f, 12.0f).withTrimmedRight (12.0f);
        drawModule (g, r);
        if (auto tex = Hw::asset ("panel_advanced.jpg"); tex.isValid())
        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path shape;
            shape.addRoundedRectangle (r.reduced (1.0f), 10.0f);
            g.reduceClipRegion (shape);
            g.setOpacity (0.85f);
            g.drawImage (tex, r, juce::RectanglePlacement::fillDestination);
        }
    }
    paintConsoleArt (g);
}

void RoomAudioProcessorEditor::paintConsoleArt (juce::Graphics& g)
{
    // The wordmark (Ethan's artwork, room_logo.svg) in off-white, the top bar's height
    {
        int size = 0;
        if (auto* data = UiAssetFiles::get ("room_logo.svg", size))
            if (auto logo = juce::Drawable::createFromImageData (data, (size_t) size))
            {
                logo->replaceColour (juce::Colours::black, Colours::dustWhite);
                const auto h = 32.0f, w = h * 317.35f / 87.64f;
                logo->drawWithin (g, juce::Rectangle<float> (w, h).withPosition (60.0f, 20.0f), juce::RectanglePlacement::centred, 1.0f);
            }
    }

    // Top bar dividers either side of out, like WARP's
    g.setColour (Colours::moduleEdge);
    for (auto x : { (float) output.slider.getX() - 8.0f, (float) bypassButton.getX() - 8.0f })
        g.drawLine (x, 24.0f, x, 52.0f, 1.0f);

    // Controls module: the dividers and titles (the values are drawn live)
    const auto sw = controlsArea.getWidth() / 4.0f;
    const char* titles[4] = { "intensity", "balance", "tail", "space" };
    for (int i = 0; i < 4; ++i)
    {
        const auto sec = juce::Rectangle<float> (controlsArea.getX() + sw * (float) i, controlsArea.getY(), sw, controlsArea.getHeight());
        if (i > 0)
        {
            g.setColour (Colours::moduleEdge);
            g.drawLine (sec.getX(), sec.getY() + 18.0f, sec.getX(), sec.getBottom() - 18.0f, 1.0f);
        }
        EvilleLookAndFeel::drawCaps (g, titles[i], { sec.getX() + 24.0f, sec.getY() + 20.0f, 120.0f, 16.0f }, 12.5f, Colours::dustWhite);
    }

    // Bottom module: dividers between eq, tape, auto eq, on and out; their titles
    g.setColour (Colours::moduleEdge);
    for (auto x : { 560.0f, 670.0f, 780.0f, 830.0f })
        g.drawLine (x, finishArea.getY() + 20.0f, x, finishArea.getBottom() - 20.0f, 1.0f);
    EvilleLookAndFeel::drawCaps (g, "on", juce::Rectangle<float> (40.0f, 14.0f).withCentre (finishLed.translated (0.0f, 22.0f)),
                                 10.5f, Colours::dustWhite, juce::Justification::centred);
    EvilleLookAndFeel::drawCaps (g, "out", juce::Rectangle<float> (40.0f, 14.0f).withCentre (finishJack.translated (0.0f, -34.0f)),
                                 10.5f, Colours::dustWhite, juce::Justification::centred);
    Hw::drawJack (g, finishJack, 34.0f);
}

void RoomAudioProcessorEditor::paintValues (juce::Graphics& g)
{
    // The four big values, live (they follow the LFOs too)
    Knob* ks[4] = { &intensity, &balance, &tail, &space };
    const auto sw = controlsArea.getWidth() / 4.0f;
    for (int i = 0; i < 4; ++i)
    {
        const auto x = controlsArea.getX() + sw * (float) i + 22.0f;
        const auto v = ks[i]->slider.getValue();
        auto text = ks[i] == &tail ? juce::String (v, v < 10.0 ? 1 : 0) + " s"
                                   : juce::String (juce::roundToInt (v * 100.0)) + "%";
        if (ks[i] == &balance)
            text = juce::String (juce::roundToInt ((autoBalance.getToggleState() ? proc.getDetectedBalance() : (float) balance.slider.getValue()) * 100.0f));
        const auto alpha = ks[i]->slider.isEnabled() || ks[i] == &balance ? 1.0f : 0.5f;
        g.setFont (Fonts::sans (34.0f, Fonts::bold));
        g.setColour (Colours::dustWhite.withAlpha (alpha));
        g.drawFittedText (text, juce::Rectangle<float> (x, controlsArea.getY() + 42.0f, sw - 128.0f, 42.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.8f);
    }
    // The power light follows bypass.
    const auto on = ! (proc.apvts.getRawParameterValue (ParamIDs::bypass)->load() > 0.5f);
    g.setColour (on ? Colours::crimson.brighter (0.2f) : Colours::chip);
    g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (finishLed));
    if (! on)
    {
        g.setColour (Colours::chipEdge);
        g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (finishLed), 1.0f);
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
    licensePage.setBounds (full);
    licensePage.update();
    roomBrowser.setBounds (full);
    saveDialog.setBounds (full);
    roomSaveDialog.setBounds (full);
    staticLayer = {};   // the print follows the layout

    // --- Top bar (WARP's): menu, wordmark, studios box, advanced, randomize, wet, out, power ---
    const int keyY = 16, key = 40;
    menuButton.setBounds (12, keyY, key, key);
    presetBar.setBounds (196, keyY, 316, key);
    advancedButton.setBounds (522, keyY + 2, 104, key - 4);
    randomButton.setBounds (634, keyY + 2, key - 4, key - 4);
    for (auto [k, x] : { std::pair { &mix, 730 }, { &output, 808 } })
    {
        k->slider.setBounds (juce::Rectangle<int> (44, 44).withCentre ({ x, keyY + key / 2 }));
        k->label.setBounds (juce::Rectangle<int> (40, 16).withCentre ({ x - 44, keyY + key / 2 }));
        k->label.setJustificationType (juce::Justification::centredRight);
    }
    bypassButton.setBounds (842, keyY + 2, 36, 36);

    // --- The stage, its chips, and the room box in the middle ---
    const auto st = stageArea.toNearestInt();
    loadScreen.setBounds (st);
    roomScreen.setBounds (loadScreen.boxArea());   // stage coordinates from here on
    recordButton.setBounds (16, 14, 116, 32);
    roomSaveButton.setBounds (140, 14, 124, 32);
    clearRoomButton.setBounds (272, 14, 84, 32);
    allRoomsButton.setBounds (st.getWidth() - 16 - 120, 14, 120, 32);
    toneScreen.setVisible (false);   // the tone now shows in the room's info (click the box)

    // --- Controls: one module, four sections ---
    const auto ca = controlsArea.toNearestInt();
    const int sw = ca.getWidth() / 4;
    Knob* knobs[4] = { &intensity, &balance, &tail, &space };
    for (int i = 0; i < 4; ++i)
    {
        const auto sec = juce::Rectangle<int> (ca.getX() + sw * i, ca.getY(), sw, ca.getHeight());
        knobs[i]->slider.setBounds (juce::Rectangle<int> (104, 104).withCentre ({ sec.getRight() - 64, sec.getCentreY() }));
        knobs[i]->label.setVisible (false);   // titled in the module's print
    }
    autoBalance.setBounds (ca.getX() + sw + 22, ca.getY() + 90, 64, 24);

    // --- Advanced (the right side, full height) ---
    advancedPanel.setBounds (advancedArea.reduced (0.0f, 12.0f).withTrimmedRight (12.0f).toNearestInt().reduced (4));

    // --- Bottom module: eq, tape, auto eq, on, out (today's order) ---
    const auto f = finishArea.toNearestInt();
    eqPanel.setBounds (f.getX() + 14, f.getY() + 12, 520, f.getHeight() - 24);
    for (auto [k, x] : { std::pair { &tape, 615 }, { &autoEq, 725 } })
    {
        k->slider.setBounds (juce::Rectangle<int> (76, 76).withCentre ({ x, f.getY() + 72 }));
        k->label.setBounds (juce::Rectangle<int> (100, 16).withCentre ({ x, f.getY() + 24 }));
    }
    finishLed = { 805.0f, finishArea.getY() + 56.0f };
    finishJack = { 855.0f, finishArea.getY() + 76.0f };
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
