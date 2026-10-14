#include "RoomAnalysis.h"
#include "Biquad.h"

#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <numeric>

namespace room
{

namespace
{
    // All constants mirror room_engine/analysis.py so both versions agree.
    constexpr double kAnalysisSr = 48000.0;
    constexpr double kFMin = 60.0;
    constexpr double kFMax = 16000.0;
    constexpr float kMinT60 = 0.05f;
    constexpr double kMinDynamicRangeDb = 15.0;
    constexpr float kFallbackT60 = 0.4f;
    constexpr double kMinConfidentBandShare = 0.25;
    constexpr int kPointsPerOctave = 48;
    constexpr int kFftOrder = 12;          // 4096-point FFT
    constexpr double kActiveRangeDb = 40.0;
    constexpr double kMinPeakProminenceDb = 4.0;
    constexpr double kMaxAnalysisSeconds = 60.0;

    using Vec = std::vector<double>;

    double percentile (Vec v, double p)
    {
        if (v.empty())
            return 0.0;
        // Linear interpolation between closest ranks (numpy's default).
        std::sort (v.begin(), v.end());
        const auto pos = p / 100.0 * (double) (v.size() - 1);
        const auto lo = (size_t) std::floor (pos);
        const auto hi = std::min (lo + 1, v.size() - 1);
        return v[lo] + (v[hi] - v[lo]) * (pos - (double) lo);
    }

    double median (Vec v) { return percentile (std::move (v), 50.0); }

    //==========================================================================
    // Stage 1: multiband split
    //==========================================================================

    Vec bandEdges (int numBands)
    {
        Vec e ((size_t) numBands + 1);
        for (int i = 0; i <= numBands; ++i)
            e[(size_t) i] = kFMin * std::pow (kFMax / kFMin, (double) i / numBands);
        return e;
    }

    /** Zero-phase band filter (forward, then backward), like scipy's sosfiltfilt,
        so each band's envelope lines up with the original in time. */
    Vec filterBandZeroPhase (const Vec& x, double lo, double hi)
    {
        BandFilter f;
        f.design (lo, hi, kAnalysisSr);
        Vec y (x.size());
        for (size_t i = 0; i < x.size(); ++i)
            y[i] = f.process (x[i]);
        f.reset();
        for (size_t i = x.size(); i-- > 0;)
            y[i] = f.process (y[i]);
        return y;
    }

    //==========================================================================
    // Stage 2: per-band decay (T60)
    //==========================================================================

    /** Centred moving-average energy envelope in dB. */
    Vec smoothEnergyDb (const Vec& y, double windowS)
    {
        const auto n = std::max<size_t> (1, (size_t) (windowS * kAnalysisSr));
        Vec prefix (y.size() + 1, 0.0);
        for (size_t i = 0; i < y.size(); ++i)
            prefix[i + 1] = prefix[i] + y[i] * y[i];

        Vec out (y.size());
        const auto half = (n - 1) / 2;
        for (size_t i = 0; i < y.size(); ++i)
        {
            const auto start = i >= half ? i - half : 0;
            const auto end = std::min (y.size(), i + (n - half));
            out[i] = 10.0 * std::log10 ((prefix[end] - prefix[start]) / (double) n + 1e-20);
        }
        return out;
    }

