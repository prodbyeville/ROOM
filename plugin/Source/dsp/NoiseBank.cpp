#include "NoisePlayer.h"

#if ROOM_HAS_NOISE_FILES
 #include "NoiseFiles.h"
#endif

namespace room
{

namespace
{
    // The four made-here profiles of 1.0.2, in their order (projects saved their index).
    const NoiseBank::Look builtInLooks[4] = {
        { 420, 0.22f, 0.10f, 1.5f, 0.10f },   // vinyl: sparse, with bright specks
        { 1500, 0.20f, 0.28f, 2.0f, 0.0f },   // tape hiss: dense, quick
        { 900, 0.42f, 0.07f, 1.0f, 0.0f },    // air: wide, slow swells
        { 1100, 0.14f, 0.05f, 0.7f, 0.0f },   // room tone: low and still
    };
    const char* builtInNames[4] = { "vinyl", "tape hiss", "air", "room tone" };

    // The recordings sit at about -22 dBFS rms; the built-ins at -20. This evens them out.
    constexpr float recordingGain = 1.2589f;

    /** All four built-ins, made in one go with one random sequence (as 1.0.2 did). */
    std::vector<juce::AudioBuffer<float>> makeBuiltIns()
    {
        constexpr double loopRate = NoiseBank::loopRate;
        std::vector<juce::AudioBuffer<float>> out;
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
            out.push_back (std::move (b));
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
            out.push_back (std::move (b));
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
            out.push_back (std::move (b));
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
            out.push_back (std::move (b));
        }
        return out;
    }
}

const std::vector<NoiseBank::Info>& NoiseBank::infos()
{
    static const std::vector<Info> list = []
    {
        std::vector<Info> l;
        for (int i = 0; i < 4; ++i)
            l.push_back ({ builtInNames[i], "classics", builtInLooks[i] });
       #if ROOM_HAS_NOISE_FILES
        int size = 0;
        if (const auto* text = NoiseFiles::getNamedResource ("noise_txt", size))
        {
            juce::StringArray lines;
            lines.addLines (juce::String::fromUTF8 (text, size));
            for (const auto& line : lines)
            {
                if (line.trim().isEmpty() || line.startsWith ("#"))
                    continue;
                const auto f = juce::StringArray::fromTokens (line, "|", "");
                if (f.size() < 8)
                    continue;
                // number n is parameter index n - 1: fill any gap so the numbers hold
                const auto index = f[0].getIntValue() - 1;
                if (index < 4)
                    continue;
                while ((int) l.size() < index)
                    l.push_back ({ "unused", "classics", {} });
                Info info { f[1], f[2], { f[3].getIntValue(), f[4].getFloatValue(), f[5].getFloatValue(),
                                          f[6].getFloatValue(), f[7].getFloatValue() } };
                if ((int) l.size() == index)
                    l.push_back (info);
                else
                    l[(size_t) index] = info;
            }
        }
       #endif
        return l;
    }();
    return list;
}

juce::StringArray NoiseBank::names()
{
    juce::StringArray n;
    for (const auto& i : infos())
        n.add (i.name);
    return n;
}

NoiseBank::NoiseBank() : juce::Thread ("ROOM noise")
{
    for (size_t i = 0; i < infos().size(); ++i)
        slots.push_back (std::make_unique<Slot>());

    auto builtIns = makeBuiltIns();
    for (int i = 0; i < 4; ++i)
        addBuiltIn (i, std::move (builtIns[(size_t) i]));

   #if ROOM_HAS_NOISE_FILES
    for (int r = 0; r < NoiseFiles::namedResourceListSize; ++r)
    {
        const juce::String file (NoiseFiles::getNamedResourceOriginalFilename (NoiseFiles::namedResourceList[r]));
        if (! file.endsWithIgnoreCase (".ogg"))
            continue;
        const auto index = file.getIntValue() - 1;   // "05 waves.ogg" -> 4
        if (index < 4 || index >= size())
            continue;
        int bytes = 0;
        slots[(size_t) index]->data = NoiseFiles::getNamedResource (NoiseFiles::namedResourceList[r], bytes);
        slots[(size_t) index]->dataSize = bytes;
    }
   #endif
    startThread (juce::Thread::Priority::low);
}

