#include "EqPanel.h"
#include "UiColours.h"
#include "Screens.h"
#include "EvilleLookAndFeel.h"

namespace
{
    constexpr float kFMin = 20.0f, kFMax = 20000.0f;
    constexpr float kSpecTopDb = 0.0f, kSpecBottomDb = -84.0f;
    constexpr float kSpecTiltDbPerOct = 4.5f;   // so typical music looks roughly flat
    constexpr float kNodeRadius = 5.0f;
}

EqPanel::EqPanel (RoomAudioProcessor& p) : proc (p)
{
    spectrumDb.fill (kSpecBottomDb);
    setRepaintsOnMouseActivity (false);
    setTooltip ("the final eq. drag a band to boost or cut, scroll to make it wider or narrower, double-click to reset it.");
    startTimerHz (30);
}

EqPanel::~EqPanel() = default;

//==============================================================================
// Coordinates

juce::Rectangle<float> EqPanel::plotArea() const
{
    // Inside the glass: room for dB numbers on the left and Hz numbers below.
    return getLocalBounds().toFloat().reduced (4.0f).withTrimmedLeft (34.0f).withTrimmedBottom (20.0f)
                                                     .withTrimmedTop (22.0f).withTrimmedRight (12.0f);
}

float EqPanel::xForFreq (float f) const
{
    const auto r = plotArea();
    return r.getX() + r.getWidth() * std::log (f / kFMin) / std::log (kFMax / kFMin);
}

float EqPanel::freqForX (float x) const
{
    const auto r = plotArea();
    return kFMin * std::pow (kFMax / kFMin, juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth()));
}

float EqPanel::yForEqDb (float db) const
{
    const auto r = plotArea();
    return r.getCentreY() - r.getHeight() * 0.5f * db / eqRangeDb;
}

float EqPanel::eqDbForY (float y) const
{
    const auto r = plotArea();
    return (r.getCentreY() - y) / (r.getHeight() * 0.5f) * eqRangeDb;
}

float EqPanel::yForSpectrumDb (float db) const
{
    const auto r = plotArea();
    const auto t = juce::jlimit (0.0f, 1.0f, (db - kSpecBottomDb) / (kSpecTopDb - kSpecBottomDb));
    return r.getBottom() - t * r.getHeight();
}

//==============================================================================
// Parameters

juce::RangedAudioParameter* EqPanel::param (const juce::String& id) const
{
    return proc.apvts.getParameter (id);
}