    /** Schroeder backward integration + straight-line fit (-5..-25 dB). */
    std::pair<float, bool> measureT60 (const Vec& y, float maxT60)
    {
        const auto env = smoothEnergyDb (y, 0.02);
        const auto peakI = (size_t) std::distance (env.begin(), std::max_element (env.begin(), env.end()));

        Vec tail (env.begin() + (std::ptrdiff_t) peakI, env.end());
        if (tail.size() < (size_t) (0.05 * kAnalysisSr))
            return { kFallbackT60, false };

        const auto floorDb = percentile (tail, 10.0);
        if (env[peakI] - floorDb < kMinDynamicRangeDb)
            return { kFallbackT60, false };

        // Cut where the envelope first gets within 5 dB of the noise floor.
        size_t endI = env.size();
        for (size_t i = 0; i < tail.size(); ++i)
            if (tail[i] <= floorDb + 5.0) { endI = peakI + i; break; }

        const auto len = endI - peakI;
        if (len < (size_t) (0.01 * kAnalysisSr))
            return { kMinT60, true };

        // Energy remaining from each moment to the end.
        Vec edc (len);
        double acc = 0.0;
        for (size_t i = len; i-- > 0;)
        {
            acc += y[peakI + i] * y[peakI + i];
            edc[i] = acc;
        }
        double edcMin = 0.0;
        const auto edcStart = edc[0];
        for (auto& v : edc)
        {
            v = 10.0 * std::log10 (v / edcStart + 1e-20);
            edcMin = std::min (edcMin, v);
        }

        const double loDb = edcMin < -25.0 ? -25.0 : std::max (edcMin * 0.66, -25.0);
        const double hiDb = loDb < -10.0 ? -5.0 : -1.0;

        // Least-squares line through the selected part of the curve.
        double n = 0, st = 0, sv = 0, stt = 0, stv = 0;
        for (size_t i = 0; i < len; ++i)
        {
            if (edc[i] > hiDb || edc[i] < loDb)
                continue;
            const auto t = (double) i / kAnalysisSr;
            n += 1; st += t; sv += edc[i]; stt += t * t; stv += t * edc[i];
        }
        if (n < 4)
            return { kMinT60, true };

        const auto slope = (n * stv - st * sv) / (n * stt - st * st + 1e-30);
        if (slope >= 0.0)
            return { maxT60, true };
        return { juce::jlimit (kMinT60, maxT60, (float) (-60.0 / slope)), true };
    }

    //==========================================================================
    // Stage 3: averaged spectral shape
    //==========================================================================

    /** dB-averaged spectrum over all frames within 40 dB of the loudest one.
        Averaging in dB lets random noise wiggles cancel while real resonances
        (same place every frame) survive. Matches librosa.stft framing. */
    Vec averagedSpectrumDb (const Vec& x, Vec& freqsOut)
    {
        const int nFft = 1 << kFftOrder, hop = nFft / 4, half = nFft / 2;
        const int numBins = half + 1;

        // Zero-pad by half a frame on each side (librosa center=True,
        // pad_mode="constant", its current default).
        const auto n = (int) x.size();
        Vec padded ((size_t) (n + nFft), 0.0);
        std::copy (x.begin(), x.end(), padded.begin() + half);

        std::vector<float> window ((size_t) nFft);
        for (int i = 0; i < nFft; ++i) // periodic Hann
            window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / nFft));

        juce::dsp::FFT fft (kFftOrder);
        std::vector<float> buf ((size_t) nFft * 2);
        const int numFrames = 1 + n / hop;

        std::vector<Vec> framesDb;
        Vec frameEnergyDb;
        framesDb.reserve ((size_t) numFrames);

