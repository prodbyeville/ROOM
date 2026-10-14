#include "ModSlider.h"
#include "UiColours.h"

juce::Colour ModSlider::lfoColour (int lfo)
{
    return lfo == 0 ? juce::Colour (0xff4fd1c5) : juce::Colour (0xffc792ea);
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

void ModSlider::paint (juce::Graphics& g)
{
    juce::Slider::paint (g);
    if (mod == nullptr || target < 0)
        return;

    const auto layout = getLookAndFeel().getSliderLayout (*this);
    const auto bounds = layout.sliderBounds.toFloat().reduced (10.0f);
    const auto radius = std::min (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto lineW = std::min (8.0f, radius * 0.5f);
    const auto ringR = radius + lineW * 0.15f + 3.0f;
    const auto centre = bounds.getCentre();
    const auto rp = getRotaryParameters();
    const auto base = (float) valueToProportionOfLength (getValue());
    auto angleFor = [&] (float norm)
    {
        return rp.startAngleRadians + juce::jlimit (0.0f, 1.0f, norm) * (rp.endAngleRadians - rp.startAngleRadians);
    };

    if (dropHighlight)
    {
        g.setColour (Colours::text.withAlpha (0.25f));
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
        g.setColour (Colours::text);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (dot));
    }
}

void ModSlider::mouseDown (const juce::MouseEvent& e)
{
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
    for (int l = 0; l < room::Modulation::numLfos; ++l)
    {
        const auto name = "LFO " + juce::String (l + 1);
        if (! mod->isAssigned (l, target))
        {
            menu.addItem ("Modulate with " + name, [this, l]
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
        sub.addItem ("Invert", true, depth < 0.0f, [this, l, depth] { mod->assign (l, target, -depth); repaint(); });
        sub.addItem ("Remove", [this, l] { mod->remove (l, target); updateTimer(); repaint(); });
        menu.addSubMenu (name + "  (" + juce::String (juce::roundToInt (depth * 100.0f)) + "%)", sub);
    }
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
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
