#include "Randomizer.h"
#include "PluginProcessor.h"

namespace room::Randomizer
{
namespace
{
    const juce::Identifier intensityId { "_intensity" };   // not "intensity": that key is the intensity knob's tick

    // Each knob's musical band (Moderate stays inside it; Basic narrows it around its
    // middle; Extreme opens it up to the knob's whole range). `oftenOff`: a knob that's
    // usually best left at zero (pre-delay, ducking...), so it lands on zero now and then.
    struct Spread { const char* id; float lo, hi; bool oftenOff = false; };
    const Spread spreads[] = {
        { ParamIDs::intensity,   0.5f, 1.6f },
        { ParamIDs::toneAmount,  0.15f, 0.85f },
        { ParamIDs::spaceAmount, 0.15f, 0.85f },
        { ParamIDs::tail,        0.6f, 4.0f },
        { ParamIDs::size,        0.7f, 1.5f },
        { ParamIDs::smooth,      0.1f, 0.6f, true },
        { ParamIDs::mix,         0.25f, 0.6f },
        { ParamIDs::glitchAmount, 0.2f, 0.8f },
        { ParamIDs::preDelay,    5.0f, 120.0f, true },
        { ParamIDs::space,       0.7f, 1.6f },     // width
        { ParamIDs::damping,     -0.5f, 0.7f },
        { ParamIDs::duck,        0.2f, 0.7f, true },
        { ParamIDs::duckRelease, 120.0f, 600.0f },
        { ParamIDs::tape,        0.1f, 0.8f, true },
        { ParamIDs::autoEq,      0.1f, 0.6f, true },
        { ParamIDs::noiseLevel,  0.1f, 0.5f, true },
        { ParamIDs::noiseFollow, 0.0f, 1.0f },
        { ParamIDs::rate,        6000.0f, 44100.0f, true },   // "off" for rate is the top: handled below
        { "lfo1Rate",            0.1f, 4.0f },
        { "lfo2Rate",            0.1f, 4.0f },
    };

    juce::ValueTree selection (RoomAudioProcessor& p) { return p.randomizeSelection; }

    void setParam (RoomAudioProcessor& p, const juce::String& id, float value)
    {
        if (auto* prm = p.apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (value));
            prm->endChangeGesture();
        }
    }
}

const std::vector<Target>& targets()
{
    static const std::vector<Target> list = {
        // Room
        { "intensity", "intensity", "room", { ParamIDs::intensity } },
        { "tone",      "tone",      "room", { ParamIDs::toneAmount } },
        { "space",     "space",     "room", { ParamIDs::spaceAmount } },
        { "tail",      "tail",      "room", { ParamIDs::tail } },
        { "size",      "size",      "room", { ParamIDs::size } },
        { "smooth",    "smooth",    "room", { ParamIDs::smooth } },
        { "mix",       "dry / wet", "room", { ParamIDs::mix }, false },
        { "auto",      "auto",      "room", { ParamIDs::balanceAuto }, false },
        { "toneSync",  "tone sync", "room", { ParamIDs::toneSpaceSync }, false },

        // Glitch
        { "glitchOn",     "glitch on",     "glitch", { ParamIDs::glitch }, false },
        { "glitchAmount", "glitch amount", "glitch", { ParamIDs::glitchAmount } },
        { "glitchSync",   "glitch sync",   "glitch", { ParamIDs::glitchSync }, false },

        // Advanced
        { "preDelay", "pre-delay", "advanced", { ParamIDs::preDelay } },
        { "width",    "width",     "advanced", { ParamIDs::space } },
        { "damping",  "damping",   "advanced", { ParamIDs::damping } },
        { "duck",     "ducking",   "advanced", { ParamIDs::duck } },
        { "release",  "release",   "advanced", { ParamIDs::duckRelease } },
        { "tape",     "tape",      "advanced", { ParamIDs::tape } },
        { "autoEq",   "auto eq",   "advanced", { ParamIDs::autoEq } },
        { "freeze",   "freeze",    "advanced", { ParamIDs::freeze }, false },

        // Texture
        { "noiseType",   "noise type",  "texture", { ParamIDs::noiseType } },
        { "noiseLevel",  "noise level", "texture", { ParamIDs::noiseLevel }, false },
        { "noiseFollow", "follow",      "texture", { ParamIDs::noiseFollow } },
        { "rate",        "rate",        "texture", { ParamIDs::rate }, false },

        // LFOs (their settings; what they're assigned to stays)
        { "lfo1", "lfo 1", "lfos", { ParamIDs::lfoShape (0), ParamIDs::lfoRate (0), ParamIDs::lfoSync (0), ParamIDs::lfoDiv (0) }, false },
        { "lfo2", "lfo 2", "lfos", { ParamIDs::lfoShape (1), ParamIDs::lfoRate (1), ParamIDs::lfoSync (1), ParamIDs::lfoDiv (1) }, false },
    };
    return list;
}

