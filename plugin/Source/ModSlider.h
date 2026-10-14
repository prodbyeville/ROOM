#pragma once

#include "Modulation.h"
#include <juce_gui_basics/juce_gui_basics.h>

// A rotary knob that LFOs can be dropped onto.
//   - drop "LFO 1" / "LFO 2" (dragged from the LFO panel) to assign it
//   - a coloured ring shows how far the LFO swings the knob, and a dot shows
//     where it is right now
//   - right-click: modulate with LFO 1/2, set depth, invert, remove
//   - each assigned LFO also gets a small dot beside the knob (Serum style):
//     drag it up/down to set the amount (Shift = fine), double-click to remove,
//     right-click for the menu

class ModSlider : public juce::Slider,
                  public juce::DragAndDropTarget,
                  private juce::Timer
{
public:
    ModSlider() : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow) {}

    /** Make this knob an LFO target (target = index in Modulation::targetIds()). */
    void setModTarget (room::Modulation* m, int targetIndex);

    /** Re-check assignments (they can change from presets or other knobs). */
    void refreshModulation() { updateTimer(); }

    static juce::Colour lfoColour (int lfo);
    static juce::String dragDescription (int lfo) { return "LFO" + juce::String (lfo + 1); }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    /** Where the depth dot for this LFO sits (empty if not assigned or no room for it). */
    juce::Rectangle<float> depthDotBounds (int lfo) const;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragEnter (const SourceDetails&) override { dropHighlight = true; repaint(); }
    void itemDragExit (const SourceDetails&) override { dropHighlight = false; repaint(); }
    void itemDropped (const SourceDetails&) override;

private:
    void timerCallback() override;
    void showModMenu();
    void updateTimer();

    struct RingGeometry { juce::Point<float> centre; float ringR = 0.0f; };
    RingGeometry ringGeometry() const;
    int dotAt (juce::Point<float>) const;

    room::Modulation* mod = nullptr;
    int target = -1;
    bool dropHighlight = false;
    int hoverDot = -1, dragDot = -1;
    float dragStartDepth = 0.0f;
};
