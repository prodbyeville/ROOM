#pragma once

#include "PluginProcessor.h"
#include "ModSlider.h"

// The two LFOs (advanced section). Each module, top to bottom:
//   [:: lfo 1  drag to a knob]  shape
//   live waveform with a moving dot
//   [sync] rate-or-division  [retrig]
//   a chip for every knob it moves
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
    juce::ToggleButton sync { "sync" }, retrig { "retrig" };
    juce::Slider rate { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> shapeAtt, divAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt, retrigAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateAtt;
    juce::Rectangle<int> waveArea, targetsArea;
    std::vector<float> trail;   // recent LFO values for the scope
    int dragTarget = -1;
    float dragStartDepth = 0.0f;
};

/** Compact view of one LFO for the advanced section: its waveform, what it
    moves, and sync / retrig. Click it to open the full controls; drag it onto a
    knob to assign it there. */
class LfoCard : public juce::Component,
                public juce::SettableTooltipClient,
                private juce::Timer
{
public:
    LfoCard (RoomAudioProcessor&, int lfoIndex);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    std::function<void (int lfo)> onOpen;

private:
    void timerCallback() override;
    juce::Rectangle<int> screenArea() const;

    RoomAudioProcessor& proc;
    const int lfo;
    juce::ToggleButton sync { "sync" }, retrig { "retrig" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAtt, retrigAtt;
    std::vector<float> trail;
    bool dragged = false;
};

/** The full LFO controls, shown in place of the advanced knobs: tabs for
    LFO 1 / 2, the module, and "done" to go back. */
class LfoPanel : public juce::Component
{
public:
    explicit LfoPanel (RoomAudioProcessor& p);

    void showLfo (int lfo);
    int getShownLfo() const { return shown; }
    void resized() override;
    void paint (juce::Graphics&) override;

    std::function<void()> onDone;

private:
    LfoModule lfo1, lfo2;
    juce::TextButton tab1 { "lfo 1" }, tab2 { "lfo 2" }, done { "done" };
    int shown = 0;
};
