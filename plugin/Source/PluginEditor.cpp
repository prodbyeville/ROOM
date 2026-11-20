#include "PluginEditor.h"
#include "R5Parts.h"

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
                                                                                  : liveParam (p, ParamIDs::spaceAmount).value;
    }

    juce::String lower (const juce::String& s) { return s.toLowerCase(); }
}

//==============================================================================
// The stage

LoadScreen::LoadScreen (RoomAudioProcessor& p) : proc (p)
{
    setTooltip ("drop a sound to make a room");
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
    // Your quick-select rooms (slot order) first; any free places are filled with
    // the loaded room, then favourites, then the rest by name.
    auto& lib = proc.getRoomLibrary();
    room::presets::PresetLibrary::Query q;
    const auto all = lib.query (q);
    // After the quick-select slots and the loaded room: favourites, then your own rooms
    // (every user pack, Eville included), then the factory ones.
    std::vector<int> order;
    const auto& entries = lib.getEntries();
    for (auto i : all) if (entries[(size_t) i].favorite) order.push_back (i);
    for (auto i : all) if (! entries[(size_t) i].favorite && ! entries[(size_t) i].factory) order.push_back (i);
    for (auto i : all) if (! entries[(size_t) i].favorite && entries[(size_t) i].factory) order.push_back (i);
    const auto current = proc.getCurrentRoomKey();
    std::vector<Card> next;
    auto add = [&] (const room::presets::PresetLibrary::Entry& e)
    {
        for (auto& c : next)
            if (c.key == e.key)
                return;
        next.push_back ({ e.key, e.meta.name, e.tags.decay, e.tags.tone, {}, 0.0f });
    };
    constexpr auto maxCards = (size_t) room::presets::PresetLibrary::numQuickSlots;
    for (auto& k : lib.getQuickSlots())
        if (auto* e = lib.find (k))
            add (*e);
    if (auto* e = lib.find (current); e != nullptr && next.size() < maxCards)
        add (*e);
    for (auto i : order)
    {
        if (next.size() >= maxCards)
            break;
        add (lib.getEntries()[(size_t) i]);
    }
    cards = std::move (next);
}

void LoadScreen::timerCallback()
{
    if (++libraryStamp % 30 == 0 || cards.empty())
        refreshCards();
    // One lap a minute; it holds still while the mouse is over the stage. Moved by the
    // time that really passed, so an uneven timer can't make it stutter.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt = lastTick > 0.0 ? juce::jlimit (0.0, 0.2, (now - lastTick) * 0.001) : 0.0;
    lastTick = now;
    if (! isMouseOverOrDragging (true) && ! hovering)
        orbitPhase += (float) (juce::MathConstants<double>::twoPi / 60.0 * dt);
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
            const auto crop = tex.getClippedImage ({ seed % (tex.getWidth() - cw), (seed / 7) % (tex.getHeight() - ch), cw, ch });
            g.setOpacity (alpha);
            // Drawn at the card's exact (fractional) place: whole-pixel snapping made the cards jitter.
            g.drawImageTransformed (crop, juce::AffineTransform::scale (r.getWidth() / (float) cw, r.getHeight() / (float) ch)
                                              .translated (r.getX(), r.getY()));
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
        const auto d = (x < 0.01f ? 3.4f : 3.0f) * scale;
        g.setColour (ink.withAlpha (alpha));
        g.fillEllipse (juce::Rectangle<float> (d * 2.0f, d * 2.0f).withCentre ({ plot.getX() + plot.getWidth() * x, yAt (x) }));
    }

    // The name under the card
    EvilleLookAndFeel::drawCaps (g, c.name, r.withY (r.getBottom() + 5.0f * scale).withHeight (16.0f * scale).expanded (30.0f, 0.0f),
                                 std::max (8.5f, 11.0f * scale), Colours::dustWhite.withAlpha ((hot || loaded ? 1.0f : 0.78f) * alpha),
                                 juce::Justification::centred);
}

void LoadScreen::paintCards (juce::Graphics& g, bool front)
{
    const auto n = (int) cards.size();
    const auto current = proc.getCurrentRoomKey();
    std::vector<int> order;
    for (int i = 0; i < n; ++i)
        if ((cards[(size_t) i].depth >= 0.0f) == front)
            order.push_back (i);
    std::sort (order.begin(), order.end(), [this] (int x, int y) { return cards[(size_t) x].depth < cards[(size_t) y].depth; });
    for (auto i : order)
    {
        const auto& c = cards[(size_t) i];
        const auto near = c.depth * 0.5f + 0.5f;
        const auto scale = 0.62f + 0.38f * near;
        const auto angle = 0.10f * std::sin ((float) i * 2.3f + orbitPhase * 0.5f) * (1.0f - 0.5f * near);
        const auto alpha = front ? 1.0f : 0.45f + 0.55f * near;
        paintCard (g, c, scale, angle, alpha, c.key == current, c.key == hoverKey);
    }
}

