#pragma once

#include <juce_graphics/juce_graphics.h>

namespace Colours
{
    const juce::Colour background { 0xff141417 };
    const juce::Colour panel      { 0xff1e1e23 };
    const juce::Colour outline    { 0xff34343c };
    const juce::Colour text       { 0xffe8e6e1 };
    const juce::Colour dim        { 0xff8a8892 };
    const juce::Colour accent     { 0xffd2a865 };  // decay / spatial
    const juce::Colour accent2    { 0xff6fa8dc };  // tone / tonal
    const juce::Colour reduction  { 0xffd9534f };  // Auto EQ cuts

    // One colour per EQ band node.
    const juce::Colour band[5] = { juce::Colour (0xff9b7fd4), juce::Colour (0xffd47fae),
                                   juce::Colour (0xffd2a865), juce::Colour (0xff7fc4a0),
                                   juce::Colour (0xff6fa8dc) };
}
