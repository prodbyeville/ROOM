#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// ROOM's hardware look (round 5): the parts drawn from photographed assets
// (Resources/Ui/r5_*, made with Higgsfield) and the bits drawn in code around them.
//
//   case     the near-black brushed casing behind everything
//   cell     a control's recessed pocket in the case, with a little depth
//   knob     the machined knob (photo), with its tick ring, crimson value arc and dot
//   key      cream or black keys (photo, stretched by their middle so the corners keep
//            their shape), the label printed on top, a red underglow while on
//   lines    the signal lines between cells: solid, dotted white, dotted crimson;
//            ends are a dot, an open circle or nothing, and stop short of the cells
namespace R5
{
    enum class KeyKind { cream, black, square };

    void drawCase (juce::Graphics&, juce::Rectangle<float> area);
    void drawCell (juce::Graphics&, juce::Rectangle<float> cell);

    /** The machined knob. `ring` is the tick ring's radius; the body sits inside it. */
    void drawKnob (juce::Graphics&, juce::Point<float> centre, float ring, float startAngle, float endAngle,
                   float angle, float arcFrom, bool enabled, bool hover, bool ticks = true, bool light = false);

    /** Space a key button leaves under the key for its lit glow (EvilleLookAndFeel::drawR5Key). */
    constexpr float glowRoom = 8.0f;

    /** A key with its label; `led` adds a small light above the label (D / R / N). */
    void drawKey (juce::Graphics&, juce::Rectangle<float> bounds, KeyKind, const juce::String& label,
                  bool on, bool over, bool down, bool led = false);

    enum class End { none, dot, ring };
    enum class Stroke { solid, dotted, dottedRed };
    /** A route through the points (right angles, softly rounded), ending in `end`. */
    void drawRoute (juce::Graphics&, std::initializer_list<juce::Point<float>> points, Stroke, End startEnd, End finishEnd);

    /** Uppercase Satoshi label and its value, centred over x (the cells' titles). */
    void drawLabel (juce::Graphics&, const juce::String& text, juce::Point<float> centre, float height = 10.5f, float alpha = 0.92f,
                    bool ink = false);
    void drawValue (juce::Graphics&, const juce::String& text, juce::Point<float> centre, float height = 16.0f, float alpha = 1.0f);
}