void LoadScreen::paintOverChildren (juce::Graphics& g)
{
    paintCards (g, true);
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
            const auto amount = juce::jlimit (0.0f, 1.0f, liveParam (proc, ParamIDs::intensity).value * 0.5f);
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
    const auto rx = bounds.getWidth() * 0.34f, ry = bounds.getHeight() * 0.25f;
    const auto cardScale = juce::jlimit (0.8f, 1.0f, bounds.getHeight() / 400.0f);   // the stage was 400 tall
    const auto n = (int) cards.size();
    const auto t = (float) (juce::Time::getMillisecondCounterHiRes() * 0.001);
    for (int i = 0; i < n; ++i)
    {
        auto& c = cards[(size_t) i];
        const auto a = orbitPhase + juce::MathConstants<float>::twoPi * (float) i / (float) std::max (1, n);
        c.depth = std::cos (a);   // 1 = front (bottom), -1 = back (top)
        const auto scale = 0.62f + 0.38f * (c.depth * 0.5f + 0.5f);
        const auto bob = std::sin (t * 0.7f + (float) i * 1.7f) * 3.0f;
        const auto w = 132.0f * scale * cardScale, h = 84.0f * scale * cardScale;
        c.bounds = juce::Rectangle<float> (w, h).withCentre ({ centre.x + rx * std::sin (a), centre.y + ry * c.depth + bob - 8.0f });
    }
    // The far half of the ring goes behind the room box; the near half is drawn over it
    // (paintOverChildren), solid, so the box disappears behind the cards.
    paintCards (g, false);

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
            if (auto* e = proc.getRoomLibrary().find (proc.getCurrentRoomKey()))
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
    setTooltip ("your room. click for its numbers");
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
    const auto glow = 0.3f + 0.7f * juce::jlimit (0.0f, 1.0f, intensity.value * 0.5f * 1.4f);

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
        // rounded ends, so the edges meet cleanly at every corner
        g.strokePath (e, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
                                                 std::min (intensity.value, RoomAudioProcessor::maxRoomIntensity),
                                                 size.value, damping.value);
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
    setTooltip ("the tone this room adds");
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

    // Round 5: the stage's buttons are outlined pills on the glass; header buttons are keys.
    if ((bool) getProperties()["pill"])
    {
        const auto pill = r.reduced (1.0f);
        g.setColour (juce::Colours::black.withAlpha (over ? 0.32f : 0.22f));
        g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
        g.setColour (Colours::dustWhite.withAlpha (over ? 0.95f : 0.75f));
        g.drawRoundedRectangle (pill.translated (0.0f, down ? 0.5f : 0.0f), pill.getHeight() * 0.5f, 1.2f);
        auto label = pill;
        if (icon == Icon::record)
        {
            const auto on = getToggleState();
            const auto blink = ! on || (juce::Time::getMillisecondCounter() / 450) % 2 == 0;
            const auto textW = EvilleLookAndFeel::capsWidth (chipText, 11.0f);
            const auto groupW = 9.0f + 8.0f + textW;
            const auto x0 = pill.getCentreX() - groupW * 0.5f;
            g.setColour (juce::Colour (0xffe03535).withAlpha (blink ? 1.0f : 0.4f));
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ x0 + 4.5f, pill.getCentreY() }));
            label = { x0 + 17.0f, pill.getY(), textW + 4.0f, pill.getHeight() };
        }
        EvilleLookAndFeel::drawCaps (g, chipText, label, 11.0f, Colours::dustWhite.withAlpha (over ? 1.0f : 0.92f),
                                     icon == Icon::record ? juce::Justification::centredLeft : juce::Justification::centred);
        return;
    }
    if (getProperties().contains ("r5key"))
    {
        const auto kind = getProperties()["r5key"].toString();
        auto kb = r.withTrimmedBottom (R5::glowRoom);   // room below for the lit glow
        if (kind == "square")
            kb = kb.withSizeKeepingCentre (std::min (kb.getWidth(), kb.getHeight()), std::min (kb.getWidth(), kb.getHeight()));
        R5::drawKey (g, kb.reduced (0.0f, 1.0f), kind == "cream" ? R5::KeyKind::cream : kind == "square" ? R5::KeyKind::square : R5::KeyKind::black,
                     chipText, latched, over, down);
        if (chipText.isNotEmpty())
            return;
        r = kb.withTrimmedBottom (kb.getHeight() * 0.16f).translated (0.0f, down ? 1.0f : 0.0f);   // the icon, on the key's top face
    }

    if (chipText.isNotEmpty())
    {
        EvilleLookAndFeel::drawChip (g, r, latched, over, down);
        auto label = r;
        if (icon == Icon::record)
        {
            const auto on = getToggleState();
            const auto blink = ! on || (juce::Time::getMillisecondCounter() / 450) % 2 == 0;
            // dot + gap + text, centred together like the other chips' text
            const auto textW = EvilleLookAndFeel::capsWidth (chipText, 11.5f);
            const auto groupW = 9.0f + 7.0f + textW;
            const auto x0 = r.getCentreX() - groupW * 0.5f;
            const auto dot = juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ x0 + 4.5f, r.getCentreY() });
            g.setColour (Colours::crimson.brighter (0.2f).withAlpha (blink ? 1.0f : 0.4f));
            g.fillEllipse (dot);
            label = { x0 + 16.0f, r.getY(), textW + 4.0f, r.getHeight() };
        }
        EvilleLookAndFeel::drawCaps (g, chipText, label, 11.5f, latched ? Colours::redLight : ink,
                                     icon == Icon::record ? juce::Justification::centredLeft : juce::Justification::centred);
        return;
    }

    if (icon != Icon::menu && ! getProperties().contains ("r5key"))
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
        case Icon::display:
        {
            // a small screen on a stand
            const auto scr = juce::Rectangle<float> (s * 1.3f, s * 0.86f).withCentre (c.translated (0.0f, -s * 0.12f));
            g.drawRoundedRectangle (scr, 2.0f, 1.5f);
            p.addLineSegment ({ c.x, scr.getBottom(), c.x, scr.getBottom() + s * 0.24f }, 0.0f);
            p.addLineSegment ({ c.x - s * 0.34f, scr.getBottom() + s * 0.26f, c.x + s * 0.34f, scr.getBottom() + s * 0.26f }, 0.0f);
            break;
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
            // two arcs chasing each other round a circle, each with a clean filled arrowhead
            const auto rr = s * 0.36f;
            const auto pi = juce::MathConstants<float>::pi;
            juce::Path heads;
            for (int k = 0; k < 2; ++k)
            {
                const auto a0 = (float) k * pi + 0.45f, a1 = a0 + pi - 1.15f;
                p.addCentredArc (c.x, c.y, rr, rr, 0.0f, a0, a1, true);
                const juce::Point<float> end (c.x + rr * std::sin (a1), c.y - rr * std::cos (a1));
                const juce::Point<float> dir (std::cos (a1), std::sin (a1));        // the way round (clockwise)
                const juce::Point<float> out (std::sin (a1), -std::cos (a1));       // away from the centre
                const auto len = s * 0.2f, half = s * 0.13f;
                heads.addTriangle (end + dir * len, end + out * half, end - out * half);
            }
            g.strokePath (p, stroke);
            g.fillPath (heads);
            return;
        }
    }
    g.strokePath (p, stroke);
}

