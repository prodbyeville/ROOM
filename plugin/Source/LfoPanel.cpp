#include "LfoPanel.h"
#include "UiColours.h"
#include "Screens.h"
#include "EvilleLookAndFeel.h"

LfoModule::LfoModule (RoomAudioProcessor& p, int index) : proc (p), lfo (index)
{
    {
        juce::StringArray shapes;
        for (auto& n : room::Lfo::shapeNames())
            shapes.add (n.toLowerCase());
        shape.addItemList (shapes, 1);
    }
    division.addItemList (room::Lfo::divisionNames(), 1);
    for (auto* c : { &shape, &division })
    {
        c->setColour (juce::ComboBox::backgroundColourId, Colours::glass);
        c->setColour (juce::ComboBox::outlineColourId, Colours::outline);
        c->setColour (juce::ComboBox::textColourId, Colours::screenText);
        c->setColour (juce::ComboBox::arrowColourId, Colours::screenDim);
        addAndMakeVisible (c);
    }
    rate.setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 26);
    rate.getProperties().set ("bigReadout", true);
    rate.setColour (juce::Slider::textBoxTextColourId, Colours::screenText);
    rate.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (rate);
    for (auto* b : { &sync, &retrig })
    {
        b->getProperties().set ("style", "minikey");
        b->getProperties().set ("surface", "black");
        addAndMakeVisible (b);
    }
    sync.setTooltip ("lock the rate to the song tempo");
    retrig.setTooltip ("restart the lfo when you press play");

    shapeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ParamIDs::lfoShape (lfo), shape);
    divAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, ParamIDs::lfoDiv (lfo), division);
    syncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::lfoSync (lfo), sync);
    retrigAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::lfoRetrig (lfo), retrig);
    rateAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, ParamIDs::lfoRate (lfo), rate);

    trail.assign (120, 0.0f);
    startTimerHz (30);
}

juce::Rectangle<int> LfoModule::handleArea() const
{
    return getLocalBounds().removeFromTop (32).removeFromLeft (88);
}

void LfoModule::flash()
{
    flashAlpha = 1.0f;
    repaint();
}

static juce::String targetName (int t)
{
    static const std::map<juce::String, juce::String> names {
        { "intensity", "intensity" }, { "balance", "balance" }, { "tail", "tail" }, { "space", "space" },
        { "mix", "wet" }, { "output", "out" }, { "tape", "tape" }, { "autoEq", "auto eq" },
        { "preDelay", "pre-delay" }, { "size", "size" }, { "damping", "damping" }, { "duck", "ducking" },
        { "duckRelease", "release" } };
    const auto id = room::Modulation::targetIds()[t];
    const auto it = names.find (id);
    return it != names.end() ? it->second : id;
}

static juce::String chipText (int target, float depth)
{
    return targetName (target) + "  " + juce::String (juce::roundToInt (depth * 100.0f)) + "%";
}

std::vector<LfoModule::TargetChip> LfoModule::targetChips() const
{
    std::vector<TargetChip> out;
    const auto font = Fonts::sans (12.5f);
    int x = targetsArea.getX(), y = targetsArea.getY();
    for (auto& slot : proc.modulation.slots[(size_t) lfo])
        if (const auto t = slot.target.load(); t >= 0)
        {
            const auto w = juce::GlyphArrangement::getStringWidthInt (font, chipText (t, slot.depth.load())) + 30;
            if (x > targetsArea.getX() && x + w > targetsArea.getRight())
            {
                x = targetsArea.getX();
                y += 27;
            }
            out.push_back ({ t, { x, y, w, 23 } });
            x += w + 6;
        }
    return out;
}

int LfoModule::chipAt (juce::Point<int> p) const
{
    for (auto& c : targetChips())
        if (c.bounds.contains (p))
            return c.target;
    return -1;
}

