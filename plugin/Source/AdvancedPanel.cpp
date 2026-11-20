#include "AdvancedPanel.h"
#include "UiColours.h"
#include "Screens.h"
#include "EvilleLookAndFeel.h"
#include "R5Parts.h"

AdvancedPanel::AdvancedPanel (RoomAudioProcessor& p) : proc (p), lfoPanel (p)
{
    getProperties().set ("surface", "light");   // the light module: ink text, light knobs
    setupKnob (preDelay, ParamIDs::preDelay, "pre-delay", "0", "", "500");
    setupKnob (space, ParamIDs::space, "width", "mono", "", "wide");
    setupKnob (damping, ParamIDs::damping, "damping", "-", "0", "+");
    setupKnob (duck, ParamIDs::duck, "ducking", "0", "", "100");
    setupKnob (duckRelease, ParamIDs::duckRelease, "release", "50", "", "1k");
    preDelay.slider.setTooltip ("a gap before the room");
    space.slider.setTooltip ("mono to wide");
    setupKnob (tape, ParamIDs::tape, "tape", "0", "", "100");
    setupKnob (autoEq, ParamIDs::autoEq, "auto eq", "0", "", "100");
    tape.slider.setTooltip ("warm tape colour");
    autoEq.slider.setTooltip ("evens out the tone");
    damping.slider.setTooltip ("darker or brighter tail");
    duck.slider.setTooltip ("room ducks while you play");
    duckRelease.slider.setTooltip ("how fast it comes back");

    freeze.setClickingTogglesState (true);
    freeze.getProperties().set ("r5key", "black");
    freeze.setButtonText ("freeze");
    freeze.setTooltip ("hold the tail forever");
    addAndMakeVisible (freeze);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::freeze, freeze);
    freeze.onStateChange = [this] { repaint(); };

    for (auto* c : { &card1, &card2 })
    {
        addAndMakeVisible (c);
        c->onOpen = [this] (int lfo) { lfoPanel.showLfo (lfo); showModulationTab (true); };
    }
    addChildComponent (lfoPanel);
    lfoPanel.onDone = [this] { showModulationTab (false); };
    for (auto* k : allKnobs())
        names.add (k->label.getText());
    startTimerHz (15);
}

void AdvancedPanel::timerCallback()
{
    // No value bubble: a knob being touched shows its value in place of its name.
    const auto ks = allKnobs();
    for (size_t i = 0; i < ks.size(); ++i)
    {
        auto& k = *ks[i];
        const auto text = k.slider.isMouseOverOrDragging() ? k.slider.getTextFromValue (k.slider.getValue()) : names[(int) i];
        if (k.label.getText() != text)
            k.label.setText (text, juce::dontSendNotification);
    }
}

std::vector<AdvancedPanel::Knob*> AdvancedPanel::allKnobs()
{
    return { &preDelay, &space, &damping, &duck, &duckRelease, &tape, &autoEq };
}

void AdvancedPanel::setupKnob (Knob& k, const juce::String& id, const juce::String& name,
                               const juce::String& lo, const juce::String& mid, const juce::String& hi, float radius)
{
    k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    // no value bubble (values are printed or shown on hover tooltips)
    auto& props = k.slider.getProperties();
    props.set ("surface", "light");
    props.set ("knobRadius", radius);
    props.set ("scaleMin", lo);
    props.set ("scaleMid", mid);
    props.set ("scaleMax", hi);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (Fonts::sans (13.0f));
    k.label.setColour (juce::Label::textColourId, Colours::ink);
    k.label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
    k.slider.setModTarget (&proc.modulation, room::Modulation::targetIndex (id));
}

void AdvancedPanel::refreshModulation()
{
    for (auto* k : allKnobs())
        k->slider.refreshModulation();
}

void AdvancedPanel::showModulationTab (bool lfos)
{
    lfoPanel.setVisible (lfos);
    for (auto* k : allKnobs())
    {
        k->slider.setVisible (! lfos);
        k->label.setVisible (! lfos);
    }
    freeze.setVisible (! lfos);
    card1.setVisible (! lfos);
    card2.setVisible (! lfos);
    repaint();
}

void AdvancedPanel::paint (juce::Graphics& g)
{
    // ink on the light module
    R5::drawLabel (g, lfoPanel.isVisible() ? "lfo" : "advanced", { (float) getWidth() * 0.5f, 44.0f }, 12.0f, 1.0f, true);
    g.setColour (Colours::ink.withAlpha (0.14f));
    g.fillRect (18.0f, 70.0f, (float) getWidth() - 36.0f, 1.0f);
    if (! lfoPanel.isVisible())
    {
        // Freeze: LED above the key, label below (on the same line as "release")
        g.setColour (Colours::ink.withAlpha (0.14f));
        g.fillRect (18.0f, (float) lfoTop - 14.0f, (float) getWidth() - 36.0f, 1.0f);
    }
}

void AdvancedPanel::resized()
{
    // Three columns: the room (pre-delay, damping, width), the dynamics (ducking,
    // release, freeze), the finish (tape, auto eq); then the lfos.
    const auto w = getWidth();
    const int cols[3] = { w / 6, w / 2, w * 5 / 6 };
    const int rows[3] = { 112, 196, 280 };
    auto place = [&] (Knob& k, int col, int row)
    {
        // 88 wide: room for the lfo depth dots right of the ring (the knob's hit area is
        // only the ring and its dots, so neighbours can overlap).
        k.slider.setBounds (juce::Rectangle<int> (88, 52).withCentre ({ cols[col], rows[row] }));
        k.label.setBounds (juce::Rectangle<int> (90, 16).withCentre ({ cols[col], rows[row] + 36 }));
    };
    place (preDelay, 0, 0);
    place (damping, 1, 0);
    place (space, 2, 0);
    place (duck, 0, 1);
    place (duckRelease, 1, 1);
    freeze.setBounds (juce::Rectangle<int> (62, 30).withCentre ({ cols[2], rows[1] + 2 }));
    place (tape, 0, 2);
    place (autoEq, 1, 2);

    lfoTop = rows[2] + 76;

    // The two LFO cards fill the rest, down to the bottom of the unit.
    const int bottom = getHeight() - 30, gap = 14;
    const int h = (bottom - lfoTop - gap) / 2;
    card1.setBounds (18, lfoTop, w - 36, h);
    card2.setBounds (18, lfoTop + h + gap, w - 36, h);
    lfoPanel.setBounds (18, 72, w - 36, getHeight() - 106);
}