void BypassSwitch::paintButton (juce::Graphics& g, bool over, bool down)
{
    // The ROOM mark is the power switch: glowing red while ROOM is on, dim when bypassed.
    const auto on = ! getToggleState();
    // The button is bigger than the mark so its glow fades out fully inside the bounds (no square edge).
    const auto box = getLocalBounds().toFloat();
    const auto d = std::min (box.getWidth(), box.getHeight()) * 0.58f;
    const auto r = juce::Rectangle<float> (d, d).withCentre (box.getCentre()).translated (0.0f, down ? 0.5f : 0.0f);
    const auto colour = on ? juce::Colour (0xffe03535) : Colours::dustWhite.withAlpha (over ? 0.5f : 0.32f);
    if (on)
    {
        const auto c = r.getCentre();
        const auto reach = std::min (box.getWidth(), box.getHeight()) * 0.5f - 0.5f;
        juce::ColourGradient glow (juce::Colour (0xffe03535).withAlpha (over ? 0.34f : 0.24f), c.x, c.y,
                                   juce::Colour (0xffe03535).withAlpha (0.0f), c.x + reach, c.y, true);
        glow.addColour (0.55, juce::Colour (0xffe03535).withAlpha (over ? 0.12f : 0.08f));
        g.setGradientFill (glow);
        g.fillEllipse (juce::Rectangle<float> (reach * 2.0f, reach * 2.0f).withCentre (c));
    }
    // the mark (room_mark.svg: frame, back wall, four corner lines), stroked at a weight that reads small
    const auto k = r.getWidth() / 512.0f;
    auto pt = [&] (float x, float y) { return juce::Point<float> (r.getX() + x * k, r.getY() + y * k); };
    juce::Path mark;
    mark.addRoundedRectangle (pt (64.0f, 64.0f).x, pt (64.0f, 64.0f).y, 384.0f * k, 384.0f * k, 50.0f * k);
    mark.addRectangle (pt (174.0f, 164.0f).x, pt (174.0f, 164.0f).y, 164.0f * k, 144.0f * k);
    for (auto [a, b] : { std::pair { pt (78.6f, 78.6f), pt (174.0f, 164.0f) }, { pt (433.4f, 78.6f), pt (338.0f, 164.0f) },
                         { pt (78.6f, 433.4f), pt (174.0f, 308.0f) }, { pt (433.4f, 433.4f), pt (338.0f, 308.0f) } })
    {
        mark.startNewSubPath (a);
        mark.lineTo (b);
    }
    g.setColour (colour);
    g.strokePath (mark, juce::PathStrokeType (std::max (1.6f, d * 0.055f), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

//==============================================================================
// First-run tour

//==============================================================================

void SyncDot::paintButton (juce::Graphics& g, bool over, bool down)
{
    if (getProperties().contains ("r5key"))
    {
        EvilleLookAndFeel::drawR5Key (g, *this, over, down);
        return;
    }
    // A dark round button; on, its core glows red.
    const auto b = getLocalBounds().toFloat().reduced (1.5f).translated (0.0f, down ? 0.5f : 0.0f);
    const auto c = b.getCentre();
    const auto on = getToggleState();
    if (on)
    {
        juce::ColourGradient glow (Colours::crimson.withAlpha (0.55f), c.x, c.y, Colours::crimson.withAlpha (0.0f), c.x + b.getWidth() * 0.9f, c.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (b.expanded (5.0f));
    }
    juce::ColourGradient body (juce::Colour (0xff2e2a2a), c.x, b.getY(), juce::Colour (0xff121010), c.x, b.getBottom(), false);
    g.setGradientFill (body);
    g.fillEllipse (b);
    g.setColour (juce::Colours::white.withAlpha (over ? 0.28f : 0.16f));
    g.drawEllipse (b, 1.0f);
    const auto core = b.reduced (b.getWidth() * 0.27f);
    if (on)
    {
        juce::ColourGradient led (Colours::redLight.brighter (0.5f), c.x, c.y - core.getHeight() * 0.2f, Colours::crimson, c.x, core.getBottom(), true);
        g.setGradientFill (led);
    }
    else
        g.setColour (juce::Colour (0xff3a2424));
    g.fillEllipse (core);
}

NoiseScreen::NoiseScreen (RoomAudioProcessor& p) : proc (p)
{
    type = proc.apvts.getParameter (ParamIDs::noiseType);
    setTooltip ("click to pick a noise");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    startTimerHz (30);
}

void NoiseScreen::timerCallback()
{
    // Level drives how lively the band is (and how fast it drifts); with follow up, the band
    // swells while sound comes in and thins to a line when nothing plays.
    const auto level = proc.apvts.getRawParameterValue (ParamIDs::noiseLevel)->load();
    const auto follow = proc.apvts.getRawParameterValue (ParamIDs::noiseFollow)->load();
    const auto gate = proc.noiseGateShown.load (std::memory_order_relaxed);
    const auto stop = proc.noiseStopShown.load (std::memory_order_relaxed);
    const auto target = ((1.0f - follow) + follow * gate) * stop;
    activity += (target - activity) * 0.18f;
    phase += (0.4f + 1.1f * std::sqrt (level)) / 30.0f;
    if (isShowing())
        repaint();
}

void NoiseScreen::step (int delta)
{
    auto* c = dynamic_cast<juce::AudioParameterChoice*> (type);
    if (c == nullptr)
        return;
    const auto n = c->choices.size();
    const auto next = ((c->getIndex() + delta) % n + n) % n;
    c->beginChangeGesture();
    c->setValueNotifyingHost (c->convertTo0to1 ((float) next));
    c->endChangeGesture();
    repaint();
}

void NoiseScreen::showMenu()
{
    auto* c = dynamic_cast<juce::AudioParameterChoice*> (type);
    if (c == nullptr)
        return;
    // Grouped by section (classics, nature, places, machines, tonal), each in list order.
    juce::PopupMenu m;
    const auto& infos = room::NoiseBank::infos();
    juce::StringArray sections;
    for (const auto& info : infos)
        if (info.name != "unused")
            sections.addIfNotAlreadyThere (info.section);
    for (const auto& section : sections)
    {
        m.addSectionHeader (section);
        for (int i = 0; i < juce::jmin ((int) infos.size(), c->choices.size()); ++i)
            if (infos[(size_t) i].section == section && infos[(size_t) i].name != "unused")
                m.addItem (c->choices[i].toLowerCase(), true, i == c->getIndex(),
                           [this, i] { step (i - dynamic_cast<juce::AudioParameterChoice*> (type)->getIndex()); });
    }
    EvilleLookAndFeel::showMenu (m, *this);
}

void NoiseScreen::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked())
        showMenu();
}

void NoiseScreen::paint (juce::Graphics& g)
{
    // A black glass screen set into the case, with a band of fine particles drifting like a
    // slow wave; each profile has its own texture. Level makes the band brighter, wider and
    // livelier; with follow up it thins to a line while nothing plays.
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (juce::Colour (0xff050505));
    g.fillRoundedRectangle (r, 7.0f);
    {
        juce::Graphics::ScopedSaveState s0 (g);
        juce::Path glass;
        glass.addRoundedRectangle (r, 7.0f);
        g.reduceClipRegion (glass);
        juce::ColourGradient inner (juce::Colours::black.withAlpha (0.75f), r.getX(), r.getY(),
                                    juce::Colours::transparentBlack, r.getX(), r.getY() + 10.0f, false);
        g.setGradientFill (inner);
        g.fillRect (r.withHeight (10.0f));
    }
    juce::ColourGradient rim (juce::Colours::white.withAlpha (0.04f), r.getX(), r.getY(),
                              juce::Colours::white.withAlpha (0.12f), r.getX(), r.getBottom(), false);
    g.setGradientFill (rim);
    g.drawRoundedRectangle (r, 7.0f, 1.0f);

    juce::Graphics::ScopedSaveState save (g);
    juce::Path clip;
    clip.addRoundedRectangle (r.reduced (1.0f), 6.0f);
    g.reduceClipRegion (clip);

    auto* c = dynamic_cast<juce::AudioParameterChoice*> (type);
    const auto index = c != nullptr ? c->getIndex() : 0;
    const auto level = proc.apvts.getRawParameterValue (ParamIDs::noiseLevel)->load();
    const auto amp = std::sqrt (level);
    const auto open = 0.08f + 0.92f * juce::jlimit (0.0f, 1.0f, activity);   // a thin line when nothing plays
    const auto bright = (0.25f + 0.75f * amp) * (0.45f + 0.55f * activity);
    const auto width = (0.15f + 0.85f * amp) * open;   // how thick the band is
    const auto swing = (0.25f + 0.75f * amp) * open;   // how far the wave moves

    // The texture per profile: how many particles, how thick the band, how fast it moves,
    // how sparse and sparkly (vinyl), how wide the swell (air).
    // (made from each recording's sound by Tools/noise/make_noise_loops.py; see noise.txt)
    const auto& infos = room::NoiseBank::infos();
    const auto look = infos[(size_t) juce::jlimit (0, (int) infos.size() - 1, index)].look;
    juce::Random rng (0x524f4f + index * 7919);
    const auto w = r.getWidth(), h = r.getHeight();
    const auto twoPi = juce::MathConstants<float>::twoPi;
    for (int i = 0; i < look.count; ++i)
    {
        const auto u = rng.nextFloat();                                   // where along
        const auto spread = (rng.nextFloat() + rng.nextFloat() + rng.nextFloat() - 1.5f) / 1.5f;   // roughly gaussian -1..1
        const auto seed = rng.nextFloat();
        const auto x = r.getX() + std::fmod (u + phase * look.speed * (0.6f + 0.8f * seed) * 0.15f, 1.0f) * w;
        const auto xn = (x - r.getX()) / w;
        const auto centre = r.getCentreY() + swing * (h * 0.16f * std::sin (twoPi * (xn * look.waves + phase * look.speed * 0.4f))
                                                    + h * 0.06f * std::sin (twoPi * (xn * look.waves * 2.3f - phase * look.speed * 0.7f + 0.3f)));
        const auto y = centre + spread * h * look.thickness * width * (0.75f + 0.25f * std::sin (twoPi * (xn * 1.7f + phase * 0.05f)));
        auto a = (1.0f - std::abs (spread)) * 0.55f + 0.1f;
        auto size = 1.0f + seed * 0.6f;
        if (look.sparkle > 0.0f && seed > 1.0f - look.sparkle)
        {
            // a crackle: brighter, flickering
            const auto flick = 0.5f + 0.5f * std::sin (phase * 9.0f + seed * 40.0f);
            a = 0.5f + 0.5f * flick;
            size = 1.8f;
        }
        g.setColour (Colours::dustWhite.withAlpha (juce::jlimit (0.0f, 1.0f, a * bright)));
        g.fillRect (juce::Rectangle<float> (size, size).withCentre ({ x, y }));
    }
}

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
    // as tall as its text needs
    const int w = 360;
    juce::AttributedString as;
    as.append (steps[(size_t) step].text, Fonts::sans (14.5f), Colours::dustWhite);
    as.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout layout;
    layout.createLayout (as, (float) (w - 40));
    const int h = 16 + 22 + 8 + (int) std::ceil (layout.getHeight()) + 6 + 40 + 16;
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
    g.drawFittedText (st.text, r.toNearestInt(), juce::Justification::topLeft, 14, 1.0f);
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
// About

namespace
{
    // WARP's soft pill: ends that swell a little, a gently pinched middle.
    void drawSoftPill (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c)
    {
        const auto pinch = r.getHeight() * 0.07f, rad = r.getHeight() * 0.5f;
        juce::Path p;
        p.startNewSubPath (r.getX() + rad, r.getY());
        p.quadraticTo (r.getCentreX(), r.getY() + pinch, r.getRight() - rad, r.getY());
        p.quadraticTo (r.getRight() + rad * 0.15f, r.getY(), r.getRight(), r.getCentreY());
        p.quadraticTo (r.getRight() + rad * 0.15f, r.getBottom(), r.getRight() - rad, r.getBottom());
        p.quadraticTo (r.getCentreX(), r.getBottom() - pinch, r.getX() + rad, r.getBottom());
        p.quadraticTo (r.getX() - rad * 0.15f, r.getBottom(), r.getX(), r.getCentreY());
        p.quadraticTo (r.getX() - rad * 0.15f, r.getY(), r.getX() + rad, r.getY());
        p.closeSubPath();
        g.setColour (c);
        g.fillPath (p);
    }

    struct PillButtonLnF : juce::LookAndFeel_V4
    {
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
        {
            auto c = Colours::crimson;
            if (over) c = c.brighter (0.12f);
            if (down) c = c.darker (0.12f);
            drawSoftPill (g, b.getLocalBounds().toFloat().reduced (2.0f), c);
        }
        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
        {
            EvilleLookAndFeel::drawCaps (g, b.getButtonText(), b.getLocalBounds().toFloat(), 12.5f, Colours::dustWhite,
                                         juce::Justification::centred);
        }
    };
    PillButtonLnF& pillLnF() { static PillButtonLnF l; return l; }
}

AboutOverlay::AboutOverlay()
{
    setWantsKeyboardFocus (true);
    for (auto* b : { &tourButton, &websiteButton, &closeButton })
    {
        b->setLookAndFeel (&pillLnF());
        addAndMakeVisible (b);
    }
    tourButton.onClick = [this] { setVisible (false); if (onTour) onTour(); };
    websiteButton.onClick = [] { juce::URL (UpdateChecker::websiteUrl).launchInDefaultBrowser(); };
    closeButton.onClick = [this] { setVisible (false); };
}

juce::Rectangle<int> AboutOverlay::card() const
{
    return getLocalBounds().withSizeKeepingCentre (std::min (620, getWidth() - 40), std::min (680, getHeight() - 30));
}

void AboutOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! card().contains (e.getPosition()))
        setVisible (false);
}

