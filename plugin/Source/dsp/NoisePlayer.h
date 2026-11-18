#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <vector>
#include "Biquad.h"

#if ROOM_HAS_NOISE_FILES
 #include "NoiseFiles.h"
#endif

// NOISE (the bottom module): a texture under the room, like a noise layer on a
// lo-fi box. Each profile is a stereo loop played round and round.
//
//   Profiles are the WAV files in Resources/Noise (compiled in, sorted by name,
//   so "01 vinyl.wav" comes first and shows as "vinyl"). Until that folder has
//   files, four placeholders made here stand in: vinyl, tape hiss, air, room tone.
//
//   Seamless: every loop is made once, off the audio thread, by crossfading its
//   last half second into its start (equal power, the right law for noise), so
//   the wrap has no seam or level dip.
//
//   Level: 0 = off (and then nothing runs: bit-identical).
//   Follow (like RC-20's): 0 = the noise plays all the time; 1 = only while
//   sound comes in, fading out in the gaps; in between, a blend.
//
// The audio thread never allocates: buffers are sized in prepare().

namespace room
{

class NoiseBank
{
public:
    static constexpr double loopRate = 44100.0;   // placeholders are made at this rate

    struct Loop
    {
        juce::String name;
        juce::AudioBuffer<float> audio;   // stereo, seamless
        double rate = loopRate;
    };

    NoiseBank()
    {
       #if ROOM_HAS_NOISE_FILES
        juce::WavAudioFormat wav;
        juce::AiffAudioFormat aiff;
        for (const auto& f : files())
        {
            int size = 0;
            const auto* data = NoiseFiles::getNamedResource (f.resource.toRawUTF8(), size);
            if (data == nullptr)
                continue;
            auto stream = std::make_unique<juce::MemoryInputStream> (data, (size_t) size, false);
            std::unique_ptr<juce::AudioFormatReader> reader (f.original.endsWithIgnoreCase (".wav")
                                                                 ? wav.createReaderFor (stream.release(), true)
                                                                 : aiff.createReaderFor (stream.release(), true));
            if (reader == nullptr)
                continue;
            const auto len = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 30.0));
            juce::AudioBuffer<float> raw (2, len);
            reader->read (&raw, 0, len, 0, true, true);
            if (reader->numChannels == 1)
                raw.copyFrom (1, 0, raw, 0, 0, len);
            add (displayName (f.original), std::move (raw), reader->sampleRate);
        }
       #endif
        if (loops.empty())
            makePlaceholders();
    }

    /** The profile names, in order (the noise type parameter's choices). */
    static juce::StringArray names()
    {
        juce::StringArray n;
       #if ROOM_HAS_NOISE_FILES
        for (const auto& f : files())
            n.add (displayName (f.original));
       #endif
        if (n.isEmpty())
            n = { "vinyl", "tape hiss", "air", "room tone" };
        return n;
    }

    int size() const { return (int) loops.size(); }
    const Loop& get (int i) const { return loops[(size_t) juce::jlimit (0, size() - 1, i)]; }

    /** Crossfade the end of `raw` into its start: the result loops with no seam. */
    static juce::AudioBuffer<float> makeSeamless (const juce::AudioBuffer<float>& raw, int fade)
    {
        const auto len = raw.getNumSamples();
        fade = juce::jlimit (1, std::max (1, len / 3), fade);
        const auto outLen = len - fade;
        juce::AudioBuffer<float> out (raw.getNumChannels(), outLen);
        for (int ch = 0; ch < raw.getNumChannels(); ++ch)
        {
            out.copyFrom (ch, 0, raw, ch, 0, outLen);
            // out[i] (i < fade) = head * sin + tail * cos; the tail is what followed out[outLen - 1].
            for (int i = 0; i < fade; ++i)
            {
                const auto t = ((float) i + 0.5f) / (float) fade * juce::MathConstants<float>::halfPi;
                out.setSample (ch, i, raw.getSample (ch, i) * std::sin (t) + raw.getSample (ch, outLen + i) * std::cos (t));
            }
        }
        return out;
    }

