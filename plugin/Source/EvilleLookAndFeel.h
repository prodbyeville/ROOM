#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Eville fonts (embedded) and the hardware look: black knobs with printed
// scales, tactile keys, LED toggles, glass screens. Every component that isn't
// custom painted picks these up from the editor's look and feel.
//
// Per-component options are set as properties:
//   "surface"   "paper" (default, the dust-white console) or "black" (modules)
//   "knobRadius"  knob size in px (otherwise it fills the component)
//   "scaleMin" / "scaleMid" / "scaleMax"  printed numbers around a knob
//   "key"       "light" or "dark": draw a TextButton as a hardware key
//   "style"     "led": draw a ToggleButton as an LED with a label
//               "minikey": a small black key with an LED in it, label beside

namespace Fonts
{
    enum Weight { regular, medium, bold, black };
    juce::Typeface::Ptr satoshi (Weight);
    juce::Font sans (float height, Weight = medium);
    juce::Font serif (float height);   // Cormorant Garamond Medium Italic
    juce::Font mono (float height);    // screen readouts
}

namespace UiAssetFiles
{
    /** Finds an embedded asset by its original file name. */
    const char* get (const char* fileName, int& sizeOut);
}

class EvilleLookAndFeel : public juce::LookAndFeel_V4
{
public:
    EvilleLookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                           juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;

    // Menus, tooltips and value bubbles: black glass, lowercase Satoshi, a crimson accent.
    juce::Font getPopupMenuFont() override { return Fonts::sans (14.0f); }
    int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override { return menuMargin + 6; }
    void drawPopupMenuBackgroundWithOptions (juce::Graphics&, int width, int height, const juce::PopupMenu::Options&) override;
    void drawPopupMenuItemWithOptions (juce::Graphics&, const juce::Rectangle<int>& area, bool highlighted,
                                       const juce::PopupMenu::Item&, const juce::PopupMenu::Options&) override;
    void drawPopupMenuSectionHeaderWithOptions (juce::Graphics&, const juce::Rectangle<int>& area,
                                                const juce::String& name, const juce::PopupMenu::Options&) override;
    void getIdealPopupMenuItemSizeWithOptions (const juce::String& text, bool isSeparator, int standardHeight,
                                               int& idealWidth, int& idealHeight, const juce::PopupMenu::Options&) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
    void drawBubble (juce::Graphics&, juce::BubbleComponent&, const juce::Point<float>& tip, const juce::Rectangle<float>& body) override;

    /** Show a menu in the room style, wherever the host puts it. */
    static void showMenu (juce::PopupMenu&, juce::Component& target);
    static constexpr int menuMargin = 10;   // room for the menu's shadow
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Fonts::sans (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int height) override;
    juce::Font getSliderPopupFont (juce::Slider&) override { return Fonts::mono (12.0f); }
    int getSliderPopupPlacement (juce::Slider&) override { return juce::BubbleComponent::above; }

    /** Knob centre and radius inside a slider (shared with ModSlider's rings). */
    static juce::Rectangle<float> knobBounds (const juce::Slider&);
    static bool onBlack (const juce::Component&);
};
