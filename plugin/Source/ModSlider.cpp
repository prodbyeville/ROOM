#include "ModSlider.h"
#include "UiColours.h"
#include "EvilleLookAndFeel.h"

juce::Colour ModSlider::lfoColour (int lfo)
{
    return lfo == 0 ? juce::Colour (0xffd03a3a) : juce::Colour (0xff9a9288);   // crimson, warm grey
}

void ModSlider::setModTarget (room::Modulation* m, int targetIndex)
{
    mod = m;
    target = targetIndex;
    updateTimer();
}

void ModSlider::updateTimer()
{
    const auto assigned = mod != nullptr && target >= 0 && (mod->isAssigned (0, target) || mod->isAssigned (1, target));
    if (assigned && ! isTimerRunning())
    {
        startTimerHz (30);
        repaint();
    }
    else if (! assigned && isTimerRunning())
    {
        stopTimer();
        repaint();
    }
}

void ModSlider::timerCallback()
{
    updateTimer();
    repaint();
}

ModSlider::RingGeometry ModSlider::ringGeometry() const
{
    const auto kb = EvilleLookAndFeel::knobBounds (*this);
    return { kb.getCentre(), kb.getWidth() * 0.5f + 2.5f };
}

juce::Rectangle<float> ModSlider::depthDotBounds (int lfo) const
{
    if (mod == nullptr || target < 0 || ! mod->isAssigned (lfo, target))
        return {};
    // Stack the dots for the assigned LFOs just right of the ring.
    int k = 0;
    for (int l = 0; l < lfo; ++l)
        if (mod->isAssigned (l, target))
            ++k;
    const auto geo = ringGeometry();
    const auto x = geo.centre.x + geo.ringR + 16.0f;
    if (x + 6.0f > (float) getWidth())
        return {};   // a tiny knob: use right-click or the LFO panel instead
    return juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ x, geo.centre.y - 6.0f + (float) k * 16.0f });
}

int ModSlider::dotAt (juce::Point<float> p) const
{
    for (int l = 0; l < room::Modulation::numLfos; ++l)
        if (const auto b = depthDotBounds (l); ! b.isEmpty() && b.expanded (3.0f).contains (p))
            return l;
    return -1;
}

void ModSlider::paint (juce::Graphics& g)
{
    juce::Slider::paint (g);
    if (mod == nullptr || target < 0)
        return;

    const auto geo = ringGeometry();
    const auto ringR = geo.ringR;
    const auto centre = geo.centre;
    const auto rp = getRotaryParameters();
    const auto base = (float) valueToProportionOfLength (getValue());
    auto angleFor = [&] (float norm)
    {
        return rp.startAngleRadians + juce::jlimit (0.0f, 1.0f, norm) * (rp.endAngleRadians - rp.startAngleRadians);
    };

    if (dropHighlight)
    {
        g.setColour ((EvilleLookAndFeel::onBlack (*this) ? Colours::dustWhite : Colours::ink).withAlpha (0.25f));
        g.drawEllipse (juce::Rectangle<float> (ringR * 2.0f + 8.0f, ringR * 2.0f + 8.0f).withCentre (centre), 2.0f);
    }

    int ringIndex = 0;
    for (int l = 0; l < room::Modulation::numLfos; ++l)
    {
        if (! mod->isAssigned (l, target))
            continue;
        const auto depth = mod->depthOf (l, target);
        const auto r = ringR + ringIndex * 4.0f;
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, r, r, 0.0f, angleFor (base - std::abs (depth)), angleFor (base + std::abs (depth)), true);
        g.setColour (lfoColour (l).withAlpha (0.85f));
        g.strokePath (arc, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        ++ringIndex;
    }

    if (ringIndex > 0)
    {
        // Where the LFOs have the knob right now.
        const auto now = angleFor (base + mod->liveOffset[(size_t) target].load());
        const auto r = ringR;
        const juce::Point<float> dot (centre.x + r * std::sin (now), centre.y - r * std::cos (now));
        g.setColour (EvilleLookAndFeel::onBlack (*this) ? Colours::dustWhite : Colours::ink);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
    }

    // Depth dots: drag to set how much each LFO moves this knob.
    for (int l = 0; l < room::Modulation::numLfos; ++l)
    {
        const auto b = depthDotBounds (l);
        if (b.isEmpty())
            continue;
        const auto depth = mod->depthOf (l, target);
        const auto col = lfoColour (l);
        const auto active = l == hoverDot || l == dragDot;
        g.setColour (col.withAlpha (0.25f));
        g.fillEllipse (b);
        // the fill sweeps round the dot as the amount grows
        juce::Path amt;
        amt.addPieSegment (b.reduced (1.5f), 0.0f, juce::MathConstants<float>::twoPi * std::abs (depth), 0.0f);
        g.setColour (col);
        g.fillPath (amt);
        g.drawEllipse (b.reduced (0.5f), active ? 1.6f : 1.0f);
        if (depth < 0.0f)   // inverted: a small minus
            g.fillRect (juce::Rectangle<float> (6.0f, 1.6f).withCentre (b.getCentre()).translated (0.0f, 0.0f));
        if (active)
        {
            g.setColour (EvilleLookAndFeel::onBlack (*this) ? Colours::dustWhite : Colours::ink);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            const auto txt = juce::String (juce::roundToInt (depth * 100.0f)) + "%";
            auto tr = juce::Rectangle<float> (40.0f, 14.0f).withCentre (b.getCentre()).translated (0.0f, -15.0f);
            g.drawText (txt, tr, juce::Justification::centred);
        }
    }
}

