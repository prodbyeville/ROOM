#pragma once

#include "PluginProcessor.h"
#include "ModSlider.h"

// The two LFOs (advanced section), stacked. Each module, top to bottom:
//   [:: lfo 1]  shape
//   "drag the handle onto a knob"
//   live waveform with a moving dot
//   rate (a big slider) or the tempo division when synced
//   [sync]  [retrig]
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

    void paintOverChildren (juce::Graphics&) override;
    /** Light the module up briefly (you just opened it). */
    void flash();

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
    juce::Rectangle<int> waveArea, targetsArea, captionArea;
    std::vector<float> trail;   // recent LFO values for the scope
    float flashAlpha = 0.0f;
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

/** The full LFO controls, shown in place of the advanced knobs: lfo 1 and
    lfo 2 stacked, and "done" to go back. */
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
    juce::TextButton done { "done" };
    int shown = 0;
};