bool AboutOverlay::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
        setVisible (false);
    return true;
}

void AboutOverlay::resized()
{
    auto row = card().removeFromBottom (78).reduced (40, 18);
    const auto w = (row.getWidth() - 2 * 20) / 3;
    tourButton.setBounds (row.removeFromLeft (w));
    row.removeFromLeft (20);
    websiteButton.setBounds (row.removeFromLeft (w));
    row.removeFromLeft (20);
    closeButton.setBounds (row);
}

void AboutOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.72f));
    const auto c = card().toFloat();
    g.setColour (juce::Colour (0xff121010));
    g.fillRoundedRectangle (c, 22.0f);
    g.setColour (Colours::crimson.withAlpha (0.85f));
    g.drawRoundedRectangle (c.reduced (0.75f), 22.0f, 1.5f);

    auto r = c.reduced (44.0f, 0.0f);
    r.removeFromTop (34.0f);

    // The wordmark
    {
        int size = 0;
        if (auto* data = UiAssetFiles::get ("room_logo.svg", size))
            if (auto logo = juce::Drawable::createFromImageData (data, (size_t) size))
            {
                logo->replaceColour (juce::Colours::black, Colours::dustWhite);
                const auto w = 210.0f, h = w * 87.64f / 317.35f;
                logo->drawWithin (g, r.removeFromTop (h).withSizeKeepingCentre (w, h), juce::RectanglePlacement::centred, 1.0f);
            }
    }
    r.removeFromTop (14.0f);
    g.setFont (Fonts::sans (16.0f, Fonts::medium));
    g.setColour (Colours::dustWhite);
    g.drawText ("turn any sound into a room", r.removeFromTop (22.0f), juce::Justification::centred);
    EvilleLookAndFeel::drawCaps (g, "version " + UpdateChecker::currentVersion() + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  by eville")),
                                 r.removeFromTop (20.0f), 10.5f, Colours::redLight, juce::Justification::centred, Fonts::medium);
    r.removeFromTop (16.0f);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRect (r.removeFromTop (1.0f));
    r.removeFromTop (14.0f);

    const std::pair<const char*, const char*> sections[] = {
        { "what it does", "every sound carries the space it was made in. room listens for that space, pulls it out and rebuilds it, "
                          "so anything else you play can live inside it." },
        { "make a room", "drop any audio on the stage (a clap in a stairwell, a voice memo, a tap on a glass) or press record and "
                         "play into the track. room measures how it decays in every band, the tone it puts on things and the notes "
                         "that ring. the cards are your rooms: click one to load it." },
        { "shape it", "intensity is how much of the room you hear, and glitch stutters it more as intensity goes up. tone and space "
                      "lean to the room's colour or its sense of space. tail sets how long it rings, size how big the room is." },
        { "texture it", "noise lays a texture under the room (vinyl, tape hiss, air, room tone), playing all the time or following "
                        "your sound. rate crushes the room's sample rate. advanced has pre-delay, width, damping, ducking, freeze, "
                        "tape, auto eq, a five-band eq and two lfos you can drag onto any knob." },
        { "keep it", "a ROOM is just the space. a STUDIO is the whole sound: the room, every knob and the lfos. both have packs, "
                     "favourites and tags." },
    };
    for (auto& [title, text] : sections)
    {
        EvilleLookAndFeel::drawCaps (g, title, r.removeFromTop (17.0f), 11.0f, Colours::redLight);
        r.removeFromTop (4.0f);
        juce::AttributedString as;
        as.append (text, Fonts::sans (14.5f), Colours::dustWhite.withAlpha (0.82f));
        as.setLineSpacing (2.0f);
        juce::TextLayout tl;
        tl.createLayout (as, r.getWidth());
        tl.draw (g, r.removeFromTop (tl.getHeight()));
        r.removeFromTop (14.0f);
    }
    g.setFont (Fonts::sans (11.5f));
    g.setColour (Colours::textDim);
    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc2\xa9 ")) + juce::String (juce::Time::getCurrentTime().getYear())
                    + juce::String (juce::CharPointer_UTF8 (" eville  \xc2\xb7  prodbyeville.com  \xc2\xb7  ")) + UpdateChecker::supportEmail,
                c.withTrimmedBottom (78.0f).removeFromBottom (20.0f), juce::Justification::centred);
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
    randomButton.onClick = [this] { room::Randomizer::randomizeSelected (proc, random); };
    randomButton.onRightClick = [this]
    {
        randomizePage.setVisible (! randomizePage.isVisible());
        if (randomizePage.isVisible())
            randomizePage.toFront (true);
    };
    content.addAndMakeVisible (menuButton);
    menuButton.onClick = [this] { showMainMenu(); };
    updates.onResult = [this]
    {
        menuButton.setBadge (updates.isUpdateAvailable());
        announcePending = true;   // a check found something: say so (if it's news)
    };
    menuButton.setBadge (updates.isUpdateAvailable());
    content.addAndMakeVisible (advancedButton);
    advancedButton.onClick = [this] { setAdvancedOpen (! proc.advancedOpen); };
    content.addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::bypass, bypassButton);
    setupKnob (mix, ParamIDs::mix, "wet", 15.0f, "", "", "", false,
              "room against your dry sound");
    setupKnob (output, ParamIDs::output, "out", 15.0f, "", "", "", false,
              "final level");
    for (auto* k : { &mix, &output })
        k->slider.getProperties().set ("popupBelow", true);   // the value bubble would be cut off above the window

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
    for (auto* b : { &recordButton, &roomSaveButton, &allRoomsButton, &clearRoomButton })
        b->getProperties().set ("pill", true);
    advancedButton.getProperties().set ("r5key", "cream");
    randomButton.getProperties().set ("r5key", "square");
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

    // --- Shape the room: intensity -> glitch (sync, amount) | tone -> space (sync, auto) | tail -> size ---
    setupKnob (intensity, ParamIDs::intensity, "intensity", 33.0f, "0", "100", "200", false,
              "how much room. past 100 it gets wild");
    setupKnob (smooth, ParamIDs::smooth, "smooth", 15.0f, "", "", "", false,
              "softens the attack, like a room mic");
    setupKnob (glitchAmount, ParamIDs::glitchAmount, "glitch", 15.0f, "", "", "", false,
              "how much it glitches");
    setupKnob (tone, ParamIDs::toneAmount, "tone", 33.0f, "", "", "", false,
              "the room's colour and ringing notes");
    setupKnob (space, ParamIDs::spaceAmount, "space", 33.0f, "", "", "", false,
              "the room's decay and width");
    setupKnob (tail, ParamIDs::tail, "tail", 33.0f, "0.3", "", "6 s", false,
              "how long it rings");
    setupKnob (size, ParamIDs::size, "size", 33.0f, "small", "", "big", false,
              "smaller or bigger room");
    setupKnob (imprint, ParamIDs::imprint, "imprint", 15.0f, "", "", "", false,
              "your sound takes on the room's timbre, tail or not");

    setupKnob (pitch, ParamIDs::pitch, "pitch", 15.0f, "-1 oct", "dry", "+1 oct", false,
              "an octave blended into the room: left down, right up, the middle is dry");
    pitch.slider.getProperties().set ("bipolar", true);
    pitch.slider.setDoubleClickReturnValue (true, 0.0);

    auto autoText = [this] (bool asTone)
    {
        return [this, asTone] (double v)
        {
            if (autoBalance.getToggleState())
            {
                const auto d = proc.getDetectedBalance();
                return "auto: " + juce::String (juce::roundToInt ((asTone ? 1.0f - d : d) * 100.0f)) + (asTone ? " tone" : " space");
            }
            return juce::String (juce::roundToInt (v * 100.0)) + (asTone ? " tone" : " space");
        };
    };
    tone.slider.textFromValueFunction = autoText (true);
    space.slider.textFromValueFunction = autoText (false);

    glitchAmount.slider.getProperties().set ("surface", "light");   // it sits on the light glitch module
    glitchKey.getProperties().set ("r5key", "black");
    glitchKey.setTooltip ("stutters the room");
    content.addAndMakeVisible (glitchKey);
    glitchAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::glitch, glitchKey);
    syncDot.getProperties().set ("r5key", "black");
    syncDot.setTooltip ("glitch in time with your song");
    content.addAndMakeVisible (syncDot);
    syncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::glitchSync, syncDot);
    autoBalance.getProperties().set ("r5key", "black");
    autoBalance.setTooltip ("tone and space from the sound");
    content.addAndMakeVisible (autoBalance);
    autoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::balanceAuto, autoBalance);

    // --- Advanced (attached) ---
    content.addAndMakeVisible (advancedPanel);

    // --- The bottom module: noise -> level / follow | rate ---
    content.addAndMakeVisible (noiseScreen);
    for (auto* b : { &noiseScreen.prev, &noiseScreen.next })
    {
        b->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        content.addAndMakeVisible (b);
    }
    noiseScreen.prev.setTooltip ("previous noise");
    noiseScreen.next.setTooltip ("next noise");
    noiseScreen.prev.onClick = [this] { noiseScreen.step (-1); };
    noiseScreen.next.onClick = [this] { noiseScreen.step (1); };
    setupKnob (noiseLevel, ParamIDs::noiseLevel, "level", 19.0f, "", "", "", false,
              "noise level");
    setupKnob (noiseFollow, ParamIDs::noiseFollow, "follow", 19.0f, "always", "", "input", false,
              "noise only while you play");
    noiseStop.getProperties().set ("r5key", "cream");
    noiseStop.setButtonText ("stop");
    noiseStop.setTooltip ("silence the noise when nothing plays");
    content.addAndMakeVisible (noiseStop);
    noiseStopAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::noiseStop, noiseStop);
    setupKnob (rate, ParamIDs::rate, "rate", 36.0f, "full", "", "low", false,
              "lowers the sample rate, cleanly");
    {
        const char* ids[3] = { ParamIDs::rateDry, ParamIDs::rateReverb, ParamIDs::rateNoise };
        const char* tips[3] = { "rate on the dry sound", "rate on the reverb", "rate on the noise" };
        for (int k = 0; k < 3; ++k)
        {
            rateKeys[k].getProperties().set ("r5key", "square");
            rateKeys[k].getProperties().set ("r5led", true);
            rateKeys[k].setButtonText (rateKeys[k].getButtonText().toUpperCase());
            rateKeys[k].setTooltip (tips[k]);
            content.addAndMakeVisible (rateKeys[k]);
            rateKeyAttachments[k] = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ids[k], rateKeys[k]);
        }
    }

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

    content.addChildComponent (randomizePage);
    content.addChildComponent (about);
    about.onTour = [this] { showTour(); };
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

