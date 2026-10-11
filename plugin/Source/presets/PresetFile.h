#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <map>

// One ROOM preset (".room" file): a small XML document.
//
//   <ROOM_PRESET version="1" name=".." author=".." type=".." styles="Bright,Warm"
//                notes=".." created="2026-10-11T12:00:00">
//     <PARAMS> <PARAM id="intensity" value="1.2"/> ... </PARAMS>
//     <ROOM_ANALYSIS ... />          (the analysed room, numbers only)
//   </ROOM_PRESET>
//
// Parameters are stored as real values (1.2, not 0.6), so presets stay valid
// if a parameter's range is changed later.

namespace room::presets
{

/** Fixed tag lists offered in the UI. */
const juce::StringArray& allTypes();
const juce::StringArray& allStyles();

struct PresetMeta
{
    juce::String name, author, type, notes;
    juce::StringArray styles;
    juce::Time created;
};

/** The parameter IDs a preset stores (everything except Output). */
bool isPresetParameter (const juce::String& paramId);

/** Build a preset tree from metadata, parameter values and the analysis tree. */
juce::ValueTree makePreset (const PresetMeta&, const std::map<juce::String, float>& params,
                            const juce::ValueTree& analysis);

PresetMeta readMeta (const juce::ValueTree& preset);
void writeMeta (juce::ValueTree& preset, const PresetMeta&);
std::map<juce::String, float> readParams (const juce::ValueTree& preset);

bool isValidPreset (const juce::ValueTree&);
juce::ValueTree parse (const juce::String& xmlText);
juce::ValueTree load (const juce::File&);

/** Saves safely: writes a temp file, then moves it into place. */
bool save (const juce::ValueTree& preset, const juce::File& destination);

/** Turns a preset name into a file name that's valid on Windows and macOS. */
juce::String toFileName (const juce::String& presetName);

constexpr const char* fileExtension = ".room";

} // namespace room::presets
