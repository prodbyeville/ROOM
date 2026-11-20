#pragma once

#include "PluginProcessor.h"
#include "ModSlider.h"
#include "LfoPanel.h"

// The black "advanced" section bolted to the right of the console:
//   advanced
//   (pre-delay) (damping) (width)
//   (ducking)   (release) [freeze]
//   (tape)      (auto eq)
//   [ lfo 1 card ]
//   [ lfo 2 card ]   (down to the bottom of the unit)
// Clicking an LFO card swaps the knobs and cards for that LFO's full controls
// (shape, rate / sync, retrig, drag handle, what it moves), in the same place,
// so the console stays in view while you shape it.

class AdvancedPanel : public juce::Component,
                      private juce::Timer
{
public:
    explicit AdvancedPanel (RoomAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Re-check LFO assignments on this section's knobs. */
    void refreshModulation();

    /** Show the full LFO controls (true) or the knobs and cards (false). */
    void showModulationTab (bool);
    bool isShowingLfos() const { return lfoPanel.isVisible(); }

private:
    struct Knob
    {
        ModSlider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void setupKnob (Knob&, const juce::String& id, const juce::String& name,
                    const juce::String& lo, const juce::String& mid, const juce::String& hi, float radius = 20.0f);

    RoomAudioProcessor& proc;
    Knob preDelay, space, damping, duck, duckRelease, tape, autoEq;   // "space" shows as width
    std::vector<Knob*> allKnobs();
    juce::TextButton freeze { {} };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

    LfoCard card1 { proc, 0 }, card2 { proc, 1 };
    int lfoTop = 340;
    LfoPanel lfoPanel;
    juce::StringArray names;   // each knob's label, shown again when it isn't being touched
    void timerCallback() override;
};
