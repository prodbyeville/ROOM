#pragma once

#include "PluginProcessor.h"

// The EQ section's display:
//   - live spectrum of the plugin's output
//   - the combined Tape + EQ curve
//   - Auto EQ cuts, drawn in red as they happen
//   - 5 band nodes: drag to move frequency/gain, mouse wheel for width (Q),
//     double-click to reset the band

class EqPanel : public juce::Component,
                private juce::Timer
{
public:
    explicit EqPanel (RoomAudioProcessor&);
    ~EqPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    void updateSpectrum();

    juce::Rectangle<float> plotArea() const;
    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForEqDb (float db) const;
    float eqDbForY (float y) const;
    float yForSpectrumDb (float db) const;

    juce::Point<float> nodePosition (int band) const;
    int nodeAt (juce::Point<float>) const;

    juce::RangedAudioParameter* param (const juce::String& id) const;
    void setParam (const juce::String& id, float value);

    RoomAudioProcessor& proc;

    // Analyzer
    static constexpr int fftOrder = 12, fftSize = 1 << fftOrder;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> history = std::vector<float> ((size_t) fftSize, 0.0f);
    std::vector<float> fftBuf = std::vector<float> ((size_t) fftSize * 2, 0.0f);
    std::vector<float> pullBuf = std::vector<float> (8192, 0.0f);
    static constexpr int numSpectrumPoints = 256;
    std::array<float, numSpectrumPoints> spectrumDb {};

    int hoverBand = -1, dragBand = -1;

    static constexpr float eqRangeDb = 15.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqPanel)
};
