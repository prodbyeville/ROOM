#include "AdvancedPanel.h"
#include "UiColours.h"
#include "Screens.h"
#include "EvilleLookAndFeel.h"

AdvancedPanel::AdvancedPanel (RoomAudioProcessor& p) : proc (p), lfoPanel (p)
{
    setupKnob (preDelay, ParamIDs::preDelay, "pre-delay", "0", "", "500");
    setupKnob (space, ParamIDs::space, "width", "mono", "", "wide");
    setupKnob (damping, ParamIDs::damping, "damping", "-", "0", "+");
    setupKnob (duck, ParamIDs::duck, "ducking", "0", "", "100");
    setupKnob (duckRelease, ParamIDs::duckRelease, "release", "50", "", "1k");
    preDelay.slider.setTooltip ("a gap before the room starts, so your dry sound stays clear.");
    space.slider.setTooltip ("how wide the room sounds, from mono to wide.");
    setupKnob (tape, ParamIDs::tape, "tape", "0", "", "100");
    setupKnob (autoEq, ParamIDs::autoEq, "auto eq", "0", "", "100");
    tape.slider.setTooltip ("warm tape saturation on the way out.");
    autoEq.slider.setTooltip ("gently evens out the tone so the room sits in the mix.");
    const char* bandNames[] = { "low", "lo mid", "mid", "hi mid", "high" };
    for (int b = 0; b < room::tone::numEqBands; ++b)
    {
        setupKnob (eqGain[b], ParamIDs::eqGain (b), bandNames[b], "", "", "", 13.0f);
        eqGain[b].slider.getProperties().set ("bipolar", true);
        eqGain[b].slider.setDoubleClickReturnValue (true, 0.0);
        eqGain[b].label.setFont (Fonts::sans (11.0f));
        eqGain[b].slider.setTooltip (juce::String ("eq: ") + bandNames[b] + " band, -12 to +12 db (double-click: 0)");
    }
    damping.slider.setTooltip ("darker or brighter: how fast the highs die away in the room.");
    duck.slider.setTooltip ("turns the room down while you play and lets it bloom in the gaps.");
    duckRelease.slider.setTooltip ("how fast the room comes back after ducking.");

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

std::vector<AdvancedPanel::Knob*> AdvancedPanel::allKnobs()
{
    std::vector<Knob*> v { &preDelay, &space, &damping, &duck, &duckRelease, &tape, &autoEq };
    for (auto& k : eqGain)
        v.push_back (&k);
    return v;
}

void AdvancedPanel::setupKnob (Knob& k, const juce::String& id, const juce::String& name,
                               const juce::String& lo, const juce::String& mid, const juce::String& hi, float radius)
{
    k.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    k.slider.setPopupDisplayEnabled (true, true, this);
    auto& props = k.slider.getProperties();
    props.set ("surface", "black");
    props.set ("knobRadius", radius);
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
    g.setColour (Colours::dustWhite);
    g.setFont (Fonts::sans (22.0f, Fonts::bold));
    g.drawText (lfoPanel.isVisible() ? "lfo" : "advanced", 26, 30, 200, 30, juce::Justification::centredLeft);

    Hw::drawGroove (g, { 18.0f, 70.0f }, { (float) getWidth() - 18.0f, 70.0f }, true);
    if (! lfoPanel.isVisible())
    {
        // Freeze: LED above the key, label below (on the same line as "release")
        Hw::drawLed (g, { (float) freeze.getBounds().getCentreX(), (float) freeze.getY() - 10.0f }, freeze.getToggleState(), 3.5f);
        g.setFont (Fonts::sans (13.0f));
        g.setColour (Colours::dustWhite);
        g.drawText ("freeze", juce::Rectangle<int> (80, 18).withCentre ({ freeze.getBounds().getCentreX(), duckRelease.label.getBounds().getCentreY() }),
                    juce::Justification::centred);
        // The eq row
        Hw::drawGroove (g, { 18.0f, (float) eqTop }, { (float) getWidth() - 18.0f, (float) eqTop }, true);
        EvilleLookAndFeel::drawCaps (g, "eq", { 22.0f, (float) eqTop + 10.0f, 60.0f, 14.0f }, 10.5f, Colours::dustWhite.withAlpha (0.85f));
        Hw::drawGroove (g, { 18.0f, (float) lfoTop - 14.0f }, { (float) getWidth() - 18.0f, (float) lfoTop - 14.0f }, true);
    }
}

void AdvancedPanel::resized()
{
    // Three columns: the room (pre-delay, damping, width), the dynamics (ducking,
    // release, freeze), the finish (tape, auto eq); then the eq bands, then the lfos.
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
    freeze.setBounds (juce::Rectangle<int> (40, 40).withCentre ({ cols[2], rows[1] + 2 }));
    place (tape, 0, 2);
    place (autoEq, 1, 2);

    // The eq: five small bipolar knobs in a row
    eqTop = 336;
    const auto eqY = eqTop + 50;
    const auto step = (w - 36) / room::tone::numEqBands;
    for (int b = 0; b < room::tone::numEqBands; ++b)
    {
        const auto x = 18 + step * b + step / 2;
        eqGain[b].slider.setBounds (juce::Rectangle<int> (step + 8, 36).withCentre ({ x, eqY }));
        eqGain[b].label.setBounds (juce::Rectangle<int> (step + 4, 14).withCentre ({ x, eqY + 26 }));
    }

    // The two LFO cards fill the rest, down to the bottom of the unit.
    lfoTop = eqY + 54;
    const int bottom = getHeight() - 30, gap = 14;
    const int h = (bottom - lfoTop - gap) / 2;
    card1.setBounds (18, lfoTop, w - 36, h);
    card2.setBounds (18, lfoTop + h + gap, w - 36, h);
    lfoPanel.setBounds (18, 72, w - 36, getHeight() - 106);
}