bool isSelected (RoomAudioProcessor& p, const char* key)
{
    for (const auto& t : targets())
        if (juce::String (t.key) == key)
            return (bool) selection (p).getProperty (key, t.selectedByDefault);
    return false;
}

void setSelected (RoomAudioProcessor& p, const char* key, bool selected)
{
    p.randomizeSelection.setProperty (key, selected, nullptr);
}

double getIntensity (RoomAudioProcessor& p)
{
    return juce::jlimit (0.0, 1.0, (double) selection (p).getProperty (intensityId, 0.5));
}

void setIntensity (RoomAudioProcessor& p, double v)
{
    p.randomizeSelection.setProperty (intensityId, juce::jlimit (0.0, 1.0, v), nullptr);
}

float randomFor (juce::RangedAudioParameter& param, const juce::String& id, double intensity, juce::Random& rng)
{
    const auto t = (float) juce::jlimit (0.0, 1.0, intensity);
    const auto range = param.getNormalisableRange();

    // Switches and choices change more often as the intensity rises: 10 % of the time at
    // Basic, 85 % at Moderate, always at Extreme.
    if (dynamic_cast<juce::AudioParameterBool*> (&param) != nullptr || dynamic_cast<juce::AudioParameterChoice*> (&param) != nullptr)
    {
        const auto chance = t < 0.5f ? 0.10f + (0.85f - 0.10f) * t * 2.0f : 0.85f + 0.15f * (t - 0.5f) * 2.0f;
        const auto now = param.convertFrom0to1 (param.getValue());
        if (rng.nextFloat() >= chance)
            return now;
        // A change always lands somewhere new (a coin that lands the same way changed nothing).
        const auto steps = juce::jmax (2, param.getNumSteps());
        const auto current = juce::roundToInt (param.getValue() * (float) (steps - 1));
        const auto pick = (current + 1 + rng.nextInt (steps - 1)) % steps;
        return param.convertFrom0to1 ((float) pick / (float) (steps - 1));
    }

    for (const auto& s : spreads)
        if (id == s.id)
        {
            if (s.oftenOff && rng.nextFloat() < 0.5f * (1.0f - t))
                return id == ParamIDs::rate ? range.end : juce::jlimit (range.start, range.end, 0.0f);
            // Basic: the middle 40 % of the musical band; Moderate: all of it;
            // Extreme: out to the knob's whole range.
            auto lo = s.lo, hi = s.hi;
            if (t < 0.5f)
            {
                const auto mid = 0.5f * (lo + hi), half = 0.5f * (hi - lo) * (0.4f + 1.2f * t);
                lo = mid - half;
                hi = mid + half;
            }
            else
            {
                const auto w = (t - 0.5f) * 2.0f;
                lo += (range.start - lo) * w;
                hi += (range.end - hi) * w;
            }
            // Pick in the knob's own (skewed) feel, so log knobs spread evenly.
            const auto a = range.convertTo0to1 (juce::jlimit (range.start, range.end, juce::jmin (lo, hi)));
            const auto b = range.convertTo0to1 (juce::jlimit (range.start, range.end, juce::jmax (lo, hi)));
            return range.convertFrom0to1 (juce::jmin (a, b) + rng.nextFloat() * std::abs (b - a));
        }

    // Anything else: anywhere on the knob.
    return param.convertFrom0to1 (rng.nextFloat());
}

void randomizeSelected (RoomAudioProcessor& p, juce::Random& rng)
{
    const auto intensity = getIntensity (p);
    for (const auto& t : targets())
    {
        if (! isSelected (p, t.key))
            continue;
        for (const auto& id : t.params)
            if (auto* prm = p.apvts.getParameter (id))
                setParam (p, id, randomFor (*prm, id, intensity, rng));
    }
}

} // namespace room::Randomizer