void RoomAudioProcessorEditor::turnByHand (bool toneKnob, double value)
{
    (toneKnob ? tone : space).slider.setValue (value, juce::sendNotificationSync);
}

void RoomAudioProcessorEditor::setupKnob (Knob& k, const juce::String& id, const juce::String& name, float radius,
                                          const juce::String& lo, const juce::String& mid, const juce::String& hi, bool onBlack,
                                          const juce::String& tip)
{
    k.slider.setTooltip (tip);
    k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    // no value bubble: every value is printed on the panel
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
    if (id.isNotEmpty())   // tone has no parameter of its own (it drives balance)
        k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
    k.slider.setModTarget (&proc.modulation, id.isEmpty() ? -1 : room::Modulation::targetIndex (id));   // -1 = not an LFO target
}

void RoomAudioProcessorEditor::setTipsEnabled (bool on)
{
    proc.tipsEnabled = on;
    if (on && tooltips == nullptr)
        tooltips = std::make_unique<juce::TooltipWindow> (this, 700);
    else if (! on)
        tooltips.reset();
}

void RoomAudioProcessorEditor::newPreset (bool keepName)
{
    proc.resetToInit (keepName);
    for (auto* k : { &intensity, &smooth, &glitchAmount, &imprint, &pitch, &tone, &space, &tail, &size, &mix, &output, &noiseLevel, &noiseFollow, &rate })
        k->slider.refreshModulation();
    advancedPanel.refreshModulation();
    content.repaint();
}

void RoomAudioProcessorEditor::showTour (int startStep)
{
    auto pad = [] (juce::Rectangle<float> r) { return r.expanded (6.0f).toNearestInt(); };
    tour.setBounds (0, 0, contentWidth(), baseHeight);
    tour.start ({
        { pad (stageArea), "room",
          "room isn't a reverb. it learns a real space from any sound: drop audio here or press record, and it hears how that space rings, the tone it adds and how it decays. then it puts your sound in it. the cards are your rooms: click one to swap." },
        { pad (cellIntensity.getUnion (cellSpace).getUnion (cellSmooth).getUnion (autoKeyArea)), "intensity, tone and space",
          "intensity is how much of the room you hear. past 100 the room starts falling apart, past 175 it sings. tone is the room's colour and ringing notes, space its decay and width (auto sets both from your sound). underneath: smooth softens the attack, imprint gives your sound the room's timbre, pitch blends an octave into the room." },
        { pad (cellTail.getUnion (cellSize)), "tail and size",
          "tail is how long the room rings. size makes the same room smaller or bigger." },
        { pad (glitchModule), "glitch",
          "glitch grabs slices of the room and stutters them. sync puts it in time with your song, amount sets how often and how hard." },
        { pad (finishArea), "noise and rate",
          "noise lays a texture under everything: pick one, set its level, and follow makes it play only while sound comes in. rate resamples the sound down for grit; d, r and n choose what it reaches: dry, room, noise." },
        { pad (advancedButton.getBounds().toFloat().getUnion (randomButton.getBounds().toFloat())), "advanced and randomize",
          "advanced opens pre-delay, damping, width, ducking, freeze, the finish box (tape, auto eq, mud) and two lfos. randomize rolls a new sound; right-click it to choose what it may change." } });
    tour.showStep (startStep);
}

