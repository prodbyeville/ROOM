#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

class RoomAudioProcessor;

// Randomize (WARP's, made for ROOM): picks new values for the items ticked on the
// Randomize page (right-click the randomize key). The ticks and the intensity live in
// a "RandomizeSelection" child of the project state; presets leave them out.
// Message thread only: it turns parameters the way a mouse would, with gestures, so
// the host records it.
namespace room::Randomizer
{
    struct Target
    {
        const char* key;       // saved in the project (which items are ticked)
        const char* label;
        const char* group;
        std::vector<juce::String> params;
        bool selectedByDefault = true;   // switches that change what ROOM does start unticked
    };

    const std::vector<Target>& targets();

    bool isSelected (RoomAudioProcessor&, const char* key);
    void setSelected (RoomAudioProcessor&, const char* key, bool selected);

    /** 0 Basic (musical, few switches), 0.5 Moderate (the default), 1 Extreme (whole ranges). */
    double getIntensity (RoomAudioProcessor&);
    void setIntensity (RoomAudioProcessor&, double);

    /** Randomizes every ticked item. */
    void randomizeSelected (RoomAudioProcessor&, juce::Random&);

    /** A new value (real units) for one parameter at an intensity (for tests). */
    float randomFor (juce::RangedAudioParameter&, const juce::String& id, double intensity, juce::Random&);
}
