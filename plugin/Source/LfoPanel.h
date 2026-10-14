#pragma once

#include "PluginProcessor.h"
#include "ModSlider.h"

// The two LFOs (Advanced screen, MODULATION tab). Each module:
//   [:: LFO 1  drag me]  shape  [SYNC] rate-or-division  [RETRIG]
//   live waveform with a moving dot, and a chip for every knob it moves.
// Drag the handle onto any knob to assign the LFO to it. Drag a chip up/down
// to change that amount (Shift = fine), double-click it to remove, right-click
// for invert / remove.

class LfoModule : public juce::Component,
                  private juce::Timer
{
public:
    LfoModule (RoomAudioProcessor&, int lfoIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    struct TargetChip { int target; juce::Rectangle<int> bounds; };
    /** One chip per knob this LFO moves (public for tests). */
    std::vector<TargetChip> targetChips() const;

private:
    void timerCallback() override;
    juce::Rectangle<int> handleArea() const;
    int chipAt (juce::Point<int>) const;

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
    int dragTarget = -1;
    float dragStartDepth = 0.0f;
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
