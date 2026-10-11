#pragma once

#include "PluginProcessor.h"
#include "EqPanel.h"

// Simple working UI (not the final design):
//   - drop zone to load a room sound (or click to browse)
//   - display of what was detected: decay per band, tone EQ, resonances
//   - knobs: Intensity, Mix, Balance (+ Auto), Tail, Output

class RoomAudioProcessorEditor;

/** Drop a file here, or click to browse. */
class DropZone : public juce::Component,
                 public juce::FileDragAndDropTarget
{
public:
    explicit DropZone (RoomAudioProcessor&);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { hovering = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hovering = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    RoomAudioProcessor& proc;
    std::unique_ptr<juce::FileChooser> chooser;
    bool hovering = false;
};

/** Shows the measured room: decay bars and the tone/resonance curve. */
class RoomDisplay : public juce::Component
{
public:
    void setAnalysis (const room::RoomAnalysis& a, float intensity);
    void paint (juce::Graphics&) override;

private:
    room::RoomAnalysis analysis;
    std::vector<float> eqCurve;
    float intensity = 1.0f;
};

class RoomAudioProcessorEditor : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit RoomAudioProcessorEditor (RoomAudioProcessor&);
    ~RoomAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshAnalysis();

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name);

    RoomAudioProcessor& proc;
    juce::LookAndFeel_V4 lnf;

    DropZone dropZone;
    RoomDisplay display;
    juce::Label readout;
    juce::TextButton clearButton { "Clear" };

    Knob intensity, mix, balance, tail, output;
    Knob tape, autoEq;
    EqPanel eqPanel { proc };
    juce::Rectangle<int> eqLabelArea;
    juce::ToggleButton autoBalance { "Auto" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAttachment;

    int shownVersion = -1;
    float shownIntensity = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomAudioProcessorEditor)
};