        for (int f = 0; f < numFrames; ++f)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < nFft; ++i)
            {
                const auto idx = (size_t) (f * hop + i);
                buf[(size_t) i] = idx < padded.size() ? (float) padded[idx] * window[(size_t) i] : 0.0f;
            }
            fft.performFrequencyOnlyForwardTransform (buf.data(), true);

            Vec db ((size_t) numBins);
            double energy = 0.0;
            for (int b = 0; b < numBins; ++b)
            {
                const auto p = (double) buf[(size_t) b] * buf[(size_t) b];
                energy += p;
                db[(size_t) b] = 10.0 * std::log10 (p + 1e-20);
            }
            framesDb.push_back (std::move (db));
            frameEnergyDb.push_back (10.0 * std::log10 (energy + 1e-20));
        }

        const auto maxEnergy = *std::max_element (frameEnergyDb.begin(), frameEnergyDb.end());
        Vec mean ((size_t) numBins, 0.0);
        int count = 0;
        for (size_t f = 0; f < framesDb.size(); ++f)
        {
            if (frameEnergyDb[f] < maxEnergy - kActiveRangeDb)
                continue;
            for (int b = 0; b < numBins; ++b)
                mean[(size_t) b] += framesDb[f][(size_t) b];
            ++count;
        }
        for (auto& v : mean)
            v /= std::max (1, count);

        freqsOut.resize ((size_t) numBins);
        for (int b = 0; b < numBins; ++b)
            freqsOut[(size_t) b] = b * kAnalysisSr / nFft;
        return mean;
    }

    double interp (double x, const Vec& xs, const Vec& ys)
    {
        if (x <= xs.front()) return ys.front();
        if (x >= xs.back()) return ys.back();
        const auto it = std::upper_bound (xs.begin(), xs.end(), x);
        const auto i = (size_t) std::distance (xs.begin(), it);
        const auto t = (x - xs[i - 1]) / (xs[i] - xs[i - 1]);
        return ys[i - 1] + t * (ys[i] - ys[i - 1]);
    }

    /** Log-spaced frequency grid, 48 points per octave. */
    Vec logGrid()
    {
        const auto nOct = std::log2 (kFMax / kFMin);
        const auto count = (int) (nOct * kPointsPerOctave) + 1;
        Vec g ((size_t) count);
        for (int i = 0; i < count; ++i)
            g[(size_t) i] = kFMin * std::pow (2.0, (double) i / kPointsPerOctave);
        return g;
    }

    /** Hann-weighted moving average over a fraction of an octave. */
    Vec smoothOctaves (const Vec& spec, double octaves)
    {
        const auto n = std::max (1, (int) std::lround (octaves * kPointsPerOctave));
        if (n <= 1)
            return spec;

        const auto len = (int) spec.size();
        Vec padded ((size_t) (len + 2 * n));
        for (int i = 0; i < len + 2 * n; ++i)
            padded[(size_t) i] = spec[(size_t) juce::jlimit (0, len - 1, i - n)];

        // numpy.hanning(n + 2)[1:-1], normalised.
        Vec k ((size_t) n);
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            k[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * (i + 1) / (n + 1));
            sum += k[(size_t) i];
        }
        for (auto& v : k) v /= sum;

        Vec out ((size_t) len);
        const auto offset = (n - 1) / 2;
        for (int j = 0; j < len; ++j)
        {
            double acc = 0.0;
            const auto centre = j + n + offset;
            for (int i = 0; i < n; ++i)
            {
                const auto idx = centre - i;
                if (idx >= 0 && idx < (int) padded.size())
                    acc += padded[(size_t) idx] * k[(size_t) i];
            }
            out[(size_t) j] = acc;
        }
        return out;
    }

    //==========================================================================
    // Stage 4: resonant peaks (a port of the parts of scipy.signal.find_peaks
    // and peak_widths the prototype used)
    //==========================================================================

    std::vector<Peak> findResonantPeaks (const Vec& specLog, const Vec& grid, int numPeaks)
    {
        const auto fine = smoothOctaves (specLog, 1.0 / 24.0);
        const auto baseline = smoothOctaves (specLog, 1.0);
        const auto n = (int) fine.size();
        Vec r ((size_t) n);
        for (int i = 0; i < n; ++i)
            r[(size_t) i] = fine[(size_t) i] - baseline[(size_t) i];

        // 1. local maxima (middle of flat tops)
        std::vector<int> cand;
        for (int i = 1; i < n - 1;)
        {
            if (r[(size_t) i - 1] < r[(size_t) i])
            {
                int ahead = i + 1;
                while (ahead < n - 1 && juce::exactlyEqual (r[(size_t) ahead], r[(size_t) i]))
                    ++ahead;
                if (r[(size_t) ahead] < r[(size_t) i])
                {
                    cand.push_back ((i + ahead - 1) / 2);
                    i = ahead;
                    continue;
                }
            }
            ++i;
        }

        // 2. keep peaks at least 1/6 octave apart, tallest first
        const int distance = kPointsPerOctave / 6;
        std::vector<int> order (cand.size());
        std::iota (order.begin(), order.end(), 0);
        std::sort (order.begin(), order.end(), [&] (int a, int b)
                   { return r[(size_t) cand[(size_t) a]] > r[(size_t) cand[(size_t) b]]; });
        std::vector<bool> keep (cand.size(), true);
        for (auto oi : order)
        {
            if (! keep[(size_t) oi])
                continue;
            for (size_t j = 0; j < cand.size(); ++j)
                if ((int) j != oi && keep[j] && std::abs (cand[j] - cand[(size_t) oi]) < distance)
                    keep[j] = false;
        }

        // 3. prominence + width at half prominence
        struct Found { int idx; double prom, width; };
        std::vector<Found> found;
        for (size_t c = 0; c < cand.size(); ++c)
        {
            if (! keep[c])
                continue;
            const auto p = cand[c];
            const auto h = r[(size_t) p];

            int leftBase = p, i = p;
            auto leftMin = h;
            while (i > 0 && r[(size_t) i - 1] <= h)
            {
                --i;
                if (r[(size_t) i] < leftMin) { leftMin = r[(size_t) i]; leftBase = i; }
            }
            int rightBase = p;
            i = p;
            auto rightMin = h;
            while (i < n - 1 && r[(size_t) i + 1] <= h)
            {
                ++i;
                if (r[(size_t) i] < rightMin) { rightMin = r[(size_t) i]; rightBase = i; }
            }

            const auto prom = h - std::max (leftMin, rightMin);
            if (prom < kMinPeakProminenceDb)
                continue;

            const auto ref = h - 0.5 * prom;
            double left = p, right = p;
            for (i = p; i > leftBase && r[(size_t) i] > ref;) --i;
            if (r[(size_t) i] < ref)
                left = i + (ref - r[(size_t) i]) / (r[(size_t) i + 1] - r[(size_t) i]);
            else
                left = i;
            for (i = p; i < rightBase && r[(size_t) i] > ref;) ++i;
            if (r[(size_t) i] < ref)
                right = i - (ref - r[(size_t) i]) / (r[(size_t) i - 1] - r[(size_t) i]);
            else
                right = i;

            found.push_back ({ p, prom, right - left });
        }

        // 4. keep the most prominent, return sorted by frequency
        std::sort (found.begin(), found.end(), [] (auto& a, auto& b) { return a.prom > b.prom; });
        if ((int) found.size() > numPeaks)
            found.resize ((size_t) numPeaks);
        std::sort (found.begin(), found.end(), [] (auto& a, auto& b) { return a.idx < b.idx; });

        std::vector<Peak> peaks;
        for (auto& f : found)
        {
            const auto bwOct = std::max (f.width / kPointsPerOctave, 1.0 / 48.0);
            const auto twoN = std::pow (2.0, bwOct);
            const auto q = std::sqrt (twoN) / (twoN - 1.0);
            peaks.push_back ({ (float) grid[(size_t) f.idx], (float) f.prom,
                               (float) juce::jlimit (0.5, 40.0, q) });
        }
        return peaks;
    }

    //==========================================================================
    // Stage 5: spatial-versus-tonal balance
    //==========================================================================

    double lateEnergyFraction (const Vec& x)
    {
        const auto env = smoothEnergyDb (x, 0.02);
        const auto peakI = (size_t) std::distance (env.begin(), std::max_element (env.begin(), env.end()));
        const auto split = peakI + (size_t) (0.05 * kAnalysisSr);
        double total = 1e-20, late = 0.0;
        for (size_t i = peakI; i < x.size(); ++i)
        {
            const auto e = x[i] * x[i];
            total += e;
            if (i >= split) late += e;
        }
        return late / total;
    }

    bool isSteadySound (const Vec& x)
    {
        const auto env = smoothEnergyDb (x, 0.05);
        return percentile (env, 99.0) - percentile (env, 10.0) < kMinDynamicRangeDb;
    }

    /** PROVISIONAL FORMULA, identical to balance_score() in the prototype. */
    void balanceScore (RoomAnalysis& a)
    {
        Vec good;
        for (int b = 0; b < a.numBands(); ++b)
            if (a.bandConfident[(size_t) b])
                good.push_back (a.bandT60s[(size_t) b]);

        double spatial, tonal = 0.0;
        if (! good.empty() && ! a.steady)
        {
            const auto med = median (good);
            double mean = 0.0, var = 0.0;
            for (auto t : good) mean += std::log (t);
            mean /= (double) good.size();
            for (auto t : good) var += (std::log (t) - mean) * (std::log (t) - mean);
            const auto spread = std::sqrt (var / (double) good.size());

            const auto magnitude = juce::jlimit (0.0, 1.5, med / 1.2);
            const auto uniformity = 1.0 - 0.6 * juce::jlimit (0.0, 1.0, spread);
            spatial = 0.7 * magnitude * uniformity + 0.3 * a.lateEnergyFraction;
            a.medianT60 = (float) med;
        }
        else
        {
            spatial = 0.3;
            a.medianT60 = kFallbackT60;
        }

        if (! a.peaks.empty())
        {
            double g = 0.0, q = 0.0;
            for (auto& p : a.peaks) { g += p.gainDb; q += p.q; }
            g /= (double) a.peaks.size();
            q /= (double) a.peaks.size();
            tonal = juce::jlimit (0.0, 1.5, g / 12.0) * juce::jlimit (0.2, 1.0, q / 8.0);
        }

        if (a.steady)
            tonal = std::max (tonal, spatial);

        a.spatialEvidence = (float) spatial;
        a.tonalEvidence = (float) tonal;
        a.balance = spatial + tonal < 1e-6 ? 0.5f
                                           : (float) juce::jlimit (0.0, 1.0, spatial / (spatial + tonal));
    }

    //==========================================================================

    /** Mix to mono and resample to the fixed analysis rate. */
    Vec prepareInput (const juce::AudioBuffer<float>& audio, double sampleRate)
    {
        const auto numIn = std::min (audio.getNumSamples(),
                                     (int) (kMaxAnalysisSeconds * sampleRate));
        std::vector<float> mono ((size_t) numIn, 0.0f);
        for (int c = 0; c < audio.getNumChannels(); ++c)
            for (int i = 0; i < numIn; ++i)
                mono[(size_t) i] += audio.getSample (c, i) / (float) audio.getNumChannels();

        if (std::abs (sampleRate - kAnalysisSr) < 1.0)
            return Vec (mono.begin(), mono.end());

        const auto ratio = sampleRate / kAnalysisSr;
        const auto numOut = (int) ((double) numIn / ratio);
        std::vector<float> out ((size_t) numOut, 0.0f);
        mono.resize (mono.size() + 64, 0.0f); // room for the interpolator to read ahead
        juce::WindowedSincInterpolator interp;
        interp.process (ratio, mono.data(), out.data(), numOut);
        return Vec (out.begin(), out.end());
    }
}