void EqPanel::setParam (const juce::String& id, float value)
{
    if (auto* p = param (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

juce::Point<float> EqPanel::nodePosition (int band) const
{
    room::EqBand bands[room::tone::numEqBands];
    proc.getEqBands (bands);
    return { xForFreq (bands[band].freqHz), yForEqDb (bands[band].gainDb) };
}

int EqPanel::nodeAt (juce::Point<float> pos) const
{
    int best = -1;
    float bestDist = kNodeRadius + 4.0f;
    for (int b = 0; b < room::tone::numEqBands; ++b)
    {
        const auto d = nodePosition (b).getDistanceFrom (pos);
        if (d < bestDist) { bestDist = d; best = b; }
    }
    return best;
}

//==============================================================================
// Mouse

void EqPanel::mouseMove (const juce::MouseEvent& e)
{
    const auto b = nodeAt (e.position);
    if (b != hoverBand)
    {
        hoverBand = b;
        setMouseCursor (b >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void EqPanel::mouseExit (const juce::MouseEvent&)
{
    hoverBand = -1;
    repaint();
}

void EqPanel::mouseDown (const juce::MouseEvent& e)
{
    dragBand = nodeAt (e.position);
    if (dragBand < 0)
        return;
    param (ParamIDs::eqFreq (dragBand))->beginChangeGesture();
    param (ParamIDs::eqGain (dragBand))->beginChangeGesture();
}

void EqPanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0)
        return;
    const auto r = plotArea();
    const auto x = juce::jlimit (r.getX(), r.getRight(), e.position.x);
    const auto y = juce::jlimit (r.getY(), r.getBottom(), e.position.y);
    setParam (ParamIDs::eqFreq (dragBand), freqForX (x));
    setParam (ParamIDs::eqGain (dragBand), juce::jlimit (-12.0f, 12.0f, eqDbForY (y)));
    repaint();
}

void EqPanel::mouseUp (const juce::MouseEvent&)
{
    if (dragBand < 0)
        return;
    param (ParamIDs::eqFreq (dragBand))->endChangeGesture();
    param (ParamIDs::eqGain (dragBand))->endChangeGesture();
    dragBand = -1;
}

void EqPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto b = nodeAt (e.position);
    if (b < 0)
        return;
    for (auto& id : { ParamIDs::eqFreq (b), ParamIDs::eqGain (b), ParamIDs::eqQ (b) })
    {
        auto* p = param (id);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getDefaultValue());
        p->endChangeGesture();
    }
    repaint();
}

void EqPanel::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto b = hoverBand >= 0 ? hoverBand : nodeAt (e.position);
    if (b < 0)
        return;
    auto* p = param (ParamIDs::eqQ (b));
    const auto q = p->convertFrom0to1 (p->getValue());
    const auto newQ = juce::jlimit (0.3f, 8.0f, q * std::pow (1.15f, w.deltaY * 4.0f));
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 (newQ));
    p->endChangeGesture();
    repaint();
}

//==============================================================================
// Analyzer

void EqPanel::timerCallback()
{
    updateSpectrum();
    repaint();
}

void EqPanel::updateSpectrum()
{
    // Pull whatever arrived since last time into a rolling history buffer.
    int got;
    bool any = false;
    while ((got = proc.spectrumTap.pull (pullBuf.data(), (int) pullBuf.size())) > 0)
    {
        any = true;
        if (got >= fftSize)
            std::copy (pullBuf.begin() + (got - fftSize), pullBuf.begin() + got, history.begin());
        else
        {
            std::move (history.begin() + got, history.end(), history.begin());
            std::copy_n (pullBuf.begin(), got, history.end() - got);
        }
    }

    if (! any)
    {
        // Nothing playing: let the display fall away.
        for (auto& v : spectrumDb)
            v = std::max (kSpecBottomDb, v - 3.0f);
        return;
    }

    std::copy (history.begin(), history.end(), fftBuf.begin());
    std::fill (fftBuf.begin() + fftSize, fftBuf.end(), 0.0f);
    window.multiplyWithWindowingTable (fftBuf.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftBuf.data(), true);

    const auto sr = std::max (1.0, proc.getCurrentSampleRate());
    const auto norm = 4.0f / fftSize;  // Hann window: a full-scale sine reads ~0 dB
    for (int i = 0; i < numSpectrumPoints; ++i)
    {
        const auto f0 = kFMin * std::pow (kFMax / kFMin, (float) i / numSpectrumPoints);
        const auto f1 = kFMin * std::pow (kFMax / kFMin, (float) (i + 1) / numSpectrumPoints);
        const auto b0 = juce::jlimit (1, fftSize / 2, (int) std::floor (f0 * fftSize / sr));
        const auto b1 = juce::jlimit (b0, fftSize / 2, (int) std::ceil (f1 * fftSize / sr));
        float mag = 0.0f;
        for (int b = b0; b <= b1; ++b)
            mag = std::max (mag, fftBuf[(size_t) b]);

        const auto fc = std::sqrt (f0 * f1);
        const auto db = juce::Decibels::gainToDecibels (mag * norm, -120.0f)
                      + kSpecTiltDbPerOct * std::log2 (fc / 1000.0f);
        auto& s = spectrumDb[(size_t) i];
        s = db > s ? db : std::max (db, s - 2.5f);   // fast rise, slower fall
    }
}

