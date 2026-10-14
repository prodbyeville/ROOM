#include "LfoPanel.h"
#include "UiColours.h"

LfoModule::LfoModule (RoomAudioProcessor& p, int index) : proc (p), lfo (index)
{
    shape.addItemList (room::Lfo::shapeNames(), 1);
    division.addItemList (room::Lfo::divisionNames(), 1);
    for (auto* c : { &shape, &division })
    {
        c->setColour (juce::ComboBox::backgroundColourId, Colours::background);
        c->setColour (juce::ComboBox::outlineColourId, Colours::outline);
        c->setColour (juce::ComboBox::textColourId, Colours::text);
        c->setColour (juce::ComboBox::arrowColourId, Colours::dim);
        addAndMakeVisible (c);
    }
    rate.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 18);
    rate.setColour (juce::Slider::trackColourId, ModSlider::lfoColour (lfo));
    rate.setColour (juce::Slider::backgroundColourId, Colours::background);
    rate.setColour (juce::Slider::thumbColourId, Colours::text);
    rate.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (rate);
    for (auto* b : { &sync, &retrig })
        addAndMakeVisible (b);
    sync.setTooltip ("Lock the rate to the song tempo");
    retrig.setTooltip ("Restart the LFO when you press play");

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
    return getLocalBounds().reduced (10, 8).removeFromTop (26).removeFromLeft (150);
}

static juce::String targetName (int t)
{
    static const std::map<juce::String, juce::String> names {
        { "intensity", "Intensity" }, { "balance", "Balance" }, { "tail", "Tail" }, { "space", "Space" },
        { "mix", "Dry/Wet" }, { "output", "Out" }, { "tape", "Tape" }, { "autoEq", "Auto EQ" },
        { "preDelay", "Pre-delay" }, { "size", "Size" }, { "damping", "Damping" }, { "duck", "Ducking" },
        { "duckRelease", "Release" } };
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
    const juce::Font font (juce::FontOptions (11.0f, juce::Font::bold));
    int x = targetsArea.getX(), y = targetsArea.getY();
    for (auto& slot : proc.modulation.slots[(size_t) lfo])
        if (const auto t = slot.target.load(); t >= 0)
        {
            const auto w = juce::GlyphArrangement::getStringWidthInt (font, chipText (t, slot.depth.load())) + 30;
            if (x > targetsArea.getX() && x + w > targetsArea.getRight())
            {
                x = targetsArea.getX();
                y += 24;
            }
            out.push_back ({ t, { x, y, w, 20 } });
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
        menu.addItem ("Invert", true, depth < 0.0f, [this, t, depth] { proc.modulation.assign (lfo, t, -depth); repaint(); });
        menu.addItem ("Remove", [this, t] { proc.modulation.remove (lfo, t); repaint(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
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
    repaint();
}

void LfoModule::paint (juce::Graphics& g)
{
    const auto col = ModSlider::lfoColour (lfo);
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);

    // Drag handle
    const auto h = handleArea().toFloat();
    g.setColour (col.withAlpha (0.18f));
    g.fillRoundedRectangle (h, 6.0f);
    g.setColour (col);
    g.drawRoundedRectangle (h.reduced (0.5f), 6.0f, 1.0f);
    for (int k = 0; k < 3; ++k)   // grip dots
        for (int j = 0; j < 2; ++j)
            g.fillEllipse (h.getX() + 9.0f + j * 5.0f, h.getY() + 7.0f + k * 5.0f, 3.0f, 3.0f);
    g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    g.drawText ("LFO " + juce::String (lfo + 1), h.withTrimmedLeft (24.0f), juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("drag to a knob", h.withTrimmedRight (8.0f), juce::Justification::centredRight);

    // Scope: the last couple of seconds of this LFO
    const auto w = waveArea.toFloat();
    g.setColour (Colours::background);
    g.fillRoundedRectangle (w, 5.0f);
    juce::Path p;
    for (size_t i = 0; i < trail.size(); ++i)
    {
        const auto x = w.getX() + w.getWidth() * (float) i / (float) (trail.size() - 1);
        const auto y = w.getCentreY() - trail[i] * (w.getHeight() * 0.42f);
        if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
    }
    g.setColour (col.withAlpha (0.9f));
    g.strokePath (p, juce::PathStrokeType (1.6f));
    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ w.getRight() - 1.0f, w.getCentreY() - trail.back() * (w.getHeight() * 0.42f) }));

    // Target chips: dot + knob + amount. Drag up/down to change the amount.
    const auto chips = targetChips();
    if (chips.empty())
    {
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText ("Drag the LFO handle onto any knob (or right-click a knob).", targetsArea,
                          juce::Justification::centredLeft, 2);
    }
    for (auto& c : chips)
    {
        const auto depth = proc.modulation.depthOf (lfo, c.target);
        const auto b = c.bounds.toFloat();
        g.setColour (c.target == dragTarget ? col.withAlpha (0.28f) : Colours::background);
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
        g.setColour (Colours::text);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (chipText (c.target, depth), b.withTrimmedLeft (22.0f), juce::Justification::centredLeft);
    }
}

void LfoModule::resized()
{
    auto r = getLocalBounds().reduced (10, 8);
    auto top = r.removeFromTop (26);
    top.removeFromLeft (158);
    shape.setBounds (top.removeFromLeft (std::min (130, top.getWidth())));
    r.removeFromTop (6);

    auto controls = r.removeFromTop (30);
    sync.setBounds (controls.removeFromLeft (64));
    retrig.setBounds (controls.removeFromRight (70));
    controls.removeFromRight (8);
    auto rateArea = controls;
    rate.setBounds (rateArea);
    division.setBounds (rateArea.withSizeKeepingCentre (std::min (rateArea.getWidth(), 120), 24).withX (rateArea.getX()));

    r.removeFromTop (6);
    targetsArea = r.removeFromBottom (44);
    waveArea = r.reduced (0, 2);
}
