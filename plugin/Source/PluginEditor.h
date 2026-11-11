#pragma once

#include "PluginProcessor.h"
#include "EqPanel.h"
#include "AdvancedPanel.h"
#include "EvilleLookAndFeel.h"
#include "presets/PresetBrowser.h"
#include "UpdateChecker.h"

// The ROOM unit (eville): one piece of hardware filling the window. The
// advanced section (right) opens from its key; the window grows to fit it.
//
//   +-------------------------------------------------+-------------+
//   | [≡] room  [♥ studios / name < >][save] [randomize] | advanced    |
//   |                  [advanced] (wet) (out) [bypass]|  knobs      |
//   | ( load )        ( room )        ( tone )        |  freeze     |
//   |  (intensity)  (balance)  (tail)  (space)        |             |
//   +-------------------------------------------------+  lfo 1      |
//   | finish   [ eq screen ]   (tape) (auto eq)  out  |  lfo 2      |
//   +-------------------------------------------------+-------------+
//
// Dust-white console, black finish and advanced. Everything is laid out at
// 1152 x 772 (892 wide with advanced closed) and drawn through a scale transform, so the window resizes
// freely (fixed aspect).

class RoomAudioProcessorEditor;

/** The stage (ROOM 1.0): the heart of the window. A soft blurred colour field
    (faint, stronger as intensity goes up), the ROOMS as grainy cards drifting
    slowly around the room box in the middle (click one to load it; the loaded
    one has a red edge), and the room's name or what's happening along the
    bottom. Drop a sound anywhere on it to build a room; right-click for more.
    The motion stops while the mouse is over it, so a card holds still to click. */
