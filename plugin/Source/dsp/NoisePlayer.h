#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <atomic>
#include <memory>
#include <vector>
#include "Biquad.h"


// NOISE (the bottom module): a texture under the room, like a noise layer on a
// lo-fi box. Each profile is a loop played round and round.
//
//   Profiles 1-4 (vinyl, tape hiss, air, room tone) are made here, as in 1.0.2. The rest
//   are Ethan's recordings in Resources/Noise: "NN name.ogg" plus noise.txt (number,
//   name, menu section, the screen's look), all made by Tools/noise/make_noise_loops.py,
//   which turns each recording into a seamless loop (the end flows into the start through
//   a crossfade picked where the two match best) and checks the seam. The number is the
//   noise type parameter's index + 1 and is saved in projects: only ever append.
//
//   Memory: the recordings are compiled in compressed and a loop is unpacked (16-bit) the
//   first time it's chosen, on the bank's own thread; the audio thread only asks for it
//   and keeps playing what it has until it's there. prepareToPlay unpacks the chosen one
//   up front, so a bounce starts with its noise.
//
//   Level: 0 = off (and then nothing runs: bit-identical).
//   Follow (like RC-20's): 0 = the noise plays all the time; 1 = only while
//   sound comes in, fading out in the gaps; in between, a blend.
//
// The audio thread never allocates or locks: buffers are sized in prepare().

namespace room
{

class NoiseBank : private juce::Thread
{
public:
    static constexpr double loopRate = 44100.0;   // built-ins are made at this rate

    /** How the noise screen draws a profile: particles, band thickness, drift speed,
        slow swells, bright specks. */
    struct Look { int count = 900; float thickness = 0.2f, speed = 0.1f, waves = 1.0f, sparkle = 0.0f; };
    struct Info { juce::String name, section; Look look; };

    /** A loop, 16-bit, mono or stereo (interleaved), seamless. */
    struct Loop
    {
        int channels = 2, length = 0;
        double rate = loopRate;
        float scale = 1.0f / 32767.0f;
        std::vector<int16_t> data;
        float sample (int ch, int i) const noexcept
        {
            return scale * (float) data[(size_t) (i * channels + (channels > 1 ? ch : 0))];
        }
    };

    NoiseBank();
    ~NoiseBank() override;

    /** Every profile, in parameter order (index = number - 1). */
    static const std::vector<Info>& infos();
    static juce::StringArray names();
    int size() const { return (int) slots.size(); }

    /** Audio thread: the loop if it's unpacked, else nullptr (and it's asked for). */
    const Loop* tryGet (int i) const noexcept;
    /** Not the audio thread: the loop, unpacked now if need be. */
    const Loop* getNow (int i);

    /** Crossfade the end of `raw` into its start: the result loops with no seam. */
    static juce::AudioBuffer<float> makeSeamless (const juce::AudioBuffer<float>& raw, int fade);

private:
    struct Slot
    {
        std::atomic<const Loop*> ready { nullptr };
        mutable std::atomic<bool> wanted { false };
        std::unique_ptr<Loop> owned;
        const void* data = nullptr;   // compressed recording (nullptr: a built-in)
        int dataSize = 0;
    };

    void run() override;
    void load (int i);
    void addBuiltIn (int i, juce::AudioBuffer<float> raw);
    static juce::AudioBuffer<float> makeBuiltIn (int which);

    std::vector<std::unique_ptr<Slot>> slots;
    juce::CriticalSection loadLock;
};

class NoisePlayer
{
public:
    void setBank (const NoiseBank* b) { bank = b; }

