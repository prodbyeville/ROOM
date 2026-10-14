#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <array>
#include <atomic>

// LFO -> knob assignments ("modulation slots").
//
// Two LFOs, up to 8 targets each. The UI edits the slots on the message
// thread; the audio thread only reads them. Every field is an atomic, so
// there are no locks (a slot changing mid-block just takes effect next block).
//
// Depth is in "knob travel": 0.25 = the LFO swings the knob +/- a quarter of
// its range around wherever you set it. The knob itself never moves, so DAW
// automation keeps working.

namespace room
{

struct Modulation
{
    static constexpr int numLfos = 2;
    static constexpr int slotsPerLfo = 8;

    /** Knobs an LFO can move. Index = target id. */
    static const juce::StringArray& targetIds()
    {
        static const juce::StringArray t { "intensity", "balance", "tail", "space", "mix", "output",
                                           "tape", "autoEq", "preDelay", "size", "damping", "duck", "duckRelease" };
        return t;
    }
    static int numTargets() { return targetIds().size(); }
    static int targetIndex (const juce::String& paramId) { return targetIds().indexOf (paramId); }

    /** Targets that reshape the room itself (rebuilt on the worker thread). */
    static bool isRebuildTarget (int t)
    {
        const auto& id = targetIds()[t];
        return id == "intensity" || id == "tail" || id == "size" || id == "damping";
    }

    struct Slot
    {
        std::atomic<int> target { -1 };     // -1 = empty
        std::atomic<float> depth { 0.0f };  // -1 .. 1
    };

    std::array<std::array<Slot, slotsPerLfo>, numLfos> slots;

    /** Live modulation offset per target (normalised), written by the audio thread for the UI rings. */
    std::array<std::atomic<float>, 32> liveOffset {};
    /** Live LFO outputs (-1..1) and phases, for the LFO displays. */
    std::array<std::atomic<float>, numLfos> liveValue {}, livePhase {};

    //==========================================================================
    // Message thread

    /** Adds (or updates) an assignment. Returns false if this LFO is full. */
    bool assign (int lfo, int target, float depth)
    {
        for (auto& s : slots[(size_t) lfo])
            if (s.target.load() == target)
            {
                s.depth = depth;
                return true;
            }
        for (auto& s : slots[(size_t) lfo])
            if (s.target.load() < 0)
            {
                s.depth = depth;
                s.target = target;
                return true;
            }
        return false;
    }

    void remove (int lfo, int target)
    {
        for (auto& s : slots[(size_t) lfo])
            if (s.target.load() == target)
            {
                s.target = -1;
                s.depth = 0.0f;
            }
    }

    /** Depth of lfo -> target, or 0 if not assigned. */
    float depthOf (int lfo, int target) const
    {
        for (auto& s : slots[(size_t) lfo])
            if (s.target.load() == target)
                return s.depth.load();
        return 0.0f;
    }

    bool isAssigned (int lfo, int target) const
    {
        for (auto& s : slots[(size_t) lfo])
            if (s.target.load() == target)
                return true;
        return false;
    }

    bool anyAssigned() const
    {
        for (auto& l : slots)
            for (auto& s : l)
                if (s.target.load() >= 0)
                    return true;
        return false;
    }

    void clear()
    {
        for (auto& l : slots)
            for (auto& s : l)
            {
                s.target = -1;
                s.depth = 0.0f;
            }
    }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree t ("MODULATION");
        for (int l = 0; l < numLfos; ++l)
            for (auto& s : slots[(size_t) l])
                if (const auto target = s.target.load(); target >= 0)
                {
                    juce::ValueTree m ("MOD");
                    m.setProperty ("lfo", l, nullptr);
                    m.setProperty ("target", targetIds()[target], nullptr);
                    m.setProperty ("depth", s.depth.load(), nullptr);
                    t.appendChild (m, nullptr);
                }
        return t;
    }

    void fromValueTree (const juce::ValueTree& t)
    {
        clear();
        for (const auto& m : t)
        {
            const auto l = (int) m["lfo"];
            const auto target = targetIndex (m["target"].toString());
            if (m.hasType ("MOD") && l >= 0 && l < numLfos && target >= 0)
                assign (l, target, juce::jlimit (-1.0f, 1.0f, (float) (double) m["depth"]));
        }
    }
};

} // namespace room