void LfoModule::mouseDown (const juce::MouseEvent& e)
{
    const auto t = chipAt (e.getPosition());
    if (t < 0)
        return;
    if (e.mods.isPopupMenu())
    {
        const auto depth = proc.modulation.depthOf (lfo, t);
        juce::PopupMenu menu;
        menu.addSectionHeader (targetName (t) + "  " + juce::String (juce::roundToInt (depth * 100.0f)) + "%");
        for (auto d : { 0.1f, 0.25f, 0.5f, 1.0f })
            menu.addItem (juce::String (juce::roundToInt (d * 100.0f)) + "%", true, std::abs (std::abs (depth) - d) < 0.005f,
                          [this, t, d, depth] { proc.modulation.assign (lfo, t, depth < 0.0f ? -d : d); repaint(); });
        menu.addSeparator();
        menu.addItem ("invert", true, depth < 0.0f, [this, t, depth] { proc.modulation.assign (lfo, t, -depth); repaint(); });
        menu.addItem ("remove", [this, t] { proc.modulation.remove (lfo, t); repaint(); });
        EvilleLookAndFeel::showMenu (menu, *this);
        return;
    }
    dragTarget = t;
    dragStartDepth = proc.modulation.depthOf (lfo, t);
}

void LfoModule::mouseUp (const juce::MouseEvent&)
{
    dragTarget = -1;
}

void LfoModule::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const auto t = chipAt (e.getPosition()); t >= 0)
    {
        proc.modulation.remove (lfo, t);
        dragTarget = -1;
        repaint();
    }
}

void LfoModule::mouseDrag (const juce::MouseEvent& e)
{
    if (dragTarget >= 0)
    {
        const auto pxPerFull = e.mods.isShiftDown() ? 600.0f : 150.0f;
        proc.modulation.assign (lfo, dragTarget,
                                juce::jlimit (-1.0f, 1.0f, dragStartDepth - (float) e.getDistanceFromDragStartY() / pxPerFull));
        repaint();
        return;
    }
    if (! handleArea().contains (e.getMouseDownPosition()))
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! container->isDragAndDropActive())
        {
            // A small coloured chip follows the mouse while dragging.
            juce::Image chip (juce::Image::ARGB, 64, 22, true);
            {
                juce::Graphics g (chip);
                g.setColour (ModSlider::lfoColour (lfo));
                g.fillRoundedRectangle (0.0f, 0.0f, 64.0f, 22.0f, 6.0f);
                g.setColour (Colours::background);
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText ("LFO " + juce::String (lfo + 1), 0, 0, 64, 22, juce::Justification::centred);
            }
            container->startDragging (ModSlider::dragDescription (lfo), this, juce::ScaledImage (chip), false);
        }
}

void LfoModule::timerCallback()
{
    const auto synced = sync.getToggleState();
    division.setVisible (synced);
    rate.setVisible (! synced);

    std::rotate (trail.begin(), trail.begin() + 1, trail.end());
    trail.back() = proc.modulation.liveValue[(size_t) lfo].load();
    flashAlpha = std::max (0.0f, flashAlpha - 0.025f);
    repaint();
}

