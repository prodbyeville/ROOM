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
    props.set ("knobRadius", 24.0f);
    props.set ("scaleMin", lo);
    props.set ("scaleMid", mid);
    props.set ("scaleMax", hi);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (Fonts::sans (14.0f));
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
    g.drawText (lfoPanel.isVisible() ? "lfo" : "advanced", 26, 30, 200, 30, juce::Justification::centredLeft);

    Hw::drawGroove (g, { 18.0f, 70.0f }, { (float) getWidth() - 18.0f, 70.0f }, true);
    if (! lfoPanel.isVisible())
    {
        Hw::drawGroove (g, { 18.0f, 420.0f }, { (float) getWidth() - 18.0f, 420.0f }, true);
        // Freeze: LED above the key, label below
        Hw::drawLed (g, { (float) freeze.getBounds().getCentreX(), (float) freeze.getY() - 12.0f }, freeze.getToggleState(), 4.0f);
        g.setFont (Fonts::sans (14.0f));
        g.setColour (Colours::dustWhite);
        g.drawText ("freeze", freeze.getBounds().translated (0, freeze.getHeight() + 1).withSizeKeepingCentre (80, 18),
                    juce::Justification::centred);
    }
}

void AdvancedPanel::resized()
{
    // Two columns, three rows; each knob gets room for its printed scale.
    const int cols[2] = { 70, getWidth() - 70 };
    const int rows[3] = { 128, 244, 360 };
    auto place = [&] (Knob& k, int col, int row)
    {
        k.slider.setBounds (juce::Rectangle<int> (124, 112).withCentre ({ cols[col], rows[row] }));
        k.label.setBounds (juce::Rectangle<int> (110, 18).withCentre ({ cols[col], rows[row] + 50 }));
    };
    place (preDelay, 0, 0);
    place (size, 1, 0);
    place (damping, 0, 1);
    place (duck, 1, 1);
    place (duckRelease, 0, 2);
    freeze.setBounds (juce::Rectangle<int> (50, 50).withCentre ({ cols[1], rows[2] + 4 }));

    // The two LFO cards fill the rest, down to the bottom of the unit.
    const int top = 436, bottom = getHeight() - 34, gap = 14;
    const int h = (bottom - top - gap) / 2;
    card1.setBounds (18, top, getWidth() - 36, h);
    card2.setBounds (18, top + h + gap, getWidth() - 36, h);
    lfoPanel.setBounds (18, 72, getWidth() - 36, getHeight() - 106);
}