//==============================================================================
// Drawing

void EqPanel::resized() {}

void EqPanel::paint (juce::Graphics& g)
{
    const auto glass = Hw::drawScreen (g, getLocalBounds().toFloat());

    const auto r = plotArea();
    g.saveState();
    g.reduceClipRegion (r.toNearestInt());

    // --- grid ---
    g.setColour (Colours::screenDim.withAlpha (0.15f));
    for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        g.drawVerticalLine (juce::roundToInt (xForFreq (f)), r.getY(), r.getBottom());
    for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
        g.drawHorizontalLine (juce::roundToInt (yForEqDb (db)), r.getX(), r.getRight());
    g.setColour (Colours::screenDim.withAlpha (0.3f));
    g.drawHorizontalLine (juce::roundToInt (yForEqDb (0.0f)), r.getX(), r.getRight());

    // --- live spectrum ---
    {
        juce::Path spec;
        spec.startNewSubPath (r.getX(), r.getBottom());
        for (int i = 0; i < numSpectrumPoints; ++i)
        {
            const auto f = kFMin * std::pow (kFMax / kFMin, (i + 0.5f) / numSpectrumPoints);
            spec.lineTo (xForFreq (f), yForSpectrumDb (spectrumDb[(size_t) i]));
        }
        spec.lineTo (r.getRight(), r.getBottom());
        spec.closeSubPath();
        g.setGradientFill (juce::ColourGradient (Colours::screenDim.withAlpha (0.30f), 0.0f, r.getY(),
                                                 Colours::screenDim.withAlpha (0.04f), 0.0f, r.getBottom(), false));
        g.fillPath (spec);
        g.setColour (Colours::screenDim.withAlpha (0.35f));
        g.strokePath (spec, juce::PathStrokeType (0.8f));
    }

    // --- Auto EQ reduction (hangs down from the 0 dB line) ---
    {
        const auto& sup = proc.getSuppressor();
        juce::Path red;
        const auto zero = yForEqDb (0.0f);
        red.startNewSubPath (r.getX(), zero);
        float maxRed = 0.0f;
        for (int i = 0; i < room::ResonanceSuppressor::numDisplayPoints; ++i)
        {
            const auto rd = sup.getDisplayReductionDb (i);
            maxRed = std::max (maxRed, rd);
            red.lineTo (xForFreq (room::ResonanceSuppressor::displayFrequency (i)), yForEqDb (-rd));
        }
        red.lineTo (r.getRight(), zero);
        red.closeSubPath();
        if (maxRed > 0.05f)
        {
            // Fill only: a stroke would also trace the 0 dB line where nothing is cut.
            g.setGradientFill (juce::ColourGradient (Colours::reduction.withAlpha (0.35f), 0.0f, zero,
                                                     Colours::reduction.withAlpha (0.85f), 0.0f, yForEqDb (-eqRangeDb), false));
            g.fillPath (red);
        }
    }

    // --- Tape + EQ curve ---
    room::EqBand bands[room::tone::numEqBands];
    proc.getEqBands (bands);
    const auto tape = proc.apvts.getRawParameterValue (ParamIDs::tape)->load();
    const auto sr = proc.getCurrentSampleRate() > 0 ? proc.getCurrentSampleRate() : 48000.0;
    {
        juce::Path curve;
        const int steps = juce::jmax (64, (int) r.getWidth() / 2);
        for (int i = 0; i <= steps; ++i)
        {
            const auto x = r.getX() + r.getWidth() * (float) i / steps;
            const auto db = (float) room::tone::responseDb (tape, bands, freqForX (x), sr);
            const auto y = yForEqDb (juce::jlimit (-eqRangeDb * 1.2f, eqRangeDb * 1.2f, db));
            if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
        }
        juce::Path fill (curve);
        fill.lineTo (r.getRight(), yForEqDb (0.0f));
        fill.lineTo (r.getX(), yForEqDb (0.0f));
        fill.closeSubPath();
        g.setColour (Colours::screenText.withAlpha (0.05f));
        g.fillPath (fill);
        Hw::strokeGlow (g, curve, Colours::screenText.withAlpha (0.9f), 1.4f);
    }

    g.restoreState();

    // --- nodes ---
    for (int b = 0; b < room::tone::numEqBands; ++b)
    {
        const auto pos = nodePosition (b);
        const auto active = std::abs (bands[b].gainDb) > 0.05f;
        const auto highlighted = b == hoverBand || b == dragBand;
        const auto rad = kNodeRadius + (highlighted ? 1.5f : 0.0f);
        const auto col = highlighted ? Colours::crimson.brighter (0.2f) : Colours::screenText;
        if (highlighted)
        {
            g.setColour (Colours::crimson.withAlpha (0.3f));
            g.fillEllipse (juce::Rectangle<float> (rad * 4, rad * 4).withCentre (pos));
        }
        g.setColour (col.withAlpha (active || highlighted ? 1.0f : 0.55f));
        g.fillEllipse (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (pos));
        g.setColour (Colours::glass);
        g.drawEllipse (juce::Rectangle<float> (rad * 2, rad * 2).withCentre (pos), 1.2f);
    }

    // --- printed numbers ---
    g.setColour (Colours::screenDim);
    g.setFont (Fonts::mono (10.0f));
    for (auto [f, t] : { std::pair { 20.0f, "20" }, { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" }, { 20000.0f, "20k" } })
        g.drawText (t, juce::Rectangle<float> (juce::jlimit (r.getX(), r.getRight() - 30.0f, xForFreq (f) - 15.0f), r.getBottom() + 3.0f, 30.0f, 14.0f),
                    juce::Justification::centred);
    for (float db : { -12.0f, 0.0f, 12.0f })
        g.drawText ((db > 0 ? "+" : "") + juce::String ((int) db),
                    juce::Rectangle<float> (glass.getX() + 6.0f, yForEqDb (db) - 7.0f, 28.0f, 14.0f),
                    juce::Justification::centredLeft);
    g.setColour (Colours::screenText);
    g.setFont (Fonts::mono (12.0f));
    g.drawText ("eq", juce::Rectangle<float> (glass.getX() + 9.0f, glass.getY() + 4.0f, 40.0f, 16.0f), juce::Justification::centredLeft);

    // --- readout for the band under the mouse ---
    const auto shown = dragBand >= 0 ? dragBand : hoverBand;
    if (shown >= 0)
    {
        const auto& b = bands[shown];
        juce::String s;
        s << "band " << (shown + 1) << "   "
          << (b.freqHz >= 1000.0f ? juce::String (b.freqHz / 1000.0f, 2) + " kHz" : juce::String (juce::roundToInt (b.freqHz)) + " Hz")
          << "   " << (b.gainDb > 0 ? "+" : "") << juce::String (b.gainDb, 1) << " dB";
        if (b.type == room::EqBand::Type::peak)
            s << "   Q " << juce::String (b.q, 2);
        g.setColour (Colours::screenText);
        g.setFont (Fonts::mono (11.0f));
        g.drawText (s, juce::Rectangle<float> (r.getX() + 30.0f, glass.getY() + 4.0f, r.getWidth() - 30.0f, 16.0f), juce::Justification::centredRight);
    }
    else
    {
        g.setColour (Colours::screenDim.withAlpha (0.8f));
        g.setFont (Fonts::mono (10.0f));
        g.drawText ("drag bands  (scroll = width, double-click = reset)",
                    juce::Rectangle<float> (r.getX() + 30.0f, glass.getY() + 4.0f, r.getWidth() - 30.0f, 16.0f), juce::Justification::centredRight);
    }
    Hw::drawScreenOverlay (g, glass);
}