void LfoModule::paint (juce::Graphics& g)
{
    const auto col = ModSlider::lfoColour (lfo);

    // Drag handle: a key with a grip and the lfo's name; a caption under it.
    const auto h = handleArea().toFloat();
    Hw::drawKey (g, h, false, false, false);
    const auto cap = Hw::keyCap (h);
    g.setColour (col);
    for (int k = 0; k < 3; ++k)   // grip dots
        for (int j = 0; j < 2; ++j)
            g.fillEllipse (cap.getX() + 7.0f + j * 5.0f, cap.getCentreY() - 6.5f + k * 4.5f, 2.6f, 2.6f);
    g.setColour (Colours::dustWhite);
    g.setFont (Fonts::sans (14.0f, Fonts::bold));
    g.drawText ("lfo " + juce::String (lfo + 1), cap.withTrimmedLeft (21.0f), juce::Justification::centredLeft);
    g.setColour (Colours::screenDim);
    g.setFont (Fonts::sans (12.0f));
    g.drawText ("drag the handle onto a knob", captionArea.toFloat(), juce::Justification::centredLeft);

    // Scope: the last couple of seconds of this LFO, on glass
    const auto glass = Hw::drawScreen (g, waveArea.toFloat());
    const auto w = glass.reduced (8.0f, 10.0f);
    g.setColour (Colours::screenDim.withAlpha (0.18f));
    g.drawHorizontalLine (juce::roundToInt (w.getCentreY()), w.getX(), w.getRight());
    juce::Path p;
    for (size_t i = 0; i < trail.size(); ++i)
    {
        const auto x = w.getX() + w.getWidth() * (float) i / (float) (trail.size() - 1);
        const auto y = w.getCentreY() - trail[i] * (w.getHeight() * 0.45f);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    Hw::strokeGlow (g, p, Colours::screenText.withAlpha (0.9f), 1.4f);
    const auto dotPos = juce::Point<float> (w.getRight() - 1.0f, w.getCentreY() - trail.back() * (w.getHeight() * 0.45f));
    g.setColour (Colours::crimson.withAlpha (0.35f));
    g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (dotPos));
    g.setColour (Colours::crimson);
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (dotPos));
    Hw::drawScreenOverlay (g, glass);

    // Target chips: dot + knob + amount. Drag up/down to change the amount.
    const auto chips = targetChips();
    if (chips.empty())
    {
        g.setColour (Colours::screenDim);
        g.setFont (Fonts::sans (12.5f));
        g.drawFittedText ("moves nothing yet. drag the handle onto a knob, or right-click a knob.", targetsArea.withHeight (36),
                          juce::Justification::topLeft, 2);
    }
    for (auto& c : chips)
    {
        const auto depth = proc.modulation.depthOf (lfo, c.target);
        const auto b = c.bounds.toFloat();
        g.setColour (c.target == dragTarget ? col.withAlpha (0.28f) : Colours::glass);
        g.fillRoundedRectangle (b, 10.0f);
        g.setColour (col.withAlpha (0.7f));
        g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);
        const auto dot = juce::Rectangle<float> (10.0f, 10.0f).withCentre ({ b.getX() + 12.0f, b.getCentreY() });
        g.setColour (col.withAlpha (0.25f));
        g.fillEllipse (dot);
        juce::Path amt;
        amt.addPieSegment (dot, 0.0f, juce::MathConstants<float>::twoPi * std::abs (depth), 0.0f);
        g.setColour (col);
        g.fillPath (amt);
        g.setColour (Colours::dustWhite);
        g.setFont (Fonts::sans (12.5f));
        g.drawText (chipText (c.target, depth), b.withTrimmedLeft (22.0f), juce::Justification::centredLeft);
    }
}

void LfoModule::paintOverChildren (juce::Graphics& g)
{
    if (flashAlpha > 0.0f)
    {
        g.setColour (Colours::crimson.withAlpha (0.8f * flashAlpha));
        g.drawRoundedRectangle (waveArea.toFloat().expanded (2.0f), 8.0f, 2.0f);
    }
}

void LfoModule::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (32);
    top.removeFromLeft (handleArea().getWidth() + 8);
    shape.setBounds (top.reduced (0, 3));
    captionArea = r.removeFromTop (20).withTrimmedLeft (2);
    r.removeFromTop (4);

    const int fixedBelow = 10 + 34 + 8 + 30 + 10 + 48;
    waveArea = r.removeFromTop (std::max (60, std::min (200, r.getHeight() - fixedBelow)));
    r.removeFromTop (10);

    // Rate: a full-width slider (or the tempo division when synced)
    const auto controls = r.removeFromTop (34);
    rate.setBounds (controls);
    division.setBounds (controls.reduced (0, 3));
    r.removeFromTop (8);

    auto keys = r.removeFromTop (30);
    sync.setBounds (keys.removeFromLeft (keys.getWidth() / 2).withWidth (96));
    retrig.setBounds (keys.withWidth (100));

    r.removeFromTop (10);
    targetsArea = r;
}

//==============================================================================
// Compact card

LfoCard::LfoCard (RoomAudioProcessor& p, int index) : proc (p), lfo (index)
{
    for (auto* b : { &sync, &retrig })
    {
        b->getProperties().set ("style", "minikey");
        b->getProperties().set ("surface", "black");
        addAndMakeVisible (b);
    }
    syncAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::lfoSync (lfo), sync);
    retrigAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::lfoRetrig (lfo), retrig);
    setTooltip ("lfo " + juce::String (lfo + 1) + ": click to open, drag onto a knob to assign");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    trail.assign (90, 0.0f);
    startTimerHz (30);
}

juce::Rectangle<int> LfoCard::screenArea() const
{
    return getLocalBounds().withTrimmedBottom (40);
}

void LfoCard::resized()
{
    auto r = getLocalBounds().removeFromBottom (30);
    sync.setBounds (r.removeFromLeft (r.getWidth() / 2).withWidth (96));
    retrig.setBounds (r.withWidth (100));
}

void LfoCard::timerCallback()
{
    std::rotate (trail.begin(), trail.begin() + 1, trail.end());
    trail.back() = proc.modulation.liveValue[(size_t) lfo].load();
    repaint (screenArea());
}