    void prepare (double sr, int maxBlock)
    {
        sampleRate = sr;
        gate.assign ((size_t) std::max (1, maxBlock), 0.0f);
        level.reset (sr, 0.05);
        level.setCurrentAndTargetValue (0.0f);
        envAttack = (float) std::exp (-1.0 / (0.005 * sr));
        envRelease = (float) std::exp (-1.0 / (0.15 * sr));
        gateUp = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));
        gateDown = (float) (1.0 - std::exp (-1.0 / (0.3 * sr)));
        fadeStep = (float) (1.0 / std::max (1.0, 0.05 * sr));
        idleDown = (float) (1.0 - std::exp (-1.0 / (0.03 * sr / 4.0)));
        idleUp = (float) (1.0 - std::exp (-1.0 / (0.01 * sr / 4.0)));
        reset();
    }

    void reset()
    {
        env = 0.0f;
        gateNow = 0.0f;
        heads[0] = {};
        heads[1] = {};
        fade = 1.0f;
        level.setCurrentAndTargetValue (0.0f);
    }

    /** Set the knobs for the next slice (amount: 0..1, follow: 0..1). */
    void setTargets (int type, float amount, float followAmount)
    {
        const auto a = juce::jlimit (0.0f, 1.0f, amount);
        level.setTargetValue (1.4f * a * a);
        follow = juce::jlimit (0.0f, 1.0f, followAmount);
        if (bank != nullptr && bank->size() > 0)
        {
            type = juce::jlimit (0, bank->size() - 1, type);
            // A profile that isn't unpacked yet is asked for; until it's there the one
            // playing carries on (or, at the very start, nothing plays).
            if (type != heads[0].index || heads[0].loop == nullptr)
                if (const auto* l = bank->tryGet (type))
                {
                    if (heads[0].loop == nullptr)
                        heads[0] = { type, l, 0.0 };
                    else if (fade >= 1.0f)
                    {
                        // crossfade to the new profile (a switch asked for mid-fade waits for it to end)
                        heads[1] = heads[0];
                        heads[0] = { type, l, 0.0 };
                        fade = 0.0f;
                    }
                }
        }
    }

    bool isIdle() const { return level.getTargetValue() <= 0.0f && ! level.isSmoothing(); }
    /** For the screen: how open the follow gate is now (0 nothing coming in .. 1 playing),
        and how much the stop has faded the noise out (0 silenced .. 1 playing). */
    float gateNowValue() const noexcept { return gateNow; }
    float idleGainValue() const noexcept { return idleGain; }

    /** Stop: nothing is playing (transport stopped, nothing coming in), so the noise
        fades out (30 ms) until something plays again (back in 10 ms). */
    void setIdle (bool nothingPlaying) { idle = nothingPlaying; }

    /** Before the room: follow the input's level (per sample, for Follow). */
    void trackInput (const juce::AudioBuffer<float>& in, int numSamples)
    {
        const auto n = std::min (numSamples, (int) gate.size());
        const auto chans = std::min (2, in.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            for (int ch = 0; ch < chans; ++ch)
                x = std::max (x, std::abs (in.getSample (ch, i)));
            env = x > env ? envAttack * env + (1.0f - envAttack) * x : envRelease * env + (1.0f - envRelease) * x;
            // -54 dB: nothing coming in; -24 dB and up: playing
            const auto db = juce::Decibels::gainToDecibels (env, -120.0f);
            auto target = juce::jlimit (0.0f, 1.0f, (db + 54.0f) / 30.0f);
            target = target * target * (3.0f - 2.0f * target);
            gateNow += (target > gateNow ? gateUp : gateDown) * (target - gateNow);
            gate[(size_t) i] = gateNow;
        }
    }

    /** After the room: add the noise to `out`. */
    void addTo (juce::AudioBuffer<float>& out, int numSamples)
    {
        if (bank == nullptr || heads[0].loop == nullptr)
            return;
        const auto n = std::min (numSamples, (int) gate.size());
        const auto chans = std::min (2, out.getNumChannels());
        for (int i = 0; i < n; ++i)
        {
            idleGain += (idle ? idleDown : idleUp) * ((idle ? 0.0f : 1.0f) - idleGain);
            const auto g = level.getNextValue() * idleGain * ((1.0f - follow) + follow * gate[(size_t) i]);
            float s[2] {};
            read (heads[0], s, fade < 1.0f ? std::sin (fade * juce::MathConstants<float>::halfPi) : 1.0f);
            if (fade < 1.0f)
            {
                read (heads[1], s, std::cos (fade * juce::MathConstants<float>::halfPi));
                fade = std::min (1.0f, fade + fadeStep);
            }
            for (int ch = 0; ch < chans; ++ch)
                out.getWritePointer (ch)[i] += g * (chans == 1 ? 0.5f * (s[0] + s[1]) : s[ch]);
        }
    }

