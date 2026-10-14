#include "PresetFile.h"
#include "../dsp/RoomAnalysis.h"
#include "../dsp/RoomSynth.h"

namespace room::presets
{

// Tone tag thresholds (dB of high-vs-low tilt in the tone curve).
// Most found sounds (claps, taps, hiss) sit a few dB bright, so "Bright" starts a bit higher.
static constexpr float toneDarkBelowDb = -4.0f;
static constexpr float toneBrightAboveDb = 8.0f;

const juce::StringArray& allTypes()
{
    static const juce::StringArray t { "Room", "Hall", "Chamber", "Plate", "Small Space",
                                       "Outdoor", "Object", "Texture", "Lo-Fi", "FX" };
    return t;
}

const juce::StringArray& allStyles()
{
    static const juce::StringArray s { "Bright", "Dark", "Warm", "Vintage", "Dirty", "Clean",
                                       "Airy", "Tight", "Wide", "Ambient", "Metallic", "Experimental" };
    return s;
}

const juce::StringArray& allMadeFor()
{
    static const juce::StringArray m { "Vocals", "Drums", "Keys", "Guitar", "Bass", "Synth", "Bus/Master", "FX" };
    return m;
}

const juce::StringArray& allDecayTags()
{
    static const juce::StringArray d { "Short", "Medium", "Long" };
    return d;
}

const juce::StringArray& allToneTags()
{
    static const juce::StringArray t { "Dark", "Neutral", "Bright" };
    return t;
}

const juce::StringArray& allSources()
{
    static const juce::StringArray s { "Factory", "File", "Recorded" };
    return s;
}

AutoTags autoTags (const juce::ValueTree& analysisTree, const juce::String& fallbackSource)
{
    AutoTags t;
    const auto a = RoomAnalysis::fromValueTree (analysisTree);
    t.source = allSources().contains (a.origin) ? a.origin : fallbackSource;
    if (! a.valid)
        return t;

    // Decay: how long the room rings (a steady sound has no decay of its own).
    t.medianT60 = a.steady ? 0.0f : a.medianT60;
    t.decay = a.steady || a.medianT60 < 0.6f ? "Short" : a.medianT60 <= 2.0f ? "Medium" : "Long";

    // Tone: the EQ this room puts on a sound (at Intensity 100%), highs against lows.
    const auto eq = targetEqCurveDb (a, 1.0f);
    float lo = 0.0f, hi = 0.0f;
    int nLo = 0, nHi = 0;
    for (size_t i = 0; i < a.toneFreqsHz.size() && i < eq.size(); ++i)
    {
        if (a.toneFreqsHz[i] >= 80.0f && a.toneFreqsHz[i] <= 500.0f)  { lo += eq[i]; ++nLo; }
        if (a.toneFreqsHz[i] >= 2000.0f && a.toneFreqsHz[i] <= 12000.0f) { hi += eq[i]; ++nHi; }
    }
    t.toneTiltDb = (nHi > 0 ? hi / (float) nHi : 0.0f) - (nLo > 0 ? lo / (float) nLo : 0.0f);
    const auto tilt = (nHi > 0 ? hi / (float) nHi : 0.0f) - (nLo > 0 ? lo / (float) nLo : 0.0f);
    t.tone = tilt < toneDarkBelowDb ? "Dark" : tilt > toneBrightAboveDb ? "Bright" : "Neutral";
    return t;
}

bool isPresetParameter (const juce::String& id)
{
    // Not part of a sound: your monitoring level and on/off states.
    return id != "output" && id != "bypass" && id != "freeze";
}

juce::ValueTree makePreset (const PresetMeta& meta, const std::map<juce::String, float>& params,
                            const juce::ValueTree& analysis)
{
    juce::ValueTree p ("ROOM_PRESET");
    p.setProperty ("version", 1, nullptr);
    writeMeta (p, meta);

    juce::ValueTree ps ("PARAMS");
    for (auto& [id, value] : params)
    {
        if (! isPresetParameter (id))
            continue;
        juce::ValueTree e ("PARAM");
        e.setProperty ("id", id, nullptr);
        e.setProperty ("value", value, nullptr);
        ps.appendChild (e, nullptr);
    }
    p.appendChild (ps, nullptr);

    if (analysis.isValid())
        p.appendChild (analysis.createCopy(), nullptr);
    return p;
}

juce::ValueTree makeRoom (const PresetMeta& meta, const juce::ValueTree& analysis)
{
    juce::ValueTree r ("ROOM");
    r.setProperty ("version", 2, nullptr);
    auto m = meta;
    m.madeFor.clear();
    m.roomName = {};
    writeMeta (r, m);
    if (analysis.isValid())
        r.appendChild (analysis.createCopy(), nullptr);
    return r;
}

juce::ValueTree roomFromPreset (const juce::ValueTree& preset, const juce::String& origin)
{
    auto analysis = preset.getChildWithName ("ROOM_ANALYSIS").createCopy();
    if (! analysis.isValid())
        return {};
    if (! analysis.hasProperty ("origin") && origin.isNotEmpty())
        analysis.setProperty ("origin", origin, nullptr);
    auto meta = readMeta (preset);
    if (meta.roomName.isNotEmpty())
        meta.name = meta.roomName;
    return makeRoom (meta, analysis);
}

PresetMeta readMeta (const juce::ValueTree& p)
{
    PresetMeta m;
    m.name = p["name"].toString();
    m.author = p["author"].toString();
    m.type = p["type"].toString();
    m.notes = p["notes"].toString();
    m.styles = juce::StringArray::fromTokens (p["styles"].toString(), ",", "");
    m.styles.trim();
    m.styles.removeEmptyStrings();
    m.madeFor = juce::StringArray::fromTokens (p["madeFor"].toString(), ",", "");
    m.madeFor.trim();
    m.madeFor.removeEmptyStrings();
    m.roomName = p["roomName"].toString();
    m.created = juce::Time::fromISO8601 (p["created"].toString());
    return m;
}

void writeMeta (juce::ValueTree& p, const PresetMeta& m)
{
    p.setProperty ("name", m.name, nullptr);
    p.setProperty ("author", m.author, nullptr);
    p.setProperty ("type", m.type, nullptr);
    p.setProperty ("styles", m.styles.joinIntoString (","), nullptr);
    p.setProperty ("notes", m.notes, nullptr);
    if (m.madeFor.isEmpty()) p.removeProperty ("madeFor", nullptr);
    else                     p.setProperty ("madeFor", m.madeFor.joinIntoString (","), nullptr);
    if (m.roomName.isEmpty()) p.removeProperty ("roomName", nullptr);
    else                      p.setProperty ("roomName", m.roomName, nullptr);
    p.setProperty ("created", (m.created.toMilliseconds() > 0 ? m.created : juce::Time::getCurrentTime())
                                  .toISO8601 (true), nullptr);
}

std::map<juce::String, float> readParams (const juce::ValueTree& p)
{
    std::map<juce::String, float> out;
    for (const auto& e : p.getChildWithName ("PARAMS"))
        if (e.hasType ("PARAM") && e.hasProperty ("id"))
            out[e["id"].toString()] = (float) (double) e["value"];
    return out;
}

bool isValidPreset (const juce::ValueTree& p)
{
    return p.hasType ("ROOM_PRESET") && p["name"].toString().isNotEmpty()
           && p.getChildWithName ("PARAMS").isValid();
}

bool isValidRoom (const juce::ValueTree& r)
{
    return r.hasType ("ROOM") && r["name"].toString().isNotEmpty()
           && r.getChildWithName ("ROOM_ANALYSIS").isValid();
}

bool isValid (const juce::ValueTree& t, Kind k)
{
    return k == Kind::room ? isValidRoom (t) : isValidPreset (t);
}

juce::ValueTree parse (const juce::String& text, Kind k)
{
    if (auto xml = juce::parseXML (text))
    {
        auto t = juce::ValueTree::fromXml (*xml);
        if (isValid (t, k))
            return t;
    }
    return {};
}

juce::ValueTree load (const juce::File& f, Kind k)
{
    if (! f.existsAsFile() || f.getSize() > 4 * 1024 * 1024)
        return {};
    return parse (f.loadFileAsString(), k);
}

bool save (const juce::ValueTree& preset, const juce::File& destination)
{
    auto xml = preset.createXml();
    if (xml == nullptr || ! destination.getParentDirectory().createDirectory())
        return false;

    juce::TemporaryFile temp (destination);
    if (! xml->writeTo (temp.getFile()))
        return false;
    return temp.overwriteTargetFileWithTemporary();
}

juce::String toFileName (const juce::String& name)
{
    auto s = juce::File::createLegalFileName (name.trim()).trim();
    // Windows also dislikes names ending in a dot or space.
    while (s.endsWithChar ('.') || s.endsWithChar (' '))
        s = s.dropLastCharacters (1);
    if (s.isEmpty())
        s = "Untitled";
    return s.substring (0, 80);
}

} // namespace room::presets