void LfoCard::paint (juce::Graphics& g)
{
    const auto glass = Hw::drawScreen (g, screenArea().toFloat());
    auto inner = glass.reduced (9.0f, 6.0f);
    g.setColour (Colours::screenText);
    g.setFont (Fonts::mono (12.0f));
    g.drawText ("lfo " + juce::String (lfo + 1), inner.removeFromTop (16.0f), juce::Justification::centredLeft);

    // What it moves
    juce::StringArray parts;
    for (auto& slot : proc.modulation.slots[(size_t) lfo])
        if (const auto t = slot.target.load(); t >= 0)
            parts.add (targetName (t) + " " + juce::String (juce::roundToInt (slot.depth.load() * 100.0f)) + "%");
    auto textRow = inner.removeFromBottom (14.0f);
    g.setColour (parts.isEmpty() ? Colours::screenDim : Colours::screenText);
    g.setFont (Fonts::mono (10.5f));
    g.drawText (parts.isEmpty() ? juce::String ("drag onto a knob")
                                : parts.size() <= 2 ? parts.joinIntoString ("  ") : parts[0] + "  +" + juce::String (parts.size() - 1),
                textRow, juce::Justification::centred, true);

    // Waveform
    auto w = inner.reduced (2.0f, 4.0f);
    juce::Path p;
    for (size_t i = 0; i < trail.size(); ++i)
    {
        const auto x = w.getX() + w.getWidth() * (float) i / (float) (trail.size() - 1);
        const auto y = w.getCentreY() - trail[i] * w.getHeight() * 0.45f;
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    Hw::strokeGlow (g, p, Colours::screenText.withAlpha (0.85f), 1.2f, 0.8f);
    const auto dot = juce::Point<float> (w.getRight(), w.getCentreY() - trail.back() * w.getHeight() * 0.45f);
    g.setColour (Colours::crimson);
    g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
    Hw::drawScreenOverlay (g, glass);
}

void LfoCard::mouseDrag (const juce::MouseEvent& e)
{
    if (dragged || e.getDistanceFromDragStart() < 6)
        return;
    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        if (! container->isDragAndDropActive())
        {
            dragged = true;
            juce::Image chip (juce::Image::ARGB, 64, 22, true);
            {
                juce::Graphics cg (chip);
                cg.setColour (ModSlider::lfoColour (lfo));
                cg.fillRoundedRectangle (0.0f, 0.0f, 64.0f, 22.0f, 6.0f);
                cg.setColour (Colours::dustWhite);
                cg.setFont (Fonts::sans (12.0f, Fonts::bold));
                cg.drawText ("lfo " + juce::String (lfo + 1), 0, 0, 64, 22, juce::Justification::centred);
            }
            container->startDragging (ModSlider::dragDescription (lfo), this, juce::ScaledImage (chip), false);
        }
}

void LfoCard::mouseUp (const juce::MouseEvent& e)
{
    if (! dragged && screenArea().contains (e.getPosition()) && onOpen)
        onOpen (lfo);
    dragged = false;
}

//==============================================================================
// Full controls

LfoPanel::LfoPanel (RoomAudioProcessor& p) : lfo1 (p, 0), lfo2 (p, 1)
{
    addAndMakeVisible (lfo1);
    addAndMakeVisible (lfo2);
    done.getProperties().set ("key", "dark");
    addAndMakeVisible (done);
    done.onClick = [this] { if (onDone) onDone(); };
    done.setTooltip ("back to the advanced knobs");
}

void LfoPanel::showLfo (int l)
{
    // Both lfos are on screen, stacked; the one you clicked lights up briefly.
    shown = l;
    (l == 0 ? lfo1 : lfo2).flash();
}

void LfoPanel::paint (juce::Graphics& g)
{
    // A crease between the two lfos
    const auto y = (float) (lfo1.getBottom() + lfo2.getY()) * 0.5f;
    Hw::drawGroove (g, { 0.0f, y }, { (float) getWidth(), y }, true);
}

void LfoPanel::resized()
{
    auto r = getLocalBounds();
    done.setBounds (r.removeFromTop (34).removeFromRight (78));
    r.removeFromTop (8);
    const auto gap = 22;
    const auto h = (r.getHeight() - gap) / 2;
    lfo1.setBounds (r.removeFromTop (h));
    r.removeFromTop (gap);
    lfo2.setBounds (r);
}
