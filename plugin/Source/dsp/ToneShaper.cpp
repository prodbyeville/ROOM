#include "ToneShaper.h"

namespace room
{

namespace
{
    // Q of the resonant first stage of each Tape filter (Butterworth would be 0.54).
    constexpr double kTapeBumpQ = 0.8;
    constexpr double kSmoothingS = 0.03;
}

void tone::designTape (float amount, double sr, BiquadCoeffs out[4])
{
    const auto [lo, hi] = tapeCutoffs (amount);
    const auto hiClamped = std::min (hi, 0.45 * sr);
    out[0] = design::highpass (lo, kTapeBumpQ, sr);
    out[1] = design::highpass (lo, design::kButter4Q[1], sr);
    out[2] = design::lowpass (hiClamped, kTapeBumpQ, sr);
    out[3] = design::lowpass (hiClamped, design::kButter4Q[1], sr);
}

BiquadCoeffs tone::designBand (const EqBand& b, double sr)
{
    const auto f = std::min ((double) b.freqHz, 0.45 * sr);
    switch (b.type)
    {
        case EqBand::Type::lowShelf:  return design::lowShelf (f, b.gainDb, sr);
        case EqBand::Type::highShelf: return design::highShelf (f, b.gainDb, sr);
        case EqBand::Type::peak:      break;
    }
    return design::peaking (f, std::max (0.1f, b.q), b.gainDb, sr);
}

double tone::responseDb (float tapeAmount, const EqBand* bands, double f, double sr)
{
    double db = 0.0;
    if (tapeAmount > 0.0005f)
    {
        BiquadCoeffs t[4];
        designTape (tapeAmount, sr, t);
        for (auto& c : t)
            db += c.magnitudeDb (f, sr);
    }
    for (int i = 0; i < numEqBands; ++i)
        if (std::abs (bands[i].gainDb) > 0.01f)
            db += designBand (bands[i], sr).magnitudeDb (f, sr);
    return db;
}

//==============================================================================

void ToneShaper::prepare (double sr, int numChannels)
{
    sampleRate = sr;
    numChans = juce::jlimit (1, maxChannels, numChannels);

    tapeSm.reset (sr, kSmoothingS);
    for (int i = 0; i < tone::numEqBands; ++i)
    {
        gainSm[i].reset (sr, kSmoothingS);
        freqSm[i].reset (sr, kSmoothingS);
        qSm[i].reset (sr, kSmoothingS);
        freqSm[i].setCurrentAndTargetValue (tone::kBandFreqs[i]);
        qSm[i].setCurrentAndTargetValue (tone::kBandQs[i]);
    }
    reset();
}

void ToneShaper::reset()
{
    for (auto& ch : tapeF) for (auto& f : ch) f.reset();
    for (auto& ch : eqF) for (auto& f : ch) f.reset();
}

void ToneShaper::updateCoefficients (float tape, const EqBand (&bands)[tone::numEqBands])
{
    juce::ignoreUnused (bands);

    // Tape: fully bypassed at 0 so the knob at zero is a true "off".
    const auto wasTape = tapeActive;
    tapeActive = tape > 0.0005f;
    if (tapeActive)
    {
        BiquadCoeffs c[4];
        tone::designTape (tape, sampleRate, c);
        for (int ch = 0; ch < numChans; ++ch)
            for (int k = 0; k < 4; ++k)
            {
                tapeF[ch][k].c = c[k];
                if (! wasTape) tapeF[ch][k].reset();
            }
    }

    for (int b = 0; b < tone::numEqBands; ++b)
    {
        EqBand band { tone::kBandTypes[b], freqSm[b].getCurrentValue(), gainSm[b].getCurrentValue(), qSm[b].getCurrentValue() };
        const auto was = eqActive[b];
        eqActive[b] = std::abs (band.gainDb) > 0.01f;
        if (! eqActive[b])
            continue;
        const auto c = tone::designBand (band, sampleRate);
        for (int ch = 0; ch < numChans; ++ch)
        {
            eqF[ch][b].c = c;
            if (! was) eqF[ch][b].reset();
        }
    }
}

void ToneShaper::process (juce::AudioBuffer<float>& buffer, float tapeAmount, const EqBand (&bands)[tone::numEqBands])
{
    const auto n = buffer.getNumSamples();
    const auto chans = std::min (buffer.getNumChannels(), numChans);

    tapeSm.setTargetValue (tapeAmount);
    for (int b = 0; b < tone::numEqBands; ++b)
    {
        gainSm[b].setTargetValue (bands[b].gainDb);
        freqSm[b].setTargetValue (juce::jlimit (20.0f, 20000.0f, bands[b].freqHz));
        qSm[b].setTargetValue (bands[b].q);
    }

    for (int start = 0; start < n; start += updateInterval)
    {
        const auto len = std::min (updateInterval, n - start);

        // Advance the smoothers by this chunk, then design for where they landed.
        tapeSm.skip (len);
        for (int b = 0; b < tone::numEqBands; ++b)
        {
            gainSm[b].skip (len);
            freqSm[b].skip (len);
            qSm[b].skip (len);
        }
        updateCoefficients (tapeSm.getCurrentValue(), bands);

        for (int ch = 0; ch < chans; ++ch)
        {
            auto* d = buffer.getWritePointer (ch, start);
            for (int i = 0; i < len; ++i)
            {
                double x = d[i];
                if (tapeActive)
                    for (auto& f : tapeF[ch])
                        x = f.process (x);
                for (int b = 0; b < tone::numEqBands; ++b)
                    if (eqActive[b])
                        x = eqF[ch][b].process (x);
                d[i] = (float) x;
            }
        }
    }
}

} // namespace room