void RoomAudioProcessorEditor::showMainMenu()
{
    const auto version = UpdateChecker::currentVersion();
    juce::PopupMenu m;
    m.addItem ("new preset", [this] { newPreset(); });
    m.addItem ("reset all knobs", [this] { newPreset (true); });   // defaults, keeping the room and the studio's name
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
        m.addItem ("check for updates", [this]
        {
            updates.checkNow ([this] (bool answered)
            {
                menuButton.setBadge (updates.isUpdateAvailable());
                if (! answered)
                    EvilleLookAndFeel::showDialog (content, "check for updates",
                                                   "couldn't reach the update server. check your connection and try again.", { "ok" });
                else if (updates.isUpdateAvailable())
                    EvilleLookAndFeel::showDialog (content, "update available",
                                                   "room v" + updates.getLatestVersion() + " is out (you have v" + UpdateChecker::currentVersion() + ").",
                                                   { "download", "later" }, {},
                                                   [this] (int r, const juce::String&) { if (r == 1) juce::URL (updates.getDownloadUrl()).launchInDefaultBrowser(); });
                else
                    EvilleLookAndFeel::showDialog (content, "up to date",
                                                   "you have the latest room (v" + UpdateChecker::currentVersion() + ").", { "ok" });
            });
        });
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

bool RoomAudioProcessorEditor::overlayShowing() const
{
    const juce::Component* overlays[] = { &tour, &about, &licensePage, &browser, &roomBrowser,
                                          &saveDialog, &roomSaveDialog, &randomizePage };
    for (auto* c : overlays)
        if (c->isVisible())
            return true;
    return false;
}

bool RoomAudioProcessorEditor::wouldAnnounceUpdate() const
{
    if (updateAnnounced || ! updates.isUpdateAvailable())
        return false;
    if (auto s = UpdateChecker::openSettings())
    {
        // "later" holds that version off for a day; a newer one shows straight away
        const auto snoozed = s->getValue ("updateSnoozed");
        const auto at = s->getValue ("updateSnoozedAt").getLargeIntValue();
        if (snoozed == updates.getLatestVersion() && juce::Time::currentTimeMillis() - at < (juce::int64) 24 * 60 * 60 * 1000)
            return false;
    }
    return true;
}

void RoomAudioProcessorEditor::snoozeUpdate()
{
    updateAnnounced = true;
    if (auto s = UpdateChecker::openSettings())
    {
        s->setValue ("updateSnoozed", updates.getLatestVersion());
        s->setValue ("updateSnoozedAt", juce::String (juce::Time::currentTimeMillis()));
        s->saveIfNeeded();
    }
}

void RoomAudioProcessorEditor::maybeAnnounceUpdate()
{
    if (! wouldAnnounceUpdate())
    {
        announcePending = false;
        return;
    }
    if (overlayShowing() || ! isShowing())
        return;   // try again on the next tick, once nothing else is up
    announcePending = false;
    snoozeUpdate();   // shown once; "later" (or closing it) keeps it away for a day
    EvilleLookAndFeel::showDialog (content, "update available",
                                   "room v" + updates.getLatestVersion() + " is out (you have v" + UpdateChecker::currentVersion() + ").",
                                   { "download", "later" }, {},
                                   [this] (int r, const juce::String&) { if (r == 1) juce::URL (updates.getDownloadUrl()).launchInDefaultBrowser(); });
}

void RoomAudioProcessorEditor::showAbout()
{
    about.setVisible (true);
    about.toFront (true);
    about.grabKeyboardFocus();
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
    auto scale = juce::jlimit (0.6f, 1.4f, getHeight() > 0 ? (float) getHeight() / (float) baseHeight : proc.editorScale);
    if (getHeight() == 0)
    {
        // Opening: never bigger than the screen leaves room for (a host's own window bar and
        // the dock on a laptop). A window that doesn't fit makes some hosts float it on its own
        // (FL Studio on Mac shows it "Detached").
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto area = d->userBounds;
            const auto w = (float) (open ? baseWidth : closedWidth);
            const auto fit = std::min (((float) area.getHeight() - 160.0f) / (float) baseHeight, ((float) area.getWidth() - 120.0f) / w);
            scale = juce::jlimit (0.6f, scale, fit);
        }
    }
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
    {
        updates.checkIfDue();                  // does nothing if the last check is under 30 min old
        ticksUntilUpdateCheck = 30 * 60 * 5;   // look again in 5 minutes while the window stays open
    }
    // Tell about a new version by itself (not just the menu's dot).
    if (ticksUntilAnnounce > 0 && --ticksUntilAnnounce == 0)
        announcePending = true;
    if (announcePending)
        maybeAnnounceUpdate();

    // The tour waits for sign-in: while ROOM is locked it holds, then starts once the
    // sign-in page has gone (it used to give up if ROOM opened locked).
    if (proc.isLicenseLocked() && ticksUntilTour > 0)
        ticksUntilTour = 8;
    else if (ticksUntilTour > 0 && --ticksUntilTour == 0 && autoShowTour)
        if (auto settings = UpdateChecker::openSettings(); settings != nullptr && ! settings->getBoolValue ("tourDone", false))
            showTour();

    {
        auto& lib = proc.getLibrary();
        const auto* e = lib.find (proc.getCurrentPresetKey());
        presetBar.setPreset (lower (proc.getCurrentPresetName()), proc.isPresetModified(), e != nullptr && e->favorite, e != nullptr);
    }

    // LFO assignments can change from presets or the right-click menu.
    for (auto* k : { &intensity, &smooth, &glitchAmount, &imprint, &pitch, &tone, &space, &tail, &size, &mix, &output, &noiseLevel, &noiseFollow, &rate })
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

    // Auto sets tone and space from the sound: the knobs rest meanwhile.
    const auto isAuto = autoBalance.getToggleState();
    for (auto* k : { &tone, &space })
        if (k->slider.isEnabled() == isAuto)
            k->slider.setEnabled (! isAuto);
    glitchAmount.slider.setAlpha (glitchKey.getToggleState() ? 1.0f : 0.5f);

    // The live values
    content.repaint (controlsArea.toNearestInt());
    content.repaint (finishArea.toNearestInt());
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

void RoomAudioProcessorEditor::paintUnit (juce::Graphics& g)
{
    // The near-black brushed case; every control sits in its own pocket (cell).
    R5::drawCase (g, { 0.0f, 0.0f, (float) contentWidth(), (float) baseHeight });
    for (auto cell : { cellIntensity, cellTone, cellSpace, cellTail, cellSize, cellSmooth, cellImprint, cellPitch,
                       cellNoise, cellLevel, cellRate })
        R5::drawCell (g, cell);
    paintGlitchModule (g);
    if (proc.advancedOpen)
    {
        // Advanced is the opposite of the case: a light module, ink text, the same knobs.
        paintPaper (g, advancedArea.reduced (0.0f, 12.0f).withTrimmedRight (12.0f), 10.0f);
    }
    paintConsoleArt (g);
}

void RoomAudioProcessorEditor::paintPaper (juce::Graphics& g, juce::Rectangle<float> r, float corner)
{
    // A light module: warm off-white paper with a faint grain, a soft drop and a lit rim.
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.2f));
        g.fillRoundedRectangle (r.expanded ((float) i * 0.8f).translated (0.0f, (float) i * 0.8f), corner + (float) i);
    }
    juce::ColourGradient paper (juce::Colour (0xffefebe2), r.getX(), r.getY(), juce::Colour (0xffe2ddd2), r.getX(), r.getBottom(), false);
    g.setGradientFill (paper);
    g.fillRoundedRectangle (r, corner);
    if (auto grain = Hw::grain ((int) r.getWidth(), (int) r.getHeight(), false); grain.isValid())
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path shape;
        shape.addRoundedRectangle (r, corner);
        g.reduceClipRegion (shape);
        g.setOpacity (0.35f);
        g.drawImageAt (grain, (int) r.getX(), (int) r.getY());
    }
    juce::ColourGradient rim (juce::Colours::white.withAlpha (0.7f), r.getX(), r.getY(), juce::Colours::black.withAlpha (0.12f), r.getX(), r.getBottom(), false);
    g.setGradientFill (rim);
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
}

