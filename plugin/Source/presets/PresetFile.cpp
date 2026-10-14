#include "PresetFile.h"

namespace room::presets
{

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

juce::ValueTree parse (const juce::String& text)
{
    if (auto xml = juce::parseXML (text))
    {
        auto t = juce::ValueTree::fromXml (*xml);
        if (isValidPreset (t))
            return t;
    }
    return {};
}

juce::ValueTree load (const juce::File& f)
{
    if (! f.existsAsFile() || f.getSize() > 4 * 1024 * 1024)
        return {};
    return parse (f.loadFileAsString());
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