//==============================================================================

RoomAnalysis analyzeRoom (const juce::AudioBuffer<float>& audio, double sampleRate,
                          const AnalysisSettings& settings)
{
    RoomAnalysis a;
    const auto x = prepareInput (audio, sampleRate);
    if (x.size() < (size_t) (0.05 * kAnalysisSr))
        return a; // too short to analyse

    a.durationS = (double) x.size() / kAnalysisSr;
    const auto nb = settings.numBands;

    // 1 + 2. bands and their decays
    const auto edges = bandEdges (nb);
    for (int b = 0; b < nb; ++b)
    {
        const auto lo = edges[(size_t) b], hi = edges[(size_t) b + 1];
        const auto band = filterBandZeroPhase (x, lo, hi);
        const auto [t60, ok] = measureT60 (band, settings.maxT60);
        a.bandT60s.push_back (t60);
        a.bandConfident.push_back (ok ? 1 : 0);
        a.bandCentersHz.push_back ((float) std::sqrt (lo * hi));
    }
    for (auto e : edges)
        a.bandEdgesHz.push_back ((float) e);

    // Steady sounds (hiss, room tone) have no decay to measure.
    a.steady = isSteadySound (x);
    const auto confident = std::count (a.bandConfident.begin(), a.bandConfident.end(), 1);
    if (a.steady || (double) confident < kMinConfidentBandShare * nb)
    {
        a.steady = true;
        std::fill (a.bandT60s.begin(), a.bandT60s.end(), kFallbackT60);
        std::fill (a.bandConfident.begin(), a.bandConfident.end(), 0);
    }

    // 3. averaged spectrum on a log grid
    Vec freqs;
    const auto spec = averagedSpectrumDb (x, freqs);
    const auto grid = logGrid();
    Vec specLog (grid.size());
    for (size_t i = 0; i < grid.size(); ++i)
        specLog[i] = interp (grid[i], freqs, spec);

    // Tone curve: 1/6-octave smoothing, normalised to 0 dB, then 1-octave
    // smoothing (same two steps as the prototype's tone_curve()).
    auto smooth = smoothOctaves (specLog, 1.0 / 6.0);
    const auto mx = *std::max_element (smooth.begin(), smooth.end());
    for (auto& v : smooth) v -= mx;
    const auto curve = smoothOctaves (smooth, 1.0);
    for (size_t i = 0; i < grid.size(); ++i)
    {
        a.toneFreqsHz.push_back ((float) grid[i]);
        a.toneCurveDb.push_back ((float) curve[i]);
    }

    // 4. resonant peaks
    a.peaks = findResonantPeaks (specLog, grid, settings.numPeaks);

    // 5. balance
    a.lateEnergyFraction = (float) lateEnergyFraction (x);
    balanceScore (a);

    a.valid = true;
    return a;
}