private:
   #if ROOM_HAS_NOISE_FILES
    struct File { juce::String resource, original; };
    static std::vector<File> files()
    {
        std::vector<File> f;
        for (int i = 0; i < NoiseFiles::namedResourceListSize; ++i)
        {
            const juce::String original (NoiseFiles::getNamedResourceOriginalFilename (NoiseFiles::namedResourceList[i]));
            if (original.endsWithIgnoreCase (".wav") || original.endsWithIgnoreCase (".aif") || original.endsWithIgnoreCase (".aiff"))
                f.push_back ({ NoiseFiles::namedResourceList[i], original });
        }
        std::sort (f.begin(), f.end(), [] (const File& a, const File& b) { return a.original.compareNatural (b.original) < 0; });
        return f;
    }
   #endif
    /** "02 Tape Hiss.wav" -> "tape hiss" */
    static juce::String displayName (const juce::String& file)
    {
        const auto n = file.upToLastOccurrenceOf (".", false, false).replaceCharacter ('_', ' ').trim();
        // drop a leading number used for ordering ("02 - tape hiss")
        const auto rest = n.trimCharactersAtStart ("0123456789 -.").trim();
        return (rest.isNotEmpty() ? rest : n).toLowerCase();
    }

    void add (const juce::String& name, juce::AudioBuffer<float> raw, double rate)
    {
        // Same level for every profile (about -20 dBFS rms), so Level means the same on each.
        float rms = 0.0f;
        for (int ch = 0; ch < raw.getNumChannels(); ++ch)
            rms = std::max (rms, raw.getRMSLevel (ch, 0, raw.getNumSamples()));
        if (rms > 1.0e-6f)
            raw.applyGain (0.1f / rms);
        loops.push_back ({ name, makeSeamless (raw, (int) (0.5 * rate)), rate });
    }

    void makePlaceholders()
    {
        const auto len = (int) (loopRate * 8.0);
        juce::Random rng (0x4e4f4953);
        auto white = [&rng] { return rng.nextFloat() * 2.0f - 1.0f; };

        // vinyl: soft hiss, a low rumble and sparse crackles of different sizes
        {
            juce::AudioBuffer<float> b (2, len);
            for (int ch = 0; ch < 2; ++ch)
            {
                float lp = 0.0f, rumble = 0.0f, hp = 0.0f, prev = 0.0f;
                for (int i = 0; i < len; ++i)
                {
                    lp += 0.25f * (white() - lp);
                    rumble += 0.0015f * (white() - rumble);
                    auto x = 0.18f * lp + 6.0f * rumble;
                    if (rng.nextFloat() < 0.0009f)   // a crackle: a short burst
                    {
                        const auto amp = 0.4f + 2.6f * std::pow (rng.nextFloat(), 3.0f);
                        x += amp * (rng.nextBool() ? 1.0f : -1.0f);
                    }
                    hp = 0.97f * (hp + x - prev);   // keep it off the very lows
                    prev = x;
                    b.setSample (ch, i, hp);
                }
            }
            add ("vinyl", std::move (b), loopRate);
        }
        // tape hiss: bright, steady
        {
            juce::AudioBuffer<float> b (2, len);
            for (int ch = 0; ch < 2; ++ch)
            {
                float lp = 0.0f, prev = 0.0f;
                for (int i = 0; i < len; ++i)
                {
                    const auto w = white();
                    lp += 0.6f * (w - lp);
                    b.setSample (ch, i, lp - 0.35f * prev);
                    prev = lp;
                }
            }
            add ("tape hiss", std::move (b), loopRate);
        }
        // air: soft, dark wind that swells slowly
        {
            juce::AudioBuffer<float> b (2, len);
            for (int ch = 0; ch < 2; ++ch)
            {
                float a = 0.0f, c = 0.0f;
                for (int i = 0; i < len; ++i)
                {
                    a += 0.06f * (white() - a);
                    c += 0.015f * (a - c);
                    // two slow swells, a whole number of cycles in the loop so they wrap too
                    const auto t = (double) i / (double) len;
                    const auto swell = 0.65f + 0.35f * (float) std::sin (juce::MathConstants<double>::twoPi * (3.0 * t + 0.4 * ch));
                    b.setSample (ch, i, (a - c) * swell);
                }
            }
            add ("air", std::move (b), loopRate);
        }
        // room tone: low, still: an empty room's hum and air
        {
            juce::AudioBuffer<float> b (2, len);
            for (int ch = 0; ch < 2; ++ch)
            {
                float lo = 0.0f, mid = 0.0f;
                for (int i = 0; i < len; ++i)
                {
                    lo += 0.008f * (white() - lo);
                    mid += 0.12f * (white() - mid);
                    const auto t = (double) i / loopRate;
                    const auto hum = 0.012f * (float) (std::sin (juce::MathConstants<double>::twoPi * 50.0 * t)
                                                       + 0.4 * std::sin (juce::MathConstants<double>::twoPi * 100.0 * t));
                    b.setSample (ch, i, 3.0f * lo + 0.12f * mid + hum);
                }
            }
            add ("room tone", std::move (b), loopRate);
        }
    }

    std::vector<Loop> loops;
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
        heads[0] = { -1, 0.0 };
        heads[1] = { -1, 0.0 };
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
            if (heads[0].loop < 0)
                heads[0] = { type, 0.0 };
            else if (type != heads[0].loop && fade >= 1.0f)
            {
                // crossfade to the new profile (a switch asked for mid-fade waits for it to end)
                heads[1] = heads[0];
                heads[0] = { type, 0.0 };
                fade = 0.0f;
            }
        }
    }

    bool isIdle() const { return level.getTargetValue() <= 0.0f && ! level.isSmoothing(); }

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
        if (bank == nullptr || bank->size() == 0 || heads[0].loop < 0)
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
    struct Head { int loop = -1; double pos = 0.0; };

    void read (Head& h, float* s, float weight)
    {
        const auto& l = bank->get (h.loop);
        const auto len = l.audio.getNumSamples();
        if (len < 2)
            return;
        const auto i0 = (int) h.pos;
        const auto i1 = (i0 + 1) % len;
        const auto f = (float) (h.pos - i0);
        for (int ch = 0; ch < 2; ++ch)
        {
            const auto a = l.audio.getSample (ch, i0), b = l.audio.getSample (ch, i1);
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

/** RATE: clean sample-rate reduction, like Decimort with its approximation and
    images filters on: an anti-alias low-pass at the new rate's Nyquist, the
    sample-and-hold at the new rate, then a low-pass that removes the images. What's
    left sounds like the sound at a lower sample rate (darker, a touch of the hold's
    character), with no crunchy aliasing. Both filters are 8th-order Butterworth
    (four biquads) at 0.45 x the new rate. At the top of the knob (44.1 kHz) it's off
    and nothing runs (bit-identical). */
class RateReducer
{
public:
    static constexpr float offHz = 44100.0f;

    void prepare (double sr) { sampleRate = sr; hzNow = offHz; setGlideFor (sr); reset(); }

    void reset()
    {
        phase = 1.0;
        held[0] = held[1] = 0.0f;
        for (auto& ch : pre) for (auto& f : ch) f.reset();
        for (auto& ch : post) for (auto& f : ch) f.reset();
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
        // Glide to the knob (about 20 ms, in octaves) so turning it never zips.
        hzNow += (targetHz - hzNow) * glide;
        if (--countdown <= 0)
        {
            countdown = 16;
            const auto cutoff = juce::jlimit (20.0, 0.45 * sampleRate, 0.45 * (double) hzNow);
            static constexpr double qs[4] = { 0.50980, 0.60134, 0.89998, 2.56292 };   // 8th-order Butterworth
            for (int k = 0; k < 4; ++k)
            {
                const auto c = design::lowpass (cutoff, qs[k], sampleRate);
                for (int ch = 0; ch < 2; ++ch)
                {
                    pre[ch][k].c = c;
                    post[ch][k].c = c;
                }
            }
        }
        phase += (double) hzNow / sampleRate;
        const auto take = phase >= 1.0;
        if (take)
            phase -= std::floor (phase);
        for (int ch = 0; ch < std::min (chans, 2); ++ch)
        {
            double y = x[ch];
            for (auto& f : pre[ch]) y = f.process (y);
            if (take)
                held[ch] = (float) y;
            double z = held[ch];
            for (auto& f : post[ch]) z = f.process (z);
            x[ch] = (float) z;
        }
    }

    /** A whole buffer (dry and wet together). */
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
    double sampleRate = 44100.0, phase = 1.0;
    float hzNow = offHz, glide = 0.001f;
    float held[2] {};
    Biquad pre[2][4], post[2][4];
    int countdown = 0;
};

} // namespace room
