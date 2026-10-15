#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Drawing helpers for the hardware look: glass screens, keys, LEDs, screws,
// printed dotted paths and the little room outlines on the faceplate.
namespace Hw
{
    /** A recessed dark glass screen: bezel, vignette, faint scanlines. Returns the glass area. */
    juce::Rectangle<float> drawScreen (juce::Graphics&, juce::Rectangle<float> bounds);

    /** Draw on top of screen contents: scanlines and a soft reflection. */
    void drawScreenOverlay (juce::Graphics&, juce::Rectangle<float> glass);

    /** A line with a little phosphor bloom. */
    void strokeGlow (juce::Graphics&, const juce::Path&, juce::Colour, float width, float glow = 1.0f);

    /** Tactile square key. light = dust-white plastic, otherwise black. */
    void drawKey (juce::Graphics&, juce::Rectangle<float>, bool light, bool over, bool down);

    void drawLed (juce::Graphics&, juce::Point<float> centre, bool on, float radius = 3.5f);
    void drawScrew (juce::Graphics&, juce::Point<float> centre, bool onBlack = false);

    /** A printed dotted line along a path (dots every `spacing` px). */
    void drawDottedPath (juce::Graphics&, const juce::Path&, juce::Colour, float spacing, float dotRadius);

    /** A tiny wireframe room (a box with a doorway), drawn like an engraving. */
    void drawTinyRoom (juce::Graphics&, juce::Point<float> centre, float size, juce::Colour);

    /** An embedded image asset (cached). Invalid if it isn't there. */
    juce::Image asset (const char* fileName);

    /** Painted-steel / paper grain, pre-rendered (cached by size). */
    juce::Image grain (int width, int height, bool dark, int seed = 3);
}