NoiseBank::~NoiseBank()
{
    stopThread (2000);
}

const NoiseBank::Loop* NoiseBank::tryGet (int i) const noexcept
{
    if (i < 0 || i >= size())
        return nullptr;
    auto& s = *slots[(size_t) i];
    if (const auto* l = s.ready.load (std::memory_order_acquire))
        return l;
    s.wanted.store (true, std::memory_order_relaxed);
    return nullptr;
}

const NoiseBank::Loop* NoiseBank::getNow (int i)
{
    if (i < 0 || i >= size())
        return nullptr;
    load (i);
    return slots[(size_t) i]->ready.load (std::memory_order_acquire);
}

void NoiseBank::run()
{
    while (! threadShouldExit())
    {
        for (int i = 0; i < size() && ! threadShouldExit(); ++i)
            if (slots[(size_t) i]->wanted.exchange (false, std::memory_order_relaxed))
                load (i);
        wait (15);
    }
}

void NoiseBank::load (int i)
{
    const juce::ScopedLock sl (loadLock);
    auto& s = *slots[(size_t) i];
    if (s.ready.load() != nullptr || s.data == nullptr)
        return;
    juce::OggVorbisAudioFormat ogg;
    std::unique_ptr<juce::AudioFormatReader> reader (
        ogg.createReaderFor (new juce::MemoryInputStream (s.data, (size_t) s.dataSize, false), true));
    if (reader == nullptr || reader->lengthInSamples < 2)
        return;
    const auto channels = juce::jlimit (1, 2, (int) reader->numChannels);
    const auto length = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> audio (channels, length);
    reader->read (&audio, 0, length, 0, true, channels > 1);

    auto loop = std::make_unique<Loop>();
    loop->channels = channels;
    loop->length = length;
    loop->rate = reader->sampleRate;
    const auto peak = juce::jmax (1.0e-6f, audio.getMagnitude (0, length) * recordingGain);
    loop->scale = peak / 32767.0f;
    loop->data.resize ((size_t) (length * channels));
    for (int k = 0; k < length; ++k)
        for (int ch = 0; ch < channels; ++ch)
            loop->data[(size_t) (k * channels + ch)]
                = (int16_t) juce::roundToInt (audio.getSample (ch, k) * recordingGain / peak * 32767.0f);
    s.owned = std::move (loop);
    s.ready.store (s.owned.get(), std::memory_order_release);
}

void NoiseBank::addBuiltIn (int i, juce::AudioBuffer<float> raw)
{
    // Same level for every profile (about -20 dBFS rms), so Level means the same on each.
    float rms = 0.0f;
    for (int ch = 0; ch < raw.getNumChannels(); ++ch)
        rms = std::max (rms, raw.getRMSLevel (ch, 0, raw.getNumSamples()));
    if (rms > 1.0e-6f)
        raw.applyGain (0.1f / rms);
    const auto audio = makeSeamless (raw, (int) (0.5 * loopRate));
    auto loop = std::make_unique<Loop>();
    loop->channels = 2;
    loop->length = audio.getNumSamples();
    loop->rate = loopRate;
    const auto peak = juce::jmax (1.0e-6f, audio.getMagnitude (0, loop->length));
    loop->scale = peak / 32767.0f;
    loop->data.resize ((size_t) (loop->length * 2));
    for (int k = 0; k < loop->length; ++k)
        for (int ch = 0; ch < 2; ++ch)
            loop->data[(size_t) (k * 2 + ch)] = (int16_t) juce::roundToInt (audio.getSample (ch, k) / peak * 32767.0f);
    auto& s = *slots[(size_t) i];
    s.owned = std::move (loop);
    s.ready.store (s.owned.get(), std::memory_order_release);
}

juce::AudioBuffer<float> NoiseBank::makeSeamless (const juce::AudioBuffer<float>& raw, int fade)
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

} // namespace room
