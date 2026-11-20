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
    setupKnob (mud, ParamIDs::mud, "mud", "0", "", "100");
    mud.slider.setTooltip ("tames the boxy low mids (280 to 450 hz)");
    for (auto* k : { &tape, &autoEq, &mud })
    {
        // the finish box is dark: white labels, dark knobs
        k->slider.getProperties().set ("surface", "black");
        k->label.setColour (juce::Label::textColourId, Colours::dustWhite);
    }
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
    return { &preDelay, &space, &damping, &duck, &duckRelease, &tape, &autoEq, &mud };
}

void AdvancedPanel::setupKnob (Knob& k, const juce::String& id, const juce::String& name,
                               const juce::String& lo, const juce::String& mid, const juce::String& hi, float radius)
{
    k.slider.getProperties().set ("dotGap", 10.0f);   // the depth dots sit close: three knobs a row
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
        // The finish box: tape, auto eq and mud in a dark recessed box on the paper
        const auto b = finishBox;
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.fillRoundedRectangle (b.translated (0.0f, 1.0f), 9.0f);   // the paper's lit lip below
        juce::ColourGradient body (juce::Colour (0xff141312), b.getX(), b.getY(), juce::Colour (0xff0b0b0a), b.getX(), b.getBottom(), false);
        g.setGradientFill (body);
        g.fillRoundedRectangle (b, 9.0f);
        {
            juce::Graphics::ScopedSaveState save (g);
            juce::Path shape;
            shape.addRoundedRectangle (b, 9.0f);
            g.reduceClipRegion (shape);
            juce::ColourGradient inner (juce::Colours::black.withAlpha (0.6f), b.getX(), b.getY(),
                                        juce::Colours::transparentBlack, b.getX(), b.getY() + 10.0f, false);
            g.setGradientFill (inner);
            g.fillRect (b.withHeight (10.0f));
        }
        R5::drawLabel (g, "finish", { b.getCentreX(), b.getY() + 13.0f }, 9.0f, 0.75f);
    }
}

void AdvancedPanel::resized()
{
    // Three columns: the room (pre-delay, damping, width), the dynamics (ducking,
    // release, freeze), the finish (tape, auto eq); then the lfos.
    const auto w = getWidth();
    const int cols[3] = { w * 18 / 100, w * 48 / 100, w * 78 / 100 };   // a little left of centre: room for the depth dots
    const int rows[3] = { 108, 190, 300 };
    auto place = [&] (Knob& k, int col, int row)
    {
        // 104 x 64: room for the lfo depth dots right of the ring and its lfo arcs (the knob's hit area is
        // only the ring and its dots, so neighbours can overlap).
        k.slider.setBounds (juce::Rectangle<int> (104, 64).withCentre ({ cols[col], rows[row] }));
        k.label.setBounds (juce::Rectangle<int> (90, 16).withCentre ({ cols[col], rows[row] + 36 }));
    };
    place (preDelay, 0, 0);
    place (damping, 1, 0);
    place (space, 2, 0);
    place (duck, 0, 1);
    place (duckRelease, 1, 1);
    freeze.setBounds (juce::Rectangle<int> (62, 30 + (int) R5::glowRoom).withCentre ({ cols[2], rows[1] + 2 + (int) R5::glowRoom / 2 }));
    finishBox = juce::Rectangle<float> (12.0f, (float) rows[2] - 50.0f, (float) w - 24.0f, 104.0f);
    place (tape, 0, 2);
    place (autoEq, 1, 2);
    place (mud, 2, 2);

    lfoTop = (int) finishBox.getBottom() + 16;

    // The two LFO cards fill the rest, down to the bottom of the unit.
    const int bottom = getHeight() - 30, gap = 14;
    const int h = (bottom - lfoTop - gap) / 2;
    card1.setBounds (18, lfoTop, w - 36, h);
    card2.setBounds (18, lfoTop + h + gap, w - 36, h);
    lfoPanel.setBounds (18, 72, w - 36, getHeight() - 106);
}
