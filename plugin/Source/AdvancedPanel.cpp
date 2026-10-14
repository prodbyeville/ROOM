#include "AdvancedPanel.h"
#include "UiColours.h"

AdvancedPanel::AdvancedPanel (RoomAudioProcessor& p) : proc (p), lfoPanel (p)
{
    for (auto* t : { &modTab, &infoTab })
    {
        t->setClickingTogglesState (true);
        t->setRadioGroupId (4711);
        t->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        t->setColour (juce::TextButton::buttonOnColourId, Colours::panel);
        t->setColour (juce::TextButton::textColourOffId, Colours::dim);
        t->setColour (juce::TextButton::textColourOnId, Colours::text);
        addAndMakeVisible (t);
    }
    infoTab.onClick = [this] { showModulationTab (false); };
    modTab.onClick = [this] { showModulationTab (true); };
    addAndMakeVisible (lfoPanel);
    modTab.setToggleState (true, juce::dontSendNotification);

    clearMod.setColour (juce::TextButton::buttonColourId, Colours::panel);
    clearMod.setColour (juce::TextButton::textColourOffId, Colours::dim.brighter (0.2f));
    clearMod.setTooltip ("Remove every LFO assignment and reset both LFOs. Your knobs stay where they are.");
    clearMod.onClick = [this] { proc.clearModulation(); refreshModulation(); repaint(); };
    addAndMakeVisible (clearMod);

    setupKnob (preDelay, ParamIDs::preDelay, "PRE-DELAY");
    setupKnob (size, ParamIDs::size, "SIZE");
    setupKnob (damping, ParamIDs::damping, "DAMPING");
    setupKnob (duck, ParamIDs::duck, "DUCKING");
    setupKnob (duckRelease, ParamIDs::duckRelease, "RELEASE");
    damping.slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::accent2);
    duck.slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::reduction);
    duckRelease.slider.setColour (juce::Slider::rotarySliderFillColourId, Colours::reduction);

    freeze.setClickingTogglesState (true);
    freeze.setColour (juce::TextButton::buttonColourId, Colours::panel);
    freeze.setColour (juce::TextButton::buttonOnColourId, Colours::accent2);
    freeze.setColour (juce::TextButton::textColourOffId, Colours::text);
    freeze.setColour (juce::TextButton::textColourOnId, Colours::background);
    addAndMakeVisible (freeze);
    freezeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, ParamIDs::freeze, freeze);
    freezeLabel.setText ("HOLD", juce::dontSendNotification);
    freezeLabel.setJustificationType (juce::Justification::centred);
    freezeLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    freezeLabel.setColour (juce::Label::textColourId, Colours::dim);
    addAndMakeVisible (freezeLabel);
}

void AdvancedPanel::setupKnob (Knob& k, const juce::String& id, const juce::String& name)
{
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 110, 18);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    k.label.setColour (juce::Label::textColourId, Colours::dim);
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

void AdvancedPanel::showModulationTab (bool mod)
{
    modTab.setToggleState (mod, juce::dontSendNotification);
    infoTab.setToggleState (! mod, juce::dontSendNotification);
    lfoPanel.setVisible (mod);
    clearMod.setVisible (mod);
    repaint();
}

void AdvancedPanel::setAnalysis (const room::RoomAnalysis& a)
{
    analysis = a;
    repaint (infoArea);
}

void AdvancedPanel::resized()
{
    auto r = getLocalBounds();
    auto row = r.removeFromTop (110);
    const auto w = row.getWidth() / 6;
    for (auto* k : { &preDelay, &size, &damping, &duck, &duckRelease })
    {
        auto col = row.removeFromLeft (w).reduced (4, 0);
        k->label.setBounds (col.removeFromTop (18));
        k->slider.setBounds (col);
    }
    auto col = row.reduced (10, 0);
    freezeLabel.setBounds (col.removeFromTop (18));
    freeze.setBounds (col.withSizeKeepingCentre (std::min (col.getWidth(), 100), 40).translated (0, -8));
    auto tabs = r.removeFromTop (26);
    modTab.setBounds (tabs.removeFromLeft (120));
    infoTab.setBounds (tabs.removeFromLeft (110));
    clearMod.setBounds (tabs.removeFromRight (96).reduced (0, 1));
    r.removeFromTop (4);
    infoArea = r;
    lfoPanel.setBounds (r);
}

