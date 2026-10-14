#pragma once

#include "PluginProcessor.h"
#include "ModSlider.h"
#include "LfoPanel.h"

// The ADVANCED screen. It takes the place of the EQ tier:
//   top row:    PRE-DELAY  SIZE  DAMPING  DUCKING  RELEASE  [FREEZE]
//   below:      [ROOM INFO | MODULATION] tabs: everything ROOM measured about
//               the room, or the two LFOs

class AdvancedPanel : public juce::Component
{
public:
    explicit AdvancedPanel (RoomAudioProcessor&);

    /** Call when the analysis changes so the info redraws. */
    void setAnalysis (const room::RoomAnalysis&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Re-check LFO assignments on this screen's knobs. */
    void refreshModulation();

    void showModulationTab (bool);

private:
    struct Knob
    {
        ModSlider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void setupKnob (Knob&, const juce::String& id, const juce::String& name);

    RoomAudioProcessor& proc;
    Knob preDelay, size, damping, duck, duckRelease;
    juce::TextButton freeze { "FREEZE" };
    juce::Label freezeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> freezeAttachment;

    juce::TextButton infoTab { "ROOM INFO" }, modTab { "MODULATION" };
    LfoPanel lfoPanel;

    room::RoomAnalysis analysis;
    juce::Rectangle<int> infoArea;
};