void RoomAudioProcessorEditor::paintGlitchModule (juce::Graphics& g)
{
    // Glitch is something else entirely: a light module, with soft waves drifting out of it
    // towards the room instead of signal lines.
    const auto r = glitchModule;
    paintPaper (g, r, 8.0f);
    R5::drawLabel (g, "glitch", { r.getX() + 22.0f + 69.0f, r.getY() + 12.0f }, 9.0f, 0.9f, true);
    R5::drawLabel (g, "amount", { r.getRight() - 44.0f - 62.0f, r.getY() + 38.0f }, 9.0f, 0.9f, true);
    g.setColour (juce::Colour (0xff1b1a18).withAlpha (0.18f));
    g.drawLine (r.getX() + 172.0f, r.getY() + 14.0f, r.getX() + 172.0f, r.getBottom() - 14.0f, 1.0f);

    // three waves out of its left edge, fading as they travel
    const auto x1 = r.getX() - 6.0f, x0 = r.getX() - 52.0f;
    for (int w = 0; w < 3; ++w)
    {
        const auto y = r.getY() + 16.0f + 14.0f * (float) w;
        const auto amp = 2.2f + 0.8f * (float) w, wavelength = 11.0f + 3.0f * (float) w;
        juce::Path p;
        for (auto x = x1; x >= x0; x -= 1.0f)
        {
            const auto t = (x1 - x) / (x1 - x0);
            const auto py = y + amp * (1.0f - 0.4f * t) * std::sin ((x1 - x) / wavelength * juce::MathConstants<float>::twoPi + (float) w);
            if (x == x1) p.startNewSubPath (x, py); else p.lineTo (x, py);
        }
        juce::ColourGradient fade (juce::Colour (0xfff5f3ef).withAlpha (0.6f), x1, y, juce::Colour (0xfff5f3ef).withAlpha (0.0f), x0, y, false);
        g.setGradientFill (fade);
        g.strokePath (p, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
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
                const auto h = 25.0f, w = h * 317.35f / 87.64f;
                logo->drawWithin (g, juce::Rectangle<float> (w, h).withPosition (52.0f, 23.5f), juce::RectanglePlacement::centred, 1.0f);
            }
    }

    // Top bar dividers: between the wet ring and the out label, and between the out ring and power
    g.setColour (Colours::moduleEdge);
    for (auto x : { (float) mix.slider.getBounds().getCentreX() + 25.0f, (float) bypassButton.getX() - 6.0f })
        g.drawLine (x, 24.0f, x, 52.0f, 1.0f);

    // The cells' titles (the values are drawn live, in paintValues)
    auto title = [&g] (const char* text, juce::Rectangle<float> cell, float dy, float height = 10.5f)
    {
        R5::drawLabel (g, text, { cell.getCentreX(), cell.getY() + dy }, height);
    };
    title ("intensity", cellIntensity, 17.0f);
    title ("tone", cellTone, 17.0f);
    title ("space", cellSpace, 17.0f);
    title ("tail", cellTail, 17.0f);
    title ("size", cellSize, 17.0f);
    title ("smooth", cellSmooth, 12.0f, 9.0f);
    title ("imprint", cellImprint, 12.0f, 9.0f);
    title ("pitch", cellPitch, 12.0f, 9.0f);
    title ("noise", cellNoise, 16.0f);
    title ("level", cellLevel, 16.0f, 9.5f);
    R5::drawLabel (g, "follow", { cellLevel.getCentreX(), cellLevel.getY() + 81.0f }, 9.5f);
    title ("rate", cellRate, 16.0f);

    paintSignalLines (g);
}

void RoomAudioProcessorEditor::paintSignalLines (juce::Graphics& g)
{
    // Fine lines in the gaps between the cells; they always stop short of a cell (by `gap`).
    // Solid white: the sound's path. Dotted white: a branch. Dotted crimson: a link.
    using R5::End;
    using R5::Stroke;
    constexpr float gap = 4.0f;
    const auto rowGapY = (cellIntensity.getBottom() + cellSmooth.getY()) * 0.5f;   // between rows 1 and 2
    const auto row2Y = cellSmooth.getCentreY() + 8.0f;                              // the keys' and knobs' height in row 2

    // intensity -> smooth (solid) and -> glitch (dotted, an open circle at its end)
    {
        const auto x = cellIntensity.getCentreX();
        R5::drawRoute (g, { { x, cellIntensity.getBottom() + gap }, { x, rowGapY } }, Stroke::solid, End::none, End::dot);
        R5::drawRoute (g, { { x, rowGapY }, { cellSmooth.getCentreX(), rowGapY }, { cellSmooth.getCentreX(), cellSmooth.getY() - gap } },
                       Stroke::solid, End::none, End::dot);
        R5::drawRoute (g, { { x + 3.0f, rowGapY }, { cellImprint.getCentreX(), rowGapY }, { cellImprint.getCentreX(), cellImprint.getY() - gap } },
                       Stroke::dotted, End::none, End::ring);
    }
    // imprint -> pitch
    R5::drawRoute (g, { { cellImprint.getRight() + gap, row2Y }, { cellPitch.getX() - gap, row2Y } }, Stroke::solid, End::none, End::none);

    // tone -> auto: solid down and along above amount to an elbow, then dotted into auto (open circle)
    {
        const auto elbowX = cellPitch.getRight() + 14.0f;
        const auto y = autoKeyArea.withTrimmedBottom (R5::glowRoom).getCentreY();
        R5::drawRoute (g, { { cellTone.getCentreX(), cellTone.getBottom() + gap }, { cellTone.getCentreX(), rowGapY },
                            { elbowX, rowGapY }, { elbowX, y } }, Stroke::solid, End::none, End::none);
        R5::drawRoute (g, { { elbowX + 4.0f, y }, { autoKeyArea.getX() - 5.0f, y } }, Stroke::dotted, End::none, End::ring);
    }
    // space -> auto: dotted crimson, from under space round into auto's right side
    {
        const auto x = cellSpace.getCentreX() + 27.0f;
        const auto y = autoKeyArea.withTrimmedBottom (R5::glowRoom).getCentreY();
        R5::drawRoute (g, { { x, cellSpace.getBottom() + gap }, { x, y }, { autoKeyArea.getRight() + 5.0f, y } },
                       Stroke::dottedRed, End::none, End::dot);
    }
    // tone -> space -> tail -> size, level with the knobs (sync sits on the tone -> space link)
    {
        const auto y = cellTone.getY() + knobRowY;
        for (auto [a, b] : { std::pair { cellTone, cellSpace }, { cellSpace, cellTail }, { cellTail, cellSize } })
            R5::drawRoute (g, { { a.getRight() + gap, y }, { b.getX() - gap, y } }, Stroke::solid, End::none, End::none);
    }
    // noise -> level / follow -> stop
    {
        const auto y = stopKeyArea.withTrimmedBottom (R5::glowRoom).getCentreY();
        R5::drawRoute (g, { { cellNoise.getRight() + gap, y }, { cellLevel.getX() - gap, y } }, Stroke::solid, End::none, End::none);
        R5::drawRoute (g, { { cellLevel.getRight() + gap, y }, { stopKeyArea.getX() - gap, y } }, Stroke::solid, End::none, End::none);
    }
}

