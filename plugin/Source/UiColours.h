#pragma once

#include <juce_graphics/juce_graphics.h>

// Eville palette: eville black #0A0A0A, crimson #BA2727, graphite #2A2A2A,
// dust white #F5F3EF. The console is dust-white painted steel, the modules
// (advanced, finish) and the overlays are black, every screen is dark glass.
namespace Colours
{
    // Brand
    const juce::Colour evilleBlack { 0xff0a0a0a };
    const juce::Colour crimson     { 0xffba2727 };
    const juce::Colour graphite    { 0xff2a2a2a };
    const juce::Colour dustWhite   { 0xfff5f3ef };

    // Dark surfaces (modules, overlays, dialogs)
    const juce::Colour background { 0xff111111 };
    const juce::Colour panel      { 0xff1a1a1a };
    const juce::Colour outline    { 0xff2e2e2e };
    const juce::Colour text       { 0xfff5f3ef };
    const juce::Colour dim        { 0xff8e8a83 };
    const juce::Colour accent     { 0xffba2727 };  // the one accent
    const juce::Colour accent2    { 0xffe8e3d9 };  // screen lines (dust white)
    const juce::Colour reduction  { 0xffc93030 };  // Auto EQ cuts

    // The console (painted steel) and what's printed on it
    const juce::Colour backdrop   { 0xffdcdad5 };
    const juce::Colour paper      { 0xffece8df };
    const juce::Colour ink        { 0xff1b1a18 };
    const juce::Colour inkDim     { 0xff77726a };
    const juce::Colour paperLine  { 0xffcbc6bb };

    // Screens
    const juce::Colour glass      { 0xff0b0b0b };
    const juce::Colour screenText { 0xffe9e5dc };
    const juce::Colour screenDim  { 0xff8a867f };

    // EQ band nodes: all dust white (the selected one turns crimson).
    const juce::Colour band[5] = { accent2, accent2, accent2, accent2, accent2 };
}
