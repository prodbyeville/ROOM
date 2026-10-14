#pragma once

#include "PluginProcessor.h"
#include "ModSlider.h"

// The two LFOs (Advanced screen, MODULATION tab). Each module:
//   [:: LFO 1  drag me]  shape  [SYNC] rate-or-division  [RETRIG]
//   live waveform with a moving dot, and the list of knobs it moves.
// Drag the handle onto any knob to assign the LFO to it.

class LfoModule : public juce::Component,
                  private juce::Timer
{
public:
    LfoModule (RoomAudioProcessor&, int lfoIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<int> handleArea() const;
    juce::String targetsText() const;

    RoomAudioProcessor& proc;
    const int lfo;
    juce::ComboBox shape, division;
    juce::ToggleButton sync { "Sync" }, retrig { "Retrig" };
    juce::Slider rate { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> shapeAtt, divAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt, retrigAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateAtt;
    juce::Rectangle<int> waveArea, targetsArea;
    std::vector<float> trail;   // recent LFO values for the scope
};

class LfoPanel : public juce::Component
{
public:
    explicit LfoPanel (RoomAudioProcessor& p) : lfo1 (p, 0), lfo2 (p, 1)
    {
        addAndMakeVisible (lfo1);
        addAndMakeVisible (lfo2);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        lfo1.setBounds (r.removeFromLeft ((r.getWidth() - 12) / 2));
        r.removeFromLeft (12);
        lfo2.setBounds (r);
    }

private:
    LfoModule lfo1, lfo2;
};