class LoadScreen : public juce::Component,
                   public juce::SettableTooltipClient,
                   public juce::FileDragAndDropTarget,
                   private juce::Timer
{
public:
    explicit LoadScreen (RoomAudioProcessor&);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override { hoverKey.clear(); repaint(); }

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { hovering = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hovering = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    std::function<void()> onOpenLibrary;
    std::function<void (const juce::String& key)> onLoadRoom;
    void chooseSoundFile();

    /** Where the room box sits (the editor puts RoomScreen there). */
    juce::Rectangle<int> boxArea() const;
    /** The cards shown, front to back order not implied (for tests). */
    juce::StringArray cardKeys() const;
    /** Move the orbit (tests, screenshots). */
    void setOrbitPhase (float p) { orbitPhase = p; repaint(); }

private:
    void timerCallback() override;
    struct Card { juce::String key, name, decay, tone; juce::Rectangle<float> bounds; float depth = 0.0f; };
    void refreshCards();
    void paintCard (juce::Graphics&, const Card&, float scale, float angle, float alpha, bool loaded, bool hot);

    RoomAudioProcessor& proc;
    std::unique_ptr<juce::FileChooser> chooser;
    bool hovering = false;
    std::vector<Card> cards;
    juce::String hoverKey;
    float orbitPhase = 0.0f;
    int libraryStamp = -1;
};

/** The room itself, drawn live: a box in 3D, turning slowly, whose floor holds
    the decay per band. It moves with the knobs (and the LFOs): intensity = glow,
    tail = how far back the decay reaches, space = width, size = depth and height,
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
    float phase = 0.0f, yaw = 0.62f, pulsePos = 2.0f, lastLevel = 0.0f, levelSmooth = 0.0f;
};

/** The tone the room puts on a sound: its EQ curve, with the strongest
    resonance marked in crimson. */
class ToneScreen : public juce::Component,
                   public juce::SettableTooltipClient,
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

/** A chip with an icon (randomize, save) or a caps label (record, save room,
    all rooms, advanced); the menu key is a bare icon. */
class IconButton : public juce::Button
{
public:
    enum class Icon { randomize, record, save, advanced, menu };
    IconButton (Icon i, const juce::String& tooltip);
    void paintButton (juce::Graphics&, bool over, bool down) override;
    void mouseDown (const juce::MouseEvent&) override;

    /** Called on right-click (instead of a click). */
    std::function<void()> onRightClick;

    /** A small crimson dot inside the key (the menu key: an update is waiting). */
    void setBadge (bool b) { if (badge != b) { badge = b; repaint(); } }
    /** Draw as a text chip with this label (and the icon's dot for record). */
    void setChipText (const juce::String& t) { chipText = t; repaint(); }

private:
    Icon icon;
    bool badge = false;
    juce::String chipText;
};

/** Bypass: a round power switch, filled red while ROOM is on. */
class BypassSwitch : public juce::Button
{
public:
    BypassSwitch() : juce::Button ("bypass") { setClickingTogglesState (true); setTooltip ("bypass"); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
};

/** The first-run tour: dims the window, lights up one area at a time and
    explains it on a small card (back / next / skip). */
class TourOverlay : public juce::Component
{
public:
    struct Step { juce::Rectangle<int> spot; juce::String title, text; };
    TourOverlay();
    void start (std::vector<Step>);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onFinished;
    int getStep() const { return step; }
    void showStep (int);

private:
    juce::Rectangle<int> cardArea() const;
    std::vector<Step> steps;
    int step = 0;
    juce::TextButton back { "back" }, next { "next" }, skip { "skip" };
};

/** The sign-in page (WARP's, in ROOM's dress), over the whole unit until this
    computer is activated: the email and key from the "get room free" email,
    activate, and links to get a key and to support. While it shows, the
    controls underneath can't be touched and the audio passes through dry. */
class LicensePage : public juce::Component,
                    private juce::ChangeListener
{
public:
    explicit LicensePage (RoomAudioProcessor&);
    ~LicensePage() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    /** Show / hide to match the processor; call after any license change. */
    void update();
    void focus() { email.grabKeyboardFocus(); }
    std::function<void()> onChange;   // the editor repaints

    /** Fill in the fields and the message under them (deactivate errors, tests, screenshots). */
    void fill (const juce::String& e, const juce::String& k, const juce::String& text, bool good)
    {
        email.setText (e, false);
        key.setText (k, false);
        message = text;
        messageIsGood = good;
        repaint();
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    juce::Rectangle<int> cardArea() const;
    juce::Rectangle<int> labelFor (const juce::Component& c) const { return c.getBounds().translated (0, -17).withHeight (14); }
    void submit();
    void show (const juce::String& text, bool good) { message = text; messageIsGood = good; repaint(); }
    RoomAudioProcessor& proc;
    juce::TextEditor email, key;
    juce::TextButton activateButton { "activate" }, getButton { "get room free" }, helpButton { "lost your key?" };
    juce::String message;
    bool busy = false, messageIsGood = false;
};

class RoomAudioProcessorEditor : public juce::AudioProcessorEditor,
                                 public juce::DragAndDropContainer,
                                 private juce::Timer
{
public:
    explicit RoomAudioProcessorEditor (RoomAudioProcessor&);
    ~RoomAudioProcessorEditor() override;

    void resized() override;

    static constexpr int baseWidth = 1152, closedWidth = 892, baseHeight = 772;

    /** Open or close the advanced section; the window grows / shrinks to fit. */
    void setAdvancedOpen (bool);
    bool isAdvancedOpen() const { return proc.advancedOpen; }
    /** Width of the layout right now (base coordinates). */
    int contentWidth() const { return proc.advancedOpen ? baseWidth : closedWidth; }

    /** Open (or close) the LFO controls in the advanced section (opens advanced first). */
    void setAdvanced (bool showLfos);
    /** Tips on / off (the main menu's "tips"). */
    bool areTipsEnabled() const { return tooltips != nullptr; }
    /** Menu actions (public for tests). */
    void newPreset();
    void showTour (int startStep = 0);
    /** Show the tour the first time the editor opens (tests switch it off). */
    static inline bool autoShowTour = true;
    void openAbout() { showAbout(); }

    /** Show the full room info on the room screen. */
    void showRoomInfo (bool show) { roomScreen.setShowInfo (show); }

private:
    void timerCallback() override;
    void layoutContent();
    void paintContent (juce::Graphics&);
    void paintUnit (juce::Graphics&);
    void paintConsoleArt (juce::Graphics&);

    struct Knob
    {
        ModSlider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    void setupKnob (Knob&, const juce::String& paramId, const juce::String& name, float radius,
                    const juce::String& scaleMin, const juce::String& scaleMid, const juce::String& scaleMax,
                    bool onBlack, const juce::String& tip = {});

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
    std::unique_ptr<juce::TooltipWindow> tooltips;   // only while tips are on
    void setTipsEnabled (bool);

    // Main menu (top left)
    IconButton menuButton { IconButton::Icon::menu, "menu" };
    void showMainMenu();
    void showAbout();
    void confirmDeactivate();
    UpdateChecker updates;
    TourOverlay tour;
    LicensePage licensePage { proc };
    int ticksUntilUpdateCheck = 45;   // a few seconds after the window opens
    int ticksUntilTour = 6;           // first open: once the window has settled

    // Fixed geometry (base coordinates): the top bar, then the stage, the
    // controls module and the bottom module; advanced pops out on the right.
    const juce::Rectangle<float> consoleArea { 0.0f, 0.0f, 892.0f, 772.0f };
    const juce::Rectangle<float> stageArea { 12.0f, 72.0f, 868.0f, 400.0f };
    const juce::Rectangle<float> controlsArea { 12.0f, 484.0f, 868.0f, 132.0f };
    const juce::Rectangle<float> finishArea { 12.0f, 628.0f, 868.0f, 132.0f };
    const juce::Rectangle<float> advancedArea { 892.0f, 0.0f, 260.0f, 772.0f };
    juce::Point<float> finishLed, finishJack;
    juce::Image staticLayer;   // the window, the modules and their titles, at the window's pixel size
    float staticScale = 0.0f;
    void paintValues (juce::Graphics&);

    // Top bar
    room::presets::PresetBar presetBar { "studios" };
    IconButton randomButton { IconButton::Icon::randomize, "randomize the knobs (right-click to choose what moves)" };
    IconButton advancedButton { IconButton::Icon::advanced, "advanced: pre-delay, size, damping, ducking, freeze and the lfos" };
    void showRandomizeMenu();
    BypassSwitch bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    Knob mix, output;
    juce::Random random;

    // Load / room / tone
    LoadScreen loadScreen;
    IconButton recordButton { IconButton::Icon::record, "record a room: play, clap or sing into this track, press again to stop (max 30 s)" };
    IconButton roomSaveButton { IconButton::Icon::save, "Save this room as a ROOM (decay, tone, resonances; no knobs)" };
    IconButton allRoomsButton { IconButton::Icon::save, "every ROOM: search, favourites, packs" };
    RoomScreen roomScreen { proc };
    ToneScreen toneScreen { proc };

    // Shape the room
    Knob intensity, balance, tail, space;
    juce::ToggleButton autoBalance { "auto" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> autoAttachment;

    // Advanced (black, the right side)
    AdvancedPanel advancedPanel { proc };

    // Finish (black, along the bottom)
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