void ModSlider::mouseMove (const juce::MouseEvent& e)
{
    const auto d = dotAt (e.position);
    if (d != hoverDot)
    {
        hoverDot = d;
        setMouseCursor (d >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
    juce::Slider::mouseMove (e);
}

void ModSlider::mouseExit (const juce::MouseEvent& e)
{
    if (hoverDot >= 0)
    {
        hoverDot = -1;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }
    juce::Slider::mouseExit (e);
}

void ModSlider::mouseDrag (const juce::MouseEvent& e)
{
    if (dragDot < 0)
    {
        juce::Slider::mouseDrag (e);
        return;
    }
    // Up = more. About 150 px from 0 to 100% (Shift: four times finer).
    const auto pxPerFull = e.mods.isShiftDown() ? 600.0f : 150.0f;
    const auto depth = juce::jlimit (-1.0f, 1.0f, dragStartDepth - (float) e.getDistanceFromDragStartY() / pxPerFull);
    mod->assign (dragDot, target, depth);
    repaint();
}

void ModSlider::mouseUp (const juce::MouseEvent& e)
{
    if (dragDot >= 0)
    {
        dragDot = -1;
        repaint();
        return;
    }
    juce::Slider::mouseUp (e);
}

void ModSlider::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (const auto d = dotAt (e.position); d >= 0)
    {
        mod->remove (d, target);
        dragDot = hoverDot = -1;
        updateTimer();
        repaint();
        return;
    }
    juce::Slider::mouseDoubleClick (e);
}

void ModSlider::mouseDown (const juce::MouseEvent& e)
{
    if (const auto d = dotAt (e.position); d >= 0)
    {
        if (e.mods.isPopupMenu())
        {
            showModMenu();
            return;
        }
        if (e.getNumberOfClicks() < 2)
        {
            dragDot = d;
            dragStartDepth = mod->depthOf (d, target);
            repaint();
        }
        return;
    }
    if (mod != nullptr && target >= 0 && e.mods.isPopupMenu())
    {
        showModMenu();
        return;
    }
    juce::Slider::mouseDown (e);
}

void ModSlider::showModMenu()
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("modulation");
    for (int l = 0; l < room::Modulation::numLfos; ++l)
    {
        const auto name = "lfo " + juce::String (l + 1);
        if (! mod->isAssigned (l, target))
        {
            menu.addItem ("move with " + name, [this, l]
            {
                mod->assign (l, target, 0.25f);
                updateTimer();
                repaint();
            });
            continue;
        }
        juce::PopupMenu sub;
        const auto depth = mod->depthOf (l, target);
        for (auto d : { 0.1f, 0.25f, 0.5f, 1.0f })
            sub.addItem (juce::String (juce::roundToInt (d * 100.0f)) + "%", true,
                         std::abs (std::abs (depth) - d) < 0.005f,
                         [this, l, d, depth] { mod->assign (l, target, depth < 0.0f ? -d : d); repaint(); });
        sub.addSeparator();
        sub.addItem ("invert", true, depth < 0.0f, [this, l, depth] { mod->assign (l, target, -depth); repaint(); });
        sub.addItem ("remove from " + name, [this, l] { mod->remove (l, target); updateTimer(); repaint(); });
        menu.addSubMenu (name + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 ")) + juce::String (juce::roundToInt (depth * 100.0f)) + "%", sub);
    }
    EvilleLookAndFeel::showMenu (menu, *this);
}

bool ModSlider::isInterestedInDragSource (const SourceDetails& d)
{
    return mod != nullptr && target >= 0 && d.description.toString().startsWith ("LFO");
}

void ModSlider::itemDropped (const SourceDetails& d)
{
    dropHighlight = false;
    const auto lfo = d.description.toString().getTrailingIntValue() - 1;
    if (lfo >= 0 && lfo < room::Modulation::numLfos && ! mod->isAssigned (lfo, target))
        mod->assign (lfo, target, 0.25f);
    updateTimer();
    repaint();
}
