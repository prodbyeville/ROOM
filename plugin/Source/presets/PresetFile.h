#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <map>

// Two kinds of file, both small XML documents:
//
// A ROOM (".room"): just the room, what makes it unique. Decay per band, tone,
// resonances, balance. Loading one swaps the room and leaves every knob alone.
//
//   <ROOM version="2" name=".." author=".." type="Hall" styles="Dark,Airy"
//         notes=".." created="..">
//     <ROOM_ANALYSIS origin="File" ... />
//   </ROOM>
//
// A STUDIO (".studio"): the whole sound. The room plus every knob and the LFOs.
//
//   <ROOM_PRESET version="1" name=".." author=".." madeFor="Vocals,Keys"
//                styles="Bright,Warm" roomName="Dark Hall" type="Hall" notes=".." created="..">
//     <PARAMS> <PARAM id="intensity" value="1.2"/> ... </PARAMS>
//     <ROOM_ANALYSIS ... />
//     <MODULATION> ... </MODULATION>
//   </ROOM_PRESET>
//
// Parameters are stored as real values (1.2, not 0.6), so presets stay valid
// if a parameter's range is changed later. (Before STUDIOS existed, full
// presets were saved as ".room" files; PresetLibrary::migrateLegacy converts them.)

namespace room::presets
{

enum class Kind { room, studio };

/** Fixed tag lists offered in the UI. */
const juce::StringArray& allTypes();      // ROOMS: what kind of space
const juce::StringArray& allStyles();     // ROOMS "Character", STUDIOS "Style"
const juce::StringArray& allMadeFor();    // STUDIOS: what it's for

/** Tags ROOM works out by itself from the analysis (ROOMS only). */
const juce::StringArray& allDecayTags();  // Short, Medium, Long
const juce::StringArray& allToneTags();   // Dark, Neutral, Bright
const juce::StringArray& allSources();    // Factory, File, Recorded

struct AutoTags
{
    juce::String decay, tone, source;
    float medianT60 = 0.0f;
    float toneTiltDb = 0.0f;   // highs minus lows of the room's EQ
};
AutoTags autoTags (const juce::ValueTree& analysisTree, const juce::String& fallbackSource);

struct PresetMeta
{
    juce::String name, author, type, notes;
    juce::StringArray styles;
    juce::StringArray madeFor;   // STUDIOS
    juce::String roomName;       // STUDIOS: the room inside it
    juce::Time created;
};

/** The parameter IDs a preset stores (everything except Output, Bypass, Freeze). */
bool isPresetParameter (const juce::String& paramId);

/** Build a preset tree from metadata, parameter values and the analysis tree. */
juce::ValueTree makePreset (const PresetMeta&, const std::map<juce::String, float>& params,
                            const juce::ValueTree& analysis);

PresetMeta readMeta (const juce::ValueTree& preset);
void writeMeta (juce::ValueTree& preset, const PresetMeta&);
std::map<juce::String, float> readParams (const juce::ValueTree& preset);

/** A ROOM file: meta plus the analysis tree. */
juce::ValueTree makeRoom (const PresetMeta&, const juce::ValueTree& analysis);

/** The room inside a STUDIO (or an old-style full preset) as a ROOM file. */
juce::ValueTree roomFromPreset (const juce::ValueTree& preset, const juce::String& origin);

bool isValidPreset (const juce::ValueTree&);   // a STUDIO
bool isValidRoom (const juce::ValueTree&);     // a ROOM
bool isValid (const juce::ValueTree&, Kind);
juce::ValueTree parse (const juce::String& xmlText, Kind = Kind::studio);
juce::ValueTree load (const juce::File&, Kind = Kind::studio);

/** Saves safely: writes a temp file, then moves it into place. */
bool save (const juce::ValueTree& preset, const juce::File& destination);

/** Turns a preset name into a file name that's valid on Windows and macOS. */
juce::String toFileName (const juce::String& presetName);

constexpr const char* roomExtension = ".room";
constexpr const char* studioExtension = ".studio";
inline const char* extensionFor (Kind k) { return k == Kind::room ? roomExtension : studioExtension; }

} // namespace room::presets