void AdvancedPanel::paint (juce::Graphics& g)
{
    if (lfoPanel.isVisible())
        return;

    // Room info card
    const auto card = infoArea.toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (card, 8.0f);

    auto r = infoArea.reduced (16, 12);

    if (! analysis.valid)
    {
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("No room loaded. Drop a sound or pick a ROOM.", r, juce::Justification::centred);
        return;
    }

    // Left column: summary
    auto left = r.removeFromLeft (r.getWidth() * 38 / 100);
    r.removeFromLeft (16);
    auto line = [&] (const juce::String& k, const juce::String& v)
    {
        auto row = left.removeFromTop (19);
        g.setColour (Colours::dim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (k, row.removeFromLeft (96), juce::Justification::centredLeft);
        g.setColour (Colours::text);
        g.drawText (v, row, juce::Justification::centredLeft, true);
    };
    const auto lean = analysis.balance > 0.6f ? "spatial" : analysis.balance < 0.4f ? "tonal" : "in between";
    line ("Source", analysis.sourceName.isNotEmpty() ? analysis.sourceName : juce::String ("-"));
    line ("Length", juce::String (analysis.durationS, 2) + " s  (" + (analysis.steady ? "steady sound" : "a hit") + ")");
    line ("Balance", juce::String (analysis.balance, 2) + "  " + lean);
    line ("Evidence", "space " + juce::String (analysis.spatialEvidence, 2) + "  /  tone " + juce::String (analysis.tonalEvidence, 2));
    line ("Decay", analysis.steady ? juce::String ("no decay (fallback tail)") : juce::String (analysis.medianT60, 2) + " s median");
    line ("Late energy", juce::String (juce::roundToInt (analysis.lateEnergyFraction * 100.0f)) + "% after 50 ms");

    // Right column: decay per band (as Size/Damping/Intensity now set it) and resonances
    const auto inten = proc.apvts.getRawParameterValue (ParamIDs::intensity)->load();
    const auto sz = proc.apvts.getRawParameterValue (ParamIDs::size)->load();
    const auto dmp = proc.apvts.getRawParameterValue (ParamIDs::damping)->load();

    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("DECAY PER BAND  (measured  ->  now)", r.removeFromTop (16), juce::Justification::centredLeft);
    const auto nb = analysis.numBands();
    const auto cols = 8;
    const auto rows = (nb + cols - 1) / cols;
    const auto cellW = r.getWidth() / cols;
    for (int rowI = 0; rowI < rows; ++rowI)
    {
        auto rowR = r.removeFromTop (30);
        for (int c = 0; c < cols; ++c)
        {
            const auto b = rowI * cols + c;
            if (b >= nb)
                break;
            auto cell = rowR.removeFromLeft (cellW);
            const auto fc = analysis.bandCentersHz[(size_t) b];
            const auto measured = analysis.bandT60s[(size_t) b];
            const auto now = room::bandDecay (measured, fc, inten, sz, dmp);
            g.setColour (Colours::dim);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (fc >= 1000.0f ? juce::String (fc / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (fc)),
                        cell.removeFromTop (14), juce::Justification::centredLeft);
            g.setColour (analysis.bandConfident[(size_t) b] ? Colours::text : Colours::dim);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (measured, 2) + (analysis.bandConfident[(size_t) b] ? "" : "?") + " -> "
                            + juce::String (now, 2),
                        cell, juce::Justification::centredLeft, true);
        }
    }
    r.removeFromTop (6);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText ("RESONANCES", r.removeFromTop (16), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (11.0f));
    if (analysis.peaks.empty())
    {
        g.drawText ("none detected", r.removeFromTop (16), juce::Justification::centredLeft);
        return;
    }
    // One cell per resonance, three to a row, so they never run together.
    g.setColour (Colours::text);
    const auto peakW = r.getWidth() / 3;
    auto rowR = r.removeFromTop (16);
    int col = 0;
    for (auto& pk : analysis.peaks)
    {
        if (col == 3)
        {
            rowR = r.removeFromTop (16);
            col = 0;
        }
        const auto txt = (pk.freqHz >= 1000.0f ? juce::String (pk.freqHz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (pk.freqHz)) + " Hz")
                         + "   +" + juce::String (pk.gainDb, 1) + " dB   Q " + juce::String (pk.q, 1);
        g.drawText (txt, rowR.removeFromLeft (peakW), juce::Justification::centredLeft, true);
        ++col;
    }

    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (10.0f));
    g.drawText ("? = no clear decay in that band, estimated", r.removeFromBottom (14), juce::Justification::bottomRight);
}