private:
    struct Head { int index = -1; const NoiseBank::Loop* loop = nullptr; double pos = 0.0; };

    void read (Head& h, float* s, float weight)
    {
        if (h.loop == nullptr)
            return;
        const auto& l = *h.loop;
        const auto len = l.length;
        if (len < 2)
            return;
        const auto i0 = (int) h.pos;
        const auto i1 = (i0 + 1) % len;
        const auto f = (float) (h.pos - i0);
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto a = l.sample (ch, i0), b = l.sample (ch, i1);
            s[ch] += weight * (a + f * (b - a));
        }
        h.pos += l.rate / sampleRate;
        if (h.pos >= len)
            h.pos -= len;
    }

    const NoiseBank* bank = nullptr;
    double sampleRate = 44100.0;
    std::vector<float> gate;
    juce::SmoothedValue<float> level;
    float follow = 0.0f, env = 0.0f, gateNow = 0.0f;
    bool idle = false;
    float idleGain = 1.0f, idleDown = 0.01f, idleUp = 0.01f;
    float envAttack = 0.0f, envRelease = 0.0f, gateUp = 0.0f, gateDown = 0.0f, fadeStep = 0.01f;
    Head heads[2];
    float fade = 1.0f;
};

/** RATE: clean sample-rate reduction. What resampling down to the chosen rate and
    back up leaves of a sound is exactly its band below the new Nyquist, so that's
    what this keeps: one steep low-pass (12th order Butterworth, six biquads) at 0.45 x
    the chosen rate. No sample-and-hold: holding steps on a host-sample grid that the
    new rate doesn't divide jitters, and that jitter is noise right in the audible band
    (the crunch). At the top of the knob (44.1 kHz) it's off and nothing runs
    (bit-identical). */
class RateReducer
{
public:
    static constexpr float offHz = 44100.0f;

    void prepare (double sr) { sampleRate = sr; hzNow = offHz; setGlideFor (sr); reset(); }

    void reset()
    {
        for (auto& ch : lp) for (auto& f : ch) f.reset();
        countdown = 0;
    }

    static bool isOff (float hz) { return hz >= offHz - 1.0f; }

    /** True once the knob is at the top and the glide has arrived there: nothing to do. */
    bool settledOff (float targetHz)
    {
        if (! isOff (targetHz) || hzNow < offHz - 30.0f)
            return false;
        if (hzNow != offHz)
        {
            hzNow = offHz;
            reset();
        }
        return true;
    }

    /** One sample: `x` holds one value per channel and is changed in place. */
    void processSample (float* x, int chans, float targetHz)
    {
        // Glide to the knob in octaves (about 20 ms), so turning it never zips.
        hzNow = std::exp (std::log (hzNow) + (std::log (std::max (1.0f, targetHz)) - std::log (hzNow)) * glide);
        if (--countdown <= 0)
        {
            countdown = 16;
            const auto cutoff = juce::jlimit (20.0, 0.45 * sampleRate, 0.45 * (double) hzNow);
            static constexpr double qs[6] = { 0.50431, 0.54120, 0.63023, 0.82134, 1.30656, 3.83065 };   // 12th-order Butterworth
            for (int k = 0; k < 6; ++k)
            {
                const auto c = design::lowpass (cutoff, qs[k], sampleRate);
                for (int ch = 0; ch < 2; ++ch)
                    lp[ch][k].c = c;
            }
        }
        for (int ch = 0; ch < std::min (chans, 2); ++ch)
        {
            double y = x[ch];
            for (auto& f : lp[ch]) y = f.process (y);
            x[ch] = (float) y;
        }
    }

    /** A whole buffer. */
    void process (juce::AudioBuffer<float>& buffer, int numSamples, float targetHz)
    {
        const auto chans = std::min (2, buffer.getNumChannels());
        for (int i = 0; i < numSamples; ++i)
        {
            float x[2] {};
            for (int ch = 0; ch < chans; ++ch) x[ch] = buffer.getSample (ch, i);
            processSample (x, chans, targetHz);
            for (int ch = 0; ch < chans; ++ch) buffer.setSample (ch, i, x[ch]);
        }
    }

    void setGlideFor (double sr) { glide = (float) (1.0 - std::exp (-1.0 / (0.02 * sr))); }

private:
    double sampleRate = 44100.0;
    float hzNow = offHz, glide = 0.001f;
    Biquad lp[2][6];
    int countdown = 0;
};

} // namespace room