//==============================================================================
// Saving / loading with the plugin state

namespace
{
    template <typename T>
    juce::String join (const std::vector<T>& v)
    {
        juce::StringArray s;
        for (auto x : v) s.add (juce::String ((double) x, 4));
        return s.joinIntoString (",");
    }

    template <typename T>
    std::vector<T> split (const juce::String& str)
    {
        std::vector<T> v;
        for (auto& s : juce::StringArray::fromTokens (str, ",", ""))
            if (s.isNotEmpty())
                v.push_back ((T) s.getDoubleValue());
        return v;
    }
}

juce::ValueTree RoomAnalysis::toValueTree() const
{
    juce::ValueTree t ("ROOM_ANALYSIS");
    t.setProperty ("valid", valid, nullptr);
    t.setProperty ("sourceName", sourceName, nullptr);
    if (origin.isNotEmpty())
        t.setProperty ("origin", origin, nullptr);
    t.setProperty ("durationS", durationS, nullptr);
    t.setProperty ("bandEdgesHz", join (bandEdgesHz), nullptr);
    t.setProperty ("bandCentersHz", join (bandCentersHz), nullptr);
    t.setProperty ("bandT60s", join (bandT60s), nullptr);
    t.setProperty ("bandConfident", join (bandConfident), nullptr);
    t.setProperty ("toneFreqsHz", join (toneFreqsHz), nullptr);
    t.setProperty ("toneCurveDb", join (toneCurveDb), nullptr);

    std::vector<float> pf, pg, pq;
    for (auto& p : peaks) { pf.push_back (p.freqHz); pg.push_back (p.gainDb); pq.push_back (p.q); }
    t.setProperty ("peakFreqs", join (pf), nullptr);
    t.setProperty ("peakGains", join (pg), nullptr);
    t.setProperty ("peakQs", join (pq), nullptr);

    t.setProperty ("lateEnergyFraction", lateEnergyFraction, nullptr);
    t.setProperty ("steady", steady, nullptr);
    t.setProperty ("balance", balance, nullptr);
    t.setProperty ("spatialEvidence", spatialEvidence, nullptr);
    t.setProperty ("tonalEvidence", tonalEvidence, nullptr);
    t.setProperty ("medianT60", medianT60, nullptr);
    return t;
}

