#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Drawing helpers for the hardware look: glass screens, keys, LEDs, screws,
// the printed dotted path and the creases in the metal.
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

    /** A translucent crimson key, lit from inside while `lit` (bypass / power). */
    void drawGlowKey (juce::Graphics&, juce::Rectangle<float>, bool lit, bool over, bool down);
    /** A small black key with an LED in its cap (sync, retrig). */
    void drawMiniKey (juce::Graphics&, juce::Rectangle<float>, bool lit, bool over, bool down);

    /** The face of a key drawn by drawKey (inside its well), for icons and text. */
    juce::Rectangle<float> keyCap (juce::Rectangle<float> keyBounds);

    /** Photographed parts (made in Higgsfield). radius / size = the part itself. */
    void drawLed (juce::Graphics&, juce::Point<float> centre, bool on, float radius = 3.5f);
    void drawScrew (juce::Graphics&, juce::Point<float> centre, bool onBlack = false, float size = 12.0f);
    void drawToggle (juce::Graphics&, juce::Point<float> centre, bool up, float size);
    void drawJack (juce::Graphics&, juce::Point<float> centre, float size);

    /** Lines pressed into the metal (panel creases, frames). */
    void drawGroove (juce::Graphics&, juce::Point<float> a, juce::Point<float> b, bool onBlack);
    void drawGrooveRect (juce::Graphics&, juce::Rectangle<float>, float corner, bool onBlack);

    /** A printed dotted line along a path (dots every `spacing` px). */
    void drawDottedPath (juce::Graphics&, const juce::Path&, juce::Colour, float spacing, float dotRadius);

    /** An embedded image asset (cached). Invalid if it isn't there. */
    juce::Image asset (const char* fileName);

    /** Painted-steel / paper grain, pre-rendered (cached by size). */
    juce::Image grain (int width, int height, bool dark, int seed = 3);
}
