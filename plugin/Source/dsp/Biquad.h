#pragma once

#include <cmath>
#include <complex>

// Small, dependency-free biquad (2nd-order filter) helpers.
// Used by the analysis (band splitting), the IR generator (band noise),
// and the real-time tone path (EQ + resonators).

namespace room
{

constexpr double kPi = 3.14159265358979323846;

/** Normalised biquad coefficients (a0 == 1). */
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    /** Complex frequency response at frequency f (Hz). */
    std::complex<double> response (double f, double sampleRate) const
    {
        const auto w = 2.0 * kPi * f / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w);
        const auto z2 = z1 * z1;
        return (b0 + b1 * z1 + b2 * z2) / (1.0 + a1 * z1 + a2 * z2);
    }

    double magnitudeDb (double f, double sampleRate) const
    {
        return 20.0 * std::log10 (std::abs (response (f, sampleRate)) + 1e-20);
    }
};

/** One biquad with its own state (transposed direct form II). */
struct Biquad
{
    BiquadCoeffs c;
    double z1 = 0.0, z2 = 0.0;

    void reset() { z1 = z2 = 0.0; }

    inline double process (double x)
    {
        const auto y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return y;
    }
};

namespace design
{
    inline BiquadCoeffs normalise (double b0, double b1, double b2, double a0, double a1, double a2)
    {
        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    /** 2 pi f / sr, with f kept safely below Nyquist. A filter asked for a
        frequency at or above sr / 2 (e.g. a 16 kHz band at 22.05 kHz) would
        otherwise come out unstable and fill the output with NaN. */
    inline double angle (double f, double sr)
    {
        return 2.0 * kPi * std::fmin (std::fmax (f, 1.0), 0.49 * sr) / sr;
    }

    // RBJ "Audio EQ Cookbook" designs ------------------------------------------

    inline BiquadCoeffs lowpass (double f, double q, double sr)
    {
        const auto w = angle (f, sr), cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise ((1 - cw) / 2, 1 - cw, (1 - cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }

    inline BiquadCoeffs highpass (double f, double q, double sr)
    {
        const auto w = angle (f, sr), cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise ((1 + cw) / 2, -(1 + cw), (1 + cw) / 2, 1 + alpha, -2 * cw, 1 - alpha);
    }

    inline BiquadCoeffs peaking (double f, double q, double gainDb, double sr)
    {
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto w = angle (f, sr), cw = std::cos (w), alpha = std::sin (w) / (2.0 * q);
        return normalise (1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
    }

    inline BiquadCoeffs lowShelf (double f, double gainDb, double sr)
    {
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto w = angle (f, sr), cw = std::cos (w);
        const auto alpha = std::sin (w) / 2.0 * std::sqrt (2.0); // shelf slope S = 1
        const auto sq = 2.0 * std::sqrt (A) * alpha;
        return normalise (A * ((A + 1) - (A - 1) * cw + sq),
                          2 * A * ((A - 1) - (A + 1) * cw),
                          A * ((A + 1) - (A - 1) * cw - sq),
                          (A + 1) + (A - 1) * cw + sq,
                          -2 * ((A - 1) + (A + 1) * cw),
                          (A + 1) + (A - 1) * cw - sq);
    }

    inline BiquadCoeffs highShelf (double f, double gainDb, double sr)
    {
        const auto A = std::pow (10.0, gainDb / 40.0);
        const auto w = angle (f, sr), cw = std::cos (w);
        const auto alpha = std::sin (w) / 2.0 * std::sqrt (2.0);
        const auto sq = 2.0 * std::sqrt (A) * alpha;
        return normalise (A * ((A + 1) + (A - 1) * cw + sq),
                          -2 * A * ((A - 1) + (A + 1) * cw),
                          A * ((A + 1) + (A - 1) * cw - sq),
                          (A + 1) - (A - 1) * cw + sq,
                          2 * ((A - 1) - (A + 1) * cw),
                          (A + 1) - (A - 1) * cw - sq);
    }

    /** Resonant bandpass with 0 dB gain at its centre. Same design as
        scipy.signal.iirpeak, which the Python prototype used. */
    inline BiquadCoeffs resonator (double f, double q, double sr)
    {
        const auto w0 = angle (f, sr);
        const auto beta = std::tan ((w0 / q) / 2.0);
        const auto g = 1.0 / (1.0 + beta);
        return { 1.0 - g, 0.0, -(1.0 - g), -2.0 * g * std::cos (w0), 2.0 * g - 1.0 };
    }

    // 4th-order Butterworth = two 2nd-order sections with these Qs.
    constexpr double kButter4Q[2] = { 0.54119610014619698, 1.3065629648763766 };
}

/** 4th-order Butterworth highpass + 4th-order lowpass = one analysis band
    (24 dB/octave on each side), as four biquads in series. */
struct BandFilter
{
    Biquad stages[4];

    void design (double lo, double hi, double sr)
    {
        hi = std::fmin (hi, 0.45 * sr);
        stages[0].c = design::highpass (lo, design::kButter4Q[0], sr);
        stages[1].c = design::highpass (lo, design::kButter4Q[1], sr);
        stages[2].c = design::lowpass (hi, design::kButter4Q[0], sr);
        stages[3].c = design::lowpass (hi, design::kButter4Q[1], sr);
    }

    void reset() { for (auto& s : stages) s.reset(); }

    inline double process (double x)
    {
        for (auto& s : stages)
            x = s.process (x);
        return x;
    }
};

} // namespace room
