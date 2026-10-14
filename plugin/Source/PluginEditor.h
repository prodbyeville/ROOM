#pragma once

#include "PluginProcessor.h"
#include "EqPanel.h"
#include "AdvancedPanel.h"
#include "EvilleLookAndFeel.h"
#include "presets/PresetBrowser.h"

// The ROOM console (eville). Three pieces of hardware:
//
//   +-------------------------------------------------+-------------+
//   | room by eville  [< studios / name >] [♥][save]   | advanced    |
//   |                 [shuffle]   (wet) (out)  bypass  |  knobs      |
//   | ( load )        ( room )        ( tone )         |  freeze     |
//   |  (intensity)  (balance)  (tail)  (space)         |  lfo 1 / 2  |
//   +-------------------------------------------------+-------------+
//          | cable
//   +----------------------------------------------+
//   | finish   [ eq screen ]   (tape) (auto eq)  out |
//   +----------------------------------------------+
//
// The dust-white console carries the printed signal path (dotted lines and
// tiny rooms). Everything is laid out at 1200 x 880 and drawn through a scale
// transform, so the window resizes freely (fixed aspect).

class RoomAudioProcessorEditor;

/** The load screen: the loaded room's name and a glimpse of it. Drop a sound
    here, click to open ROOMS, right-click for more. */
class LoadScreen : public juce::Component,
                   public juce::SettableTooltipClient,
                   public juce::FileDragAndDropTarget,
                   private juce::Timer
{
public:
    explicit LoadScreen (RoomAudioProcessor&);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { hovering = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hovering = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    std::function<void()> onOpenLibrary;
    void chooseSoundFile();

private:
    void timerCallback() override;

    RoomAudioProcessor& proc;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Image photo;
    bool hovering = false;
};

/** The room itself, drawn live: a one-point-perspective room whose floor holds
    the decay per band. It moves with the knobs (and the LFOs): intensity = glow,
    tail = how far back the decay reaches, space = width, size = depth,
    damping = colour and fade, pre-delay = gap, ducking and input = pulse,
    freeze = holds. Click it for the full room info. */
class RoomScreen : public juce::Component,
                   public juce::SettableTooltipClient,
                   private juce::Timer
{
public:
    explicit RoomScreen (RoomAudioProcessor&);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { setShowInfo (! showInfo); }
    void setShowInfo (bool);
    bool isShowingInfo() const { return showInfo; }

private:
    void timerCallback() override;
    void paintInfo (juce::Graphics&, juce::Rectangle<float>);

    RoomAudioProcessor& proc;
    room::RoomAnalysis analysis;
    int shownVersion = -1;
    bool showInfo = false;
    float phase = 0.0f, pulsePos = 2.0f, lastLevel = 0.0f, levelSmooth = 0.0f;
};

/** The tone the room puts on a sound: its EQ curve, with the strongest
    resonance marked in crimson. */
class ToneScreen : public juce::Component,
                   private juce::Timer
{
public:
    explicit ToneScreen (RoomAudioProcessor&);
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    RoomAudioProcessor& proc;
    room::RoomAnalysis analysis;
    int shownVersion = -1;
    float lastIntensity = -1.0f, lastBalance = -1.0f;
};

/** Square hardware key with an icon (record, save, shuffle). */
class IconButton : public juce::Button
{
public:
    enum class Icon { randomize, record, save };
    IconButton (Icon i, const juce::String& tooltip);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Called on right-click (instead of a click). */
    std::function<void()> onRightClick;

private:
    Icon icon;
};

/** Bypass: a small metal toggle and the power LED (lit = ROOM is on). */
class BypassSwitch : public juce::Button
{
public:
    BypassSwitch() : juce::Button ("bypass") { setClickingTogglesState (true); setTooltip ("bypass"); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

class RoomAudioProcessorEditor : public juce::AudioProcessorEditor,
                                 public juce::DragAndDropContainer,
                                 private juce::Timer
{
public:
    explicit RoomAudioProcessorEditor (RoomAudioProcessor&);
    ~RoomAudioProcessorEditor() override;

    void resized() override;

    static constexpr int baseWidth = 1200, baseHeight = 880;

    /** Open (or close) the LFO controls in the advanced section. */
    void setAdvanced (bool showLfos);
    /** Show the full room info on the room screen. */
    void showRoomInfo (bool show) { roomScreen.setShowInfo (show); }

private:
    void timerCallback() override;
    void layoutContent();
    void paintContent (juce::Graphics&);
    void paintConsoleArt (juce::Graphics&);

    struct Knob
    {
        ModSlider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, float radius,
                    const juce::String& scaleMin, const juce::String& scaleMid, const juce::String& scaleMax,
                    bool onBlack);

    /** Everything lives in here, laid out at base size and scaled as a whole. */
    struct Content : public juce::Component
    {
        std::function<void (juce::Graphics&)> painter;
        std::function<void()> layout;
        void paint (juce::Graphics& g) override { if (painter) painter (g); }
        void resized() override { if (layout) layout(); }
    };

    RoomAudioProcessor& proc;
    EvilleLookAndFeel lnf;
    Content content;
    juce::TooltipWindow tooltips { this, 600 };

    // Fixed geometry of the three pieces of hardware (base coordinates)
    const juce::Rectangle<float> consoleArea { 20.0f, 20.0f, 900.0f, 560.0f };
    const juce::Rectangle<float> advancedArea { 920.0f, 20.0f, 260.0f, 560.0f };
    const juce::Rectangle<float> finishArea { 60.0f, 614.0f, 900.0f, 246.0f };
    juce::Rectangle<int> bays[3];
    juce::Image consoleGrain, blackGrain;

    // Top bar
    room::presets::PresetBar presetBar { "studios" };
    IconButton randomButton { IconButton::Icon::randomize, "randomize the knobs (right-click to choose what moves)" };
    void showRandomizeMenu();
    BypassSwitch bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    Knob mix, output;
    juce::Random random;

    // Load / room / tone
    LoadScreen loadScreen;
    IconButton recordButton { IconButton::Icon::record, "record a room: play, clap or sing into this track, press again to stop (max 30 s)" };
    IconButton roomSaveButton { IconButton::Icon::save, "Save this room as a ROOM (decay, tone, resonances; no knobs)" };
    RoomScreen roomScreen { proc };
    ToneScreen toneScreen { proc };

    // Shape the room
    Knob intensity, balance, tail, space;
    juce::ToggleButton autoBalance { "auto" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAttachment;

    // Advanced (attached black section)
    AdvancedPanel advancedPanel { proc };

    // Finish (separate black module)
    EqPanel eqPanel { proc };
    Knob tape, autoEq;

    // STUDIOS (top bar) and ROOMS (load)
    void loadPreset (const juce::String& key);
    void loadRoom (const juce::String& key);
    void stepPreset (int delta);
    void openSaveDialog();
    void openRoomSaveDialog();
    juce::String submitSaveDialog (room::presets::SaveDialog::Mode, const room::presets::PresetMeta&,
                                   const juce::String& pack, bool overwrite);
    juce::String submitRoomSaveDialog (room::presets::SaveDialog::Mode, const room::presets::PresetMeta&,
                                       const juce::String& pack, bool overwrite);

    room::presets::PresetBrowser browser { proc, room::presets::Kind::studio };
    room::presets::PresetBrowser roomBrowser { proc, room::presets::Kind::room };
    room::presets::SaveDialog saveDialog { room::presets::Kind::studio };
    room::presets::SaveDialog roomSaveDialog { room::presets::Kind::room };
    juce::String editingKey, editingRoomKey;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RoomAudioProcessorEditor)
};
