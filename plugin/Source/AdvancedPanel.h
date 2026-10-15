#pragma once

#include "PluginProcessor.h"
#include "ModSlider.h"
#include "LfoPanel.h"

// The black "advanced" section bolted to the right of the console:
//   advanced
//   (pre-delay) (size)
//   (damping)   (ducking)
//   (release)   [freeze]
//   [ lfo 1 card ]
//   [ lfo 2 card ]   (down to the bottom of the unit)
// Clicking an LFO card swaps the knobs and cards for that LFO's full controls
// (shape, rate / sync, retrig, drag handle, what it moves), in the same place,
// so the console stays in view while you shape it.

class AdvancedPanel : public juce::Component
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
                    const juce::String& lo, const juce::String& mid, const juce::String& hi);

    RoomAudioProcessor& proc;
    Knob preDelay, size, damping, duck, duckRelease;
    juce::TextButton freeze { {} };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

    LfoCard card1 { proc, 0 }, card2 { proc, 1 };
    LfoPanel lfoPanel;
};