void RoomAudioProcessorEditor::paintValues (juce::Graphics& g)
{
    // The values, live (they follow the LFOs too), printed under each cell's title
    auto value = [&g] (const juce::String& text, juce::Rectangle<float> cell, float dy = 35.0f, float height = 16.0f)
    {
        R5::drawValue (g, text, { cell.getCentreX(), cell.getY() + dy }, height);
    };
    value (juce::String (juce::roundToInt (intensity.slider.getValue() * 50.0)) + "%", cellIntensity);

    const auto isAuto = autoBalance.getToggleState();
    const auto d = proc.getDetectedBalance();
    const auto toneV = isAuto ? 1.0f - d : (float) tone.slider.getValue();
    const auto spaceV = isAuto ? d : (float) space.slider.getValue();
    value (juce::String (juce::roundToInt (toneV * 100.0f)), cellTone);
    value (juce::String (juce::roundToInt (spaceV * 100.0f)), cellSpace);
    const auto t = tail.slider.getValue();
    value (juce::String (t, t < 10.0 ? 1 : 0) + " S", cellTail);
    value (juce::String (juce::roundToInt (std::log (juce::jlimit (0.5, 2.0, size.slider.getValue()) / 0.5) / std::log (4.0) * 100.0)) + "%",
           cellSize);

    // Rate, in Hz
    {
        const auto hz = (float) rate.slider.getValue();
        value (hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + " KHZ" : juce::String (juce::roundToInt (hz)) + " HZ", cellRate, 37.0f, 17.0f);
    }

    // The noise profile's name, between its arrows
    {
        auto* p = proc.apvts.getParameter (ParamIDs::noiseType);
        R5::drawLabel (g, p->getCurrentValueAsText(), noiseNameArea.toFloat().getCentre(), 11.5f, 0.95f);
        for (auto* b : { &noiseScreen.prev, &noiseScreen.next })
        {
            const auto c = b->getBounds().toFloat().getCentre();
            const auto dir = b == &noiseScreen.prev ? -1.0f : 1.0f;
            juce::Path arrow;
            arrow.startNewSubPath (c.x - dir * 7.0f, c.y);
            arrow.lineTo (c.x + dir * 7.0f, c.y);
            arrow.startNewSubPath (c.x + dir * 3.0f, c.y - 4.0f * std::abs (dir));
            arrow.lineTo (c.x + dir * 7.0f, c.y);
            arrow.lineTo (c.x + dir * 3.0f, c.y + 4.0f * std::abs (dir));
            g.setColour (Colours::dustWhite.withAlpha (b->isMouseOver() ? 1.0f : 0.7f));
            g.strokePath (arrow, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
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
    about.setBounds (full);
    randomizePage.setBounds (full);
    licensePage.setBounds (full);
    licensePage.update();
    roomBrowser.setBounds (full);
    saveDialog.setBounds (full);
    roomSaveDialog.setBounds (full);
    staticLayer = {};   // the print follows the layout

    // --- Top bar: menu, wordmark, studios box (save beside the heart), advanced, randomize, wet, out, power ---
    const int keyY = 16, key = 40;
    menuButton.setBounds (14, keyY + 6, 28, 28);
    presetBar.setBounds (152, keyY, 300, key);
    advancedButton.setBounds (476, keyY + 2, 104, key + 4);   // + 8: room for the lit glow below the key
    randomButton.setBounds (590, keyY + 2, key - 4, key + 4);
    // wet ring | out ring | power: each label sits left of its ring, a divider between the groups
    for (auto [k, x] : { std::pair { &mix, 704 }, { &output, 798 } })
    {
        k->slider.setBounds (juce::Rectangle<int> (44, 44).withCentre ({ x, keyY + key / 2 }));
        k->label.setBounds (juce::Rectangle<int> (40, 16).withCentre ({ x - 41, keyY + key / 2 }));
        k->label.setJustificationType (juce::Justification::centredRight);
    }
    bypassButton.setBounds (juce::Rectangle<int> (50, 50).withCentre ({ 860, keyY + 20 }));

    // --- The stage, its chips, and the room box in the middle ---
    const auto st = stageArea.toNearestInt();
    loadScreen.setBounds (st);
    roomScreen.setBounds (loadScreen.boxArea());   // stage coordinates from here on
    recordButton.setBounds (16, 14, 116, 32);
    roomSaveButton.setBounds (140, 14, 124, 32);
    clearRoomButton.setBounds (272, 14, 84, 32);
    allRoomsButton.setBounds (st.getWidth() - 16 - 120, 14, 120, 32);
    toneScreen.setVisible (false);   // the tone now shows in the room's info (click the box)

    // --- The cells (round 5): every knob centred in its cell, under its title and value ---
    // A knob's bounds are centred on the knob and wide enough for its lfo depth dots.
    auto placeKnob = [] (Knob& k, juce::Point<float> c)
    {
        const auto r = (int) (float) k.slider.getProperties()["knobRadius"];
        k.slider.setBounds (juce::Rectangle<int> (2 * (r + 26), 2 * r + 12).withCentre (c.toInt()));
        k.label.setVisible (false);   // titled in the case's print
    };
    auto knobIn = [] (juce::Rectangle<float> cell, float dy) { return juce::Point<float> (cell.getCentreX(), cell.getY() + dy); };
    placeKnob (intensity, knobIn (cellIntensity, knobRowY));
    placeKnob (tone, knobIn (cellTone, knobRowY));
    placeKnob (space, knobIn (cellSpace, knobRowY));
    placeKnob (tail, knobIn (cellTail, knobRowY));
    placeKnob (size, knobIn (cellSize, knobRowY));
    placeKnob (smooth, knobIn (cellSmooth, 39.0f));
    placeKnob (imprint, knobIn (cellImprint, 39.0f));
    placeKnob (pitch, knobIn (cellPitch, 39.0f));
    {
        // the glitch module: [glitch] [sync]  |  amount (o)
        const auto y = glitchModule.getY() + 37.0f;
        const auto x0 = glitchModule.getX() + 22.0f;
        glitchKey.setBounds (juce::Rectangle<float> (x0, y - 17.0f, 72.0f, 36.0f).toNearestInt());
        syncDot.setBounds (juce::Rectangle<float> (x0 + 80.0f, y - 17.0f, 58.0f, 36.0f).toNearestInt());
        placeKnob (glitchAmount, { glitchModule.getRight() - 44.0f, y });
    }
    autoBalance.setBounds (autoKeyArea.toNearestInt());

    // --- Advanced (the right side, full height) ---
    advancedPanel.setBounds (advancedArea.reduced (0.0f, 12.0f).withTrimmedRight (12.0f).toNearestInt().reduced (4));

    // --- Bottom: noise [screen] -> level / follow -> stop | rate ---
    {
        const auto n = cellNoise;
        noiseNameArea = juce::Rectangle<int> (150, 18).withCentre ({ (int) n.getCentreX(), (int) n.getY() + 37 });
        noiseScreen.prev.setBounds (juce::Rectangle<int> (26, 20).withCentre ({ noiseNameArea.getX() - 12, noiseNameArea.getCentreY() }));
        noiseScreen.next.setBounds (juce::Rectangle<int> (26, 20).withCentre ({ noiseNameArea.getRight() + 12, noiseNameArea.getCentreY() }));
        noiseScreen.setBounds (juce::Rectangle<float> (n.getX() + 14.0f, n.getY() + 52.0f, n.getWidth() - 28.0f, 80.0f).toNearestInt());
    }
    placeKnob (noiseLevel, knobIn (cellLevel, 46.0f));
    placeKnob (noiseFollow, knobIn (cellLevel, 112.0f));
    noiseStop.setBounds (stopKeyArea.toNearestInt());
    placeKnob (rate, { cellRate.getX() + 82.0f, cellRate.getY() + 94.0f });
    for (int k = 0; k < 3; ++k)
        rateKeys[k].setBounds (juce::Rectangle<int> (34, 42).withCentre ({ (int) cellRate.getX() + 170 + k * 44, (int) cellRate.getY() + 98 }));
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
    if (key != proc.getCurrentRoomKey())
        proc.resetIntensityForNewRoom();
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
