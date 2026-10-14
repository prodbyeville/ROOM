#pragma once

#include "PluginProcessor.h"
#include "EqPanel.h"
#include "AdvancedPanel.h"
#include "presets/PresetBrowser.h"

// Working UI (layout settled, visual design still to come):
//
//   [ROOM] [< STUDIOS | name * >] [heart] [save]    [adv] [rand] (mix) (out) [bypass]
//   1  LOAD    [rec] [ROOMS load] [ROOM save] [ drop zone ......................... ]
//   2  ROOM    [ decay per band          ] [ tone (EQ + resonances)          ]
//              INTENSITY      BALANCE      TAIL      SPACE
//   3  FINISH  [ EQ display ..................................... ]  TAPE / AUTO EQ
//      (or ADVANCED: pre-delay, size, damping, ducking, freeze + room info)
//
// The whole window scales: everything is laid out at 900 x 760 and drawn
// through a scale transform, so the plugin can be resized freely (fixed aspect).

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

/** One of the two room displays: decay per band, or the tone curve. */
class RoomDisplay : public juce::Component
{
public:
    enum class Mode { decay, tone };
    explicit RoomDisplay (Mode m) : mode (m) {}

    /** Decay bars follow Intensity, Size and Damping, so you see what you hear. */
    void setAnalysis (const room::RoomAnalysis&, float intensity, float size, float damping);
    void paint (juce::Graphics&) override;

private:
    Mode mode;
    room::RoomAnalysis analysis;
    std::vector<float> eqCurve;
    float intensity = 1.0f, size = 1.0f, damping = 0.0f;
};

/** Small icon button (randomize, bypass, advanced, record, load, save). */
class IconButton : public juce::Button
{
public:
    enum class Icon { randomize, power, advanced, record, load, save };
    IconButton (Icon i, const juce::String& tooltip);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Called on right-click (instead of a click). */
    std::function<void()> onRightClick;

private:
    Icon icon;
};

class RoomAudioProcessorEditor : public juce::AudioProcessorEditor,
                                 public juce::DragAndDropContainer,
                                 private juce::Timer
{
public:
    explicit RoomAudioProcessorEditor (RoomAudioProcessor&);
    ~RoomAudioProcessorEditor() override;

    void resized() override;

    static constexpr int baseWidth = 900, baseHeight = 760;

    /** Show the MAIN (EQ) or ADVANCED screen in the bottom tier. */
    void setAdvanced (bool);

private:
    void timerCallback() override;
    void refreshAnalysis();
    void layoutContent();
    void paintContent (juce::Graphics&);

    struct Knob
    {
        ModSlider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, bool small = false);

    /** Everything lives in here, laid out at base size and scaled as a whole. */
    struct Content : public juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        std::function<void()> layout;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
        void resized() override { if (layout) layout(); }
    };

    RoomAudioProcessor& proc;
    juce::LookAndFeel_V4 lnf;
    Content content;
    juce::TooltipWindow tooltips { this, 600 };

    // Top bar
    IconButton advancedButton { IconButton::Icon::advanced, "Advanced: pre-delay, size, damping, ducking, freeze, room info" };
    IconButton randomButton { IconButton::Icon::randomize, "Randomize the knobs (keeps the room, Dry/Wet and Output). Right-click to choose what gets randomized." };
    void showRandomizeMenu();
    IconButton bypassButton { IconButton::Icon::power, "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    Knob mix, output;   // small knobs in the top bar
    juce::Random random;

    // Tier 1
    DropZone dropZone;
    juce::TextButton clearButton { "Clear" };
    IconButton recordButton { IconButton::Icon::record, "Record a room: play, clap or sing into this track, then press again to stop (max 30 s)" };
    IconButton roomLoadButton { IconButton::Icon::load, "ROOMS: load a saved room (only the room changes, your knobs stay)" };
    IconButton roomSaveButton { IconButton::Icon::save, "Save this room as a ROOM (decay, tone, resonances; no knobs)" };

    // Tier 2
    RoomDisplay decayDisplay { RoomDisplay::Mode::decay };
    RoomDisplay toneDisplay { RoomDisplay::Mode::tone };
    Knob intensity, balance, tail, space;
    juce::ToggleButton autoBalance { "Auto" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAttachment;

    // Tier 3: MAIN
    EqPanel eqPanel { proc };
    Knob tape, autoEq;
    // Tier 3: ADVANCED
    AdvancedPanel advancedPanel { proc };
    juce::Rectangle<int> tierLabels[3];

    // STUDIOS (top bar) and ROOMS (tier 1)
    void loadPreset (const juce::String& key);
    void loadRoom (const juce::String& key);
    void stepPreset (int delta);
    void openSaveDialog();
    void openRoomSaveDialog();
    juce::String submitSaveDialog (room::presets::SaveDialog::Mode, const room::presets::PresetMeta&,
                                   const juce::String& pack, bool overwrite);
    juce::String submitRoomSaveDialog (room::presets::SaveDialog::Mode, const room::presets::PresetMeta&,
                                       const juce::String& pack, bool overwrite);

    room::presets::PresetBar presetBar { "STUDIOS" };
    room::presets::PresetBrowser browser { proc, room::presets::Kind::studio };
    room::presets::PresetBrowser roomBrowser { proc, room::presets::Kind::room };
    room::presets::SaveDialog saveDialog { room::presets::Kind::studio };
    room::presets::SaveDialog roomSaveDialog { room::presets::Kind::room };
    juce::String editingKey, editingRoomKey;

    int shownVersion = -1;
    float shownIntensity = -1.0f, shownSize = -1.0f, shownDamping = -9.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomAudioProcessorEditor)
};
