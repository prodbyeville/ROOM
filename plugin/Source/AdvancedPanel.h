#pragma once

#include "PluginProcessor.h"

// The ADVANCED screen. It takes the place of the EQ tier:
//   top row:    PRE-DELAY  SIZE  DAMPING  DUCKING  RELEASE  [FREEZE]
//   below:      ROOM INFO  (everything ROOM measured about the loaded room)

class AdvancedPanel : public juce::Component
{
public:
    explicit AdvancedPanel (RoomAudioProcessor&);

    /** Call when the analysis changes so the info redraws. */
    void setAnalysis (const room::RoomAnalysis&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void setupKnob (Knob&, const juce::String& id, const juce::String& name);

    RoomAudioProcessor& proc;
    Knob preDelay, size, damping, duck, duckRelease;
    juce::TextButton freeze { "FREEZE" };
    juce::Label freezeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

    room::RoomAnalysis analysis;
    juce::Rectangle<int> infoArea;
};
