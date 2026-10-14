#include "AdvancedPanel.h"
#include "UiColours.h"
#include "Screens.h"
#include "EvilleLookAndFeel.h"

AdvancedPanel::AdvancedPanel (RoomAudioProcessor& p) : proc (p), lfoPanel (p)
{
    setupKnob (preDelay, ParamIDs::preDelay, "pre-delay", "0", "", "500");
    setupKnob (size, ParamIDs::size, "size", "50", "", "200");
    setupKnob (damping, ParamIDs::damping, "damping", "-", "0", "+");
    setupKnob (duck, ParamIDs::duck, "ducking", "0", "", "100");
    setupKnob (duckRelease, ParamIDs::duckRelease, "release", "50", "", "1k");

    freeze.setClickingTogglesState (true);
    freeze.getProperties().set ("key", "dark");
    freeze.setTooltip ("freeze: hold the room's tail forever");
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
}

void AdvancedPanel::setupKnob (Knob& k, const juce::String& id, const juce::String& name,
                               const juce::String& lo, const juce::String& mid, const juce::String& hi)
{
    k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.slider.setPopupDisplayEnabled (true, true, this);
    auto& props = k.slider.getProperties();
    props.set ("surface", "black");
    props.set ("knobRadius", 19.0f);
    props.set ("scaleMin", lo);
    props.set ("scaleMid", mid);
    props.set ("scaleMax", hi);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (Fonts::sans (13.0f));
    k.label.setColour (juce::Label::textColourId, Colours::dustWhite);
    k.label.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id, k.slider);
    k.slider.setModTarget (&proc.modulation, room::Modulation::targetIndex (id));
}

void AdvancedPanel::refreshModulation()
{
    for (auto* k : { &preDelay, &size, &damping, &duck, &duckRelease })
        k->slider.refreshModulation();
}

void AdvancedPanel::showModulationTab (bool lfos)
{
    lfoPanel.setVisible (lfos);
    for (auto* k : { &preDelay, &size, &damping, &duck, &duckRelease })
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
    g.setColour (Colours::dustWhite);
    g.setFont (Fonts::sans (22.0f, Fonts::bold));
    g.drawText (lfoPanel.isVisible() ? "lfo" : "advanced", 24, 26, 200, 30, juce::Justification::centredLeft);

    if (! lfoPanel.isVisible())
    {
        // Freeze: LED above the key, label below
        Hw::drawLed (g, { (float) freeze.getBounds().getCentreX(), (float) freeze.getY() - 11.0f }, freeze.getToggleState());
        g.setFont (Fonts::sans (13.0f));
        g.setColour (Colours::dustWhite);
        g.drawText ("freeze", freeze.getBounds().translated (0, freeze.getHeight() + 2).withSizeKeepingCentre (80, 18),
                    juce::Justification::centred);
    }
}

void AdvancedPanel::resized()
{
    const float cols[3] = { 48.0f, 130.0f, 212.0f };
    auto place = [&] (Knob& k, int col, float cy)
    {
        k.slider.setBounds (juce::Rectangle<int> (82, 70).withCentre ({ (int) cols[col], (int) cy }));
        k.label.setBounds (juce::Rectangle<int> (90, 18).withCentre ({ (int) cols[col], (int) cy + 40 }));
    };
    place (preDelay, 0, 112.0f);
    place (size, 1, 112.0f);
    place (damping, 2, 112.0f);
    place (duck, 0, 210.0f);
    place (duckRelease, 1, 210.0f);
    freeze.setBounds (juce::Rectangle<int> (34, 30).withCentre ({ (int) cols[2], 210 }));

    card1.setBounds (16, 278, getWidth() - 32, 128);
    card2.setBounds (16, 414, getWidth() - 32, 128);
    lfoPanel.setBounds (16, 70, getWidth() - 32, getHeight() - 86);
}