RoomAnalysis RoomAnalysis::fromValueTree (const juce::ValueTree& t)
{
    RoomAnalysis a;
    if (! t.hasType ("ROOM_ANALYSIS"))
        return a;

    a.sourceName = t["sourceName"].toString();
    a.origin = t["origin"].toString();
    a.durationS = t["durationS"];
    a.bandEdgesHz = split<float> (t["bandEdgesHz"]);
    a.bandCentersHz = split<float> (t["bandCentersHz"]);
    a.bandT60s = split<float> (t["bandT60s"]);
    a.bandConfident = split<int> (t["bandConfident"]);
    a.toneFreqsHz = split<float> (t["toneFreqsHz"]);
    a.toneCurveDb = split<float> (t["toneCurveDb"]);

    const auto pf = split<float> (t["peakFreqs"]);
    const auto pg = split<float> (t["peakGains"]);
    const auto pq = split<float> (t["peakQs"]);
    for (size_t i = 0; i < pf.size() && i < pg.size() && i < pq.size(); ++i)
        a.peaks.push_back ({ pf[i], pg[i], pq[i] });

    a.lateEnergyFraction = t["lateEnergyFraction"];
    a.steady = t["steady"];
    a.balance = t["balance"];
    a.spatialEvidence = t["spatialEvidence"];
    a.tonalEvidence = t["tonalEvidence"];
    a.medianT60 = t["medianT60"];

    // Only trust it if the arrays are consistent.
    a.valid = (bool) t["valid"]
              && ! a.bandT60s.empty()
              && a.bandEdgesHz.size() == a.bandT60s.size() + 1
              && a.toneFreqsHz.size() == a.toneCurveDb.size();
    return a;
}

} // namespace room
