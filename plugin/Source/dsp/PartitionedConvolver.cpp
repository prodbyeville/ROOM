#include "PartitionedConvolver.h"

namespace room
{

namespace
{
    constexpr int maxChannels = 2;

    int orderFor (int fftSize)
    {
        int order = 0;
        while ((1 << order) < fftSize)
            ++order;
        return order;
    }

    /** What a forward + inverse round trip multiplies a signal by, for this
        FFT size. JUCE's engines differ (some scale, some don't), so measure it. */
    float roundTripGain (juce::dsp::FFT& fft)
    {
        const auto n = fft.getSize();
        std::vector<float> buf ((size_t) n * 2, 0.0f);
        buf[0] = 1.0f;
        fft.performRealOnlyForwardTransform (buf.data(), true);
        fft.performRealOnlyInverseTransform (buf.data());
        return std::abs (buf[0]) > 1.0e-9f ? buf[0] : 1.0f;
    }

    /** acc += a * b over `bins` complex values (interleaved re, im). */
    void multiplyAdd (float* acc, const float* a, const float* b, int bins) noexcept
    {
        for (int i = 0; i < bins; ++i)
        {
            const auto ar = a[2 * i], ai = a[2 * i + 1], br = b[2 * i], bi = b[2 * i + 1];
            acc[2 * i]     += ar * br - ai * bi;
            acc[2 * i + 1] += ar * bi + ai * br;
        }
    }
}

std::atomic<int> PartitionedConvolver::audioThreadWaits { 0 };

//==============================================================================

struct PartitionedConvolver::Kernel
{
    int numChannels = 0;
    int numParts[numStages] {};
    std::vector<float> spectra[numStages][maxChannels];   // numParts × 2 (P + 1) floats

    const float* part (int stage, int channel, int index) const noexcept
    {
        return spectra[stage][channel].data() + (size_t) index * 2 * (size_t) (partitionSize[stage] + 1);
    }
};

std::shared_ptr<const PartitionedConvolver::Kernel> PartitionedConvolver::makeKernel (const juce::AudioBuffer<float>& ir, int numChannels)
{
    auto k = std::make_shared<Kernel>();
    k->numChannels = juce::jlimit (1, maxChannels, numChannels);
    const auto len = ir.getNumSamples();
    for (int s = 0; s < numStages; ++s)
    {
        const auto p = partitionSize[s];
        const auto start = stageStart[s];
        const auto end = s + 1 < numStages ? std::min (len, stageStart[s + 1]) : len;
        const auto parts = end > start ? (end - start + p - 1) / p : 0;
        k->numParts[s] = parts;
        if (parts == 0)
            continue;

        juce::dsp::FFT fft (orderFor (2 * p));
        const auto norm = 1.0f / roundTripGain (fft);
        std::vector<float> buf ((size_t) 4 * (size_t) p);
        for (int c = 0; c < k->numChannels; ++c)
        {
            const auto* src = ir.getReadPointer (std::min (c, ir.getNumChannels() - 1));
            auto& dst = k->spectra[s][c];
            dst.assign ((size_t) parts * 2 * (size_t) (p + 1), 0.0f);
            for (int j = 0; j < parts; ++j)
            {
                std::fill (buf.begin(), buf.end(), 0.0f);
                const auto from = start + j * p;
                const auto count = std::min (p, end - from);
                for (int i = 0; i < count; ++i)
                    buf[(size_t) i] = src[from + i] * norm;
                fft.performRealOnlyForwardTransform (buf.data(), true);
                std::copy (buf.begin(), buf.begin() + 2 * (p + 1), dst.begin() + (std::ptrdiff_t) j * 2 * (p + 1));
            }
        }
    }
    return k;
}

//==============================================================================

struct PartitionedConvolver::Impl : private juce::Thread
{
    Impl() : juce::Thread ("ROOM convolution") {}
    ~Impl() override { shutdown(); }

    struct Stage
    {
        int p = 0, start = 0, maxParts = 0, bins = 0;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> inRing[maxChannels];    // 4 blocks of input (background stages)
        std::vector<float> fdl[maxChannels];       // input spectra, newest at fdlIndex(block)
        std::vector<float> outRing[maxChannels];   // background stages: output by absolute time
        int outMask = 0;
        std::vector<float> spec, accNew, accOld, timeNew, timeOld;

        // Which IR each block uses (background stages; written by the audio thread)
        juce::SpinLock kernelLock;
        const Kernel* kOld = nullptr;
        const Kernel* kNew = nullptr;
        juce::int64 fadeBlock = std::numeric_limits<juce::int64>::min();

        std::atomic<juce::int64> requested { -1 }, done { -1 };

        float* slot (int ch, juce::int64 block) noexcept
        {
            return fdl[ch].data() + (size_t) (block % maxParts) * 2 * (size_t) bins;
        }
    };

    Stage stages[numStages];
    int channels = 1;
    juce::int64 inputCount = 0;   // samples consumed so far (= output time, zero latency)

    // Head stage (audio thread): the current and previous input block, and
    // the contribution of all earlier blocks to the current one.
    std::vector<float> headCur[maxChannels], headPrev[maxChannels];
    std::vector<float> headAccNew[maxChannels], headAccOld[maxChannels];
    const Kernel* headOld = nullptr;
    const Kernel* headNew = nullptr;
    bool headFading = false;   // the current head block crossfades old -> new
    juce::int64 headFadeBlock = std::numeric_limits<juce::int64>::min();

    // Kernels: owned here (audio thread); freed by the worker via the graveyard.
    std::shared_ptr<const Kernel> current, previous, pending;
    bool hasPending = false, fadeActive = false;
    static constexpr int graveSlots = 16;
    std::shared_ptr<const Kernel> grave[graveSlots];
    std::atomic<bool> graveFull[graveSlots] {};

    juce::WaitableEvent work, blockDone;
    juce::CriticalSection workerLock;
    float headNorm = 1.0f, tailNorm[numStages] { 1.0f, 1.0f, 1.0f };

    //==========================================================================

    void prepare (double sr, int numChannels, double maxIrSeconds)
    {
        shutdown();
        channels = juce::jlimit (1, maxChannels, numChannels);
        const auto maxLen = (int) (maxIrSeconds * sr);
        for (int s = 0; s < numStages; ++s)
        {
            auto& st = stages[s];
            st.p = partitionSize[s];
            st.start = stageStart[s];
            st.bins = st.p + 1;
            const auto end = s + 1 < numStages ? stageStart[s + 1] : std::max (maxLen, stageStart[s] + st.p);
            st.maxParts = (end - st.start + st.p - 1) / st.p + 1;
            st.fft = std::make_unique<juce::dsp::FFT> (orderFor (2 * st.p));
            int ring = 1;
            while (ring < 2 * (st.start + st.p) + st.p)
                ring <<= 1;
            st.outMask = ring - 1;
            for (int c = 0; c < maxChannels; ++c)
            {
                st.inRing[c].assign ((size_t) 4 * (size_t) st.p, 0.0f);
                st.fdl[c].assign ((size_t) st.maxParts * 2 * (size_t) st.bins, 0.0f);
                st.outRing[c].assign (s == 0 ? 0 : (size_t) ring, 0.0f);
            }
            st.spec.assign ((size_t) 4 * (size_t) st.p, 0.0f);
            st.accNew.assign ((size_t) 4 * (size_t) st.p, 0.0f);
            st.accOld.assign ((size_t) 4 * (size_t) st.p, 0.0f);
            st.timeNew.assign ((size_t) 4 * (size_t) st.p, 0.0f);
            st.timeOld.assign ((size_t) 4 * (size_t) st.p, 0.0f);
        }
        const auto p0 = partitionSize[0];
        for (int c = 0; c < maxChannels; ++c)
        {
            headCur[c].assign ((size_t) p0, 0.0f);
            headPrev[c].assign ((size_t) p0, 0.0f);
            headAccNew[c].assign ((size_t) 4 * (size_t) p0, 0.0f);
            headAccOld[c].assign ((size_t) 4 * (size_t) p0, 0.0f);
        }
        resetState();
        startThread (juce::Thread::Priority::high);
    }

    void shutdown()
    {
        signalThreadShouldExit();
        work.signal();
        stopThread (4000);
    }

    void resetState()
    {
        const juce::ScopedLock sl (workerLock);
        for (auto& st : stages)
        {
            for (int c = 0; c < maxChannels; ++c)
            {
                std::fill (st.inRing[c].begin(), st.inRing[c].end(), 0.0f);
                std::fill (st.fdl[c].begin(), st.fdl[c].end(), 0.0f);
                std::fill (st.outRing[c].begin(), st.outRing[c].end(), 0.0f);
            }
            st.requested = -1;
            st.done = -1;
            const juce::SpinLock::ScopedLockType kl (st.kernelLock);
            st.kOld = st.kNew = current.get();
            st.fadeBlock = std::numeric_limits<juce::int64>::min();
        }
        for (int c = 0; c < maxChannels; ++c)
        {
            std::fill (headCur[c].begin(), headCur[c].end(), 0.0f);
            std::fill (headPrev[c].begin(), headPrev[c].end(), 0.0f);
            std::fill (headAccNew[c].begin(), headAccNew[c].end(), 0.0f);
            std::fill (headAccOld[c].begin(), headAccOld[c].end(), 0.0f);
        }
        inputCount = 0;
        headOld = headNew = current.get();
        headFading = false;
        headFadeBlock = std::numeric_limits<juce::int64>::min();
        fadeActive = false;
        if (previous != nullptr)
            retire (std::move (previous));
    }

    //==========================================================================
    // Kernel hand-over (audio thread)

    void retire (std::shared_ptr<const Kernel>&& k)
    {
        if (k == nullptr)
            return;
        for (int i = 0; i < graveSlots; ++i)
            if (! graveFull[i].load (std::memory_order_acquire))
            {
                grave[i] = std::move (k);
                graveFull[i].store (true, std::memory_order_release);
                work.signal();
                return;
            }
        // every slot taken (shouldn't happen): keep it alive rather than free here
        pending = std::move (k);
    }

    void setKernel (std::shared_ptr<const Kernel> k)
    {
        if (fadeActive)   // a crossfade is still running: this one goes next
        {
            if (pending != nullptr)
                retire (std::move (pending));
            pending = std::move (k);
            hasPending = true;
            return;
        }
        if (current == nullptr)   // nothing playing yet: take it as is (the engine fades the room in)
        {
            current = std::move (k);
            headOld = headNew = current.get();
            for (int s = 1; s < numStages; ++s)
            {
                const juce::SpinLock::ScopedLockType kl (stages[s].kernelLock);
                stages[s].kOld = stages[s].kNew = current.get();
            }
            return;
        }
        startFade (std::move (k));
    }

    void startFade (std::shared_ptr<const Kernel> k)
    {
        fadeActive = true;
        previous = std::move (current);
        current = std::move (k);
        const auto* o = previous.get();
        const auto* n = current.get();
        // head: the next block crossfades
        headOld = o;
        headNew = n;
        headFadeBlock = inputCount / partitionSize[0] + 1;
        for (int s = 1; s < numStages; ++s)
        {
            auto& st = stages[s];
            const juce::SpinLock::ScopedLockType kl (st.kernelLock);
            st.kOld = o;
            st.kNew = n;
            st.fadeBlock = st.requested.load() + 1;
        }
    }

    void finishFadeIfDone()
    {
        if (! fadeActive)
            return;
        if (inputCount / partitionSize[0] <= headFadeBlock)
            return;
        for (int s = 1; s < numStages; ++s)
            if (stages[s].done.load (std::memory_order_acquire) < stages[s].fadeBlock)
                return;
        for (int s = 1; s < numStages; ++s)
        {
            const juce::SpinLock::ScopedLockType kl (stages[s].kernelLock);
            stages[s].kOld = stages[s].kNew;
        }
        headOld = headNew;
        retire (std::move (previous));
        fadeActive = false;
        if (hasPending)
        {
            hasPending = false;
            startFade (std::move (pending));
        }
    }

    //==========================================================================
    // One block's output from the spectra history: sum over the kernel's partitions.

    void accumulateStage (int s, int ch, juce::int64 block, const Kernel* k, int firstPart, float* acc)
    {
        auto& st = stages[s];
        std::fill (acc, acc + 2 * st.bins, 0.0f);
        if (k == nullptr)
            return;
        const auto kc = std::min (ch, k->numChannels - 1);
        const auto parts = std::min (k->numParts[s], st.maxParts);
        for (int j = firstPart; j < parts; ++j)
        {
            const auto b = block - j;
            if (b < 0)
                break;
            multiplyAdd (acc, st.slot (ch, b), k->part (s, kc, j), st.bins);
        }
    }

    //==========================================================================
    // Background stages (worker thread)

    void run() override
    {
        while (! threadShouldExit())
        {
            work.wait (2);
            for (int i = 0; i < graveSlots; ++i)
                if (graveFull[i].load (std::memory_order_acquire))
                {
                    grave[i].reset();
                    graveFull[i].store (false, std::memory_order_release);
                }
            bool did = true;
            while (did && ! threadShouldExit())
            {
                did = false;
                for (int s = 1; s < numStages; ++s)
                {
                    auto& st = stages[s];
                    const auto next = st.done.load (std::memory_order_relaxed) + 1;
                    if (next <= st.requested.load (std::memory_order_acquire))
                    {
                        const juce::ScopedLock sl (workerLock);
                        processBlock (s, next);
                        st.done.store (next, std::memory_order_release);
                        blockDone.signal();
                        did = true;
                    }
                }
            }
        }
    }

    void processBlock (int s, juce::int64 block)
    {
        auto& st = stages[s];
        const auto p = st.p;
        const Kernel* kOld;
        const Kernel* kNew;
        juce::int64 fade;
        {
            const juce::SpinLock::ScopedLockType kl (st.kernelLock);
            kOld = st.kOld;
            kNew = st.kNew;
            fade = st.fadeBlock;
        }
        const auto* useMain = block < fade ? kOld : kNew;
        const bool crossfade = block == fade && kOld != kNew;

        for (int ch = 0; ch < channels; ++ch)
        {
            // [previous block, this block] -> spectrum, into the history
            auto* x = st.spec.data();
            std::fill (x, x + 4 * p, 0.0f);
            const auto prevOff = (size_t) (((block - 1) & 3) * p), curOff = (size_t) ((block & 3) * p);
            if (block > 0)
                std::copy (st.inRing[ch].begin() + (std::ptrdiff_t) prevOff, st.inRing[ch].begin() + (std::ptrdiff_t) prevOff + p, x);
            std::copy (st.inRing[ch].begin() + (std::ptrdiff_t) curOff, st.inRing[ch].begin() + (std::ptrdiff_t) curOff + p, x + p);
            st.fft->performRealOnlyForwardTransform (x, true);
            std::copy (x, x + 2 * st.bins, st.slot (ch, block));

            accumulateStage (s, ch, block, useMain, 0, st.accNew.data());
            std::fill (st.accNew.begin() + 2 * st.bins, st.accNew.end(), 0.0f);
            st.fft->performRealOnlyInverseTransform (st.accNew.data());
            if (crossfade)
            {
                accumulateStage (s, ch, block, kOld, 0, st.accOld.data());
                std::fill (st.accOld.begin() + 2 * st.bins, st.accOld.end(), 0.0f);
                st.fft->performRealOnlyInverseTransform (st.accOld.data());
            }

            // the valid half lands at absolute time block * p + start
            auto& ring = st.outRing[ch];
            const auto t0 = block * p + st.start;
            for (int i = 0; i < p; ++i)
            {
                auto v = st.accNew[(size_t) (p + i)];
                if (crossfade)
                {
                    const auto r = (float) (i + 1) / (float) p;
                    v = v * r + st.accOld[(size_t) (p + i)] * (1.0f - r);
                }
                ring[(size_t) ((t0 + i) & st.outMask)] = v;
            }
        }
    }

    //==========================================================================
    // Audio thread

    void process (float* const* io, int numCh, int n)
    {
        const auto ch = std::min (numCh, channels);
        const auto p0 = partitionSize[0];
        auto& head = stages[0];
        int pos = 0;
        while (pos < n)
        {
            const auto fill = (int) (inputCount % p0);
            const auto m = std::min (n - pos, p0 - fill);
            const auto block = inputCount / p0;

            // The block after a kernel switch crossfades old -> new.
            const bool fading = block == headFadeBlock && headOld != headNew;
            const auto* kMain = block < headFadeBlock ? headOld : headNew;

            for (int c = 0; c < ch; ++c)
            {
                auto* d = io[c] + pos;
                std::copy (d, d + m, headCur[c].begin() + fill);
                // feed the background stages
                for (int s = 1; s < numStages; ++s)
                {
                    auto& st = stages[s];
                    for (int i = 0; i < m; ++i)
                    {
                        const auto t = inputCount + i;
                        st.inRing[c][(size_t) ((t % (4 * st.p)))] = d[i];
                    }
                }

                // Head: [previous block, current block so far] against the first partition,
                // plus everything earlier (already summed in acc).
                auto* x = head.spec.data();
                std::fill (x, x + 4 * p0, 0.0f);
                std::copy (headPrev[c].begin(), headPrev[c].end(), x);
                std::copy (headCur[c].begin(), headCur[c].begin() + fill + m, x + p0);
                head.fft->performRealOnlyForwardTransform (x, true);

                auto mainOut = headPass (c, x, kMain, headAccNew[c].data(), head.timeNew.data());
                const float* oldOut = fading ? headPass (c, x, headOld, headAccOld[c].data(), head.timeOld.data()) : nullptr;

                for (int i = 0; i < m; ++i)
                {
                    auto v = mainOut[p0 + fill + i];
                    if (oldOut != nullptr)
                    {
                        const auto r = (float) (fill + i + 1) / (float) p0;
                        v = v * r + oldOut[p0 + fill + i] * (1.0f - r);
                    }
                    d[i] = v;
                }

                if (fill + m == p0)   // block complete: its spectrum joins the history
                    std::copy (x, x + 2 * head.bins, head.slot (c, block));
            }

            // background stages: post finished blocks, then add their output
            const auto end = inputCount + m;
            for (int s = 1; s < numStages; ++s)
            {
                auto& st = stages[s];
                const auto completed = end / st.p - 1;
                if (completed > st.requested.load (std::memory_order_relaxed))
                {
                    st.requested.store (completed, std::memory_order_release);
                    work.signal();
                }
                const auto lastT = end - 1;
                if (lastT < st.start)
                    continue;
                const auto needed = (lastT - st.start) / st.p;
                if (st.done.load (std::memory_order_acquire) < needed)
                    audioThreadWaits.fetch_add (1, std::memory_order_relaxed);
                while (st.done.load (std::memory_order_acquire) < needed && isThreadRunning())
                    blockDone.wait (5);   // only when rendering faster than real time
                for (int c = 0; c < ch; ++c)
                {
                    auto* d = io[c] + pos;
                    for (int i = 0; i < m; ++i)
                    {
                        const auto t = inputCount + i;
                        if (t >= st.start)
                            d[i] += st.outRing[c][(size_t) (t & st.outMask)];
                    }
                }
            }

            inputCount = end;
            if (inputCount % p0 == 0)   // next head block: shift, and sum the history for it
            {
                const auto nextBlock = inputCount / p0;
                const auto* kNext = nextBlock < headFadeBlock ? headOld : headNew;
                const bool nextFading = nextBlock == headFadeBlock && headOld != headNew;
                for (int c = 0; c < ch; ++c)
                {
                    std::swap (headPrev[c], headCur[c]);
                    std::fill (headCur[c].begin(), headCur[c].end(), 0.0f);
                    // partitions 1.. against the blocks before this one (partition 0 is added live)
                    accumulateStage (0, c, nextBlock, kNext, 1, headAccNew[c].data());
                    if (nextFading)
                        accumulateStage (0, c, nextBlock, headOld, 1, headAccOld[c].data());
                }
                finishFadeIfDone();
            }
            pos += m;
        }
        for (int c = ch; c < numCh; ++c)
            std::fill (io[c], io[c] + n, 0.0f);
    }

    /** Head output for the current block: x (this block's spectrum) against
        partition 0, plus acc (partitions 1.. against earlier blocks). Returns time samples. */
    const float* headPass (int c, const float* x, const Kernel* k, const float* acc, float* out)
    {
        auto& head = stages[0];
        const auto bins = head.bins;
        std::copy (acc, acc + 2 * bins, out);
        std::fill (out + 2 * bins, out + 4 * head.p, 0.0f);
        if (k != nullptr)
            multiplyAdd (out, x, k->part (0, std::min (c, k->numChannels - 1), 0), bins);
        head.fft->performRealOnlyInverseTransform (out);
        return out;
    }
};

//==============================================================================

PartitionedConvolver::PartitionedConvolver() : impl (std::make_unique<Impl>()) {}
PartitionedConvolver::~PartitionedConvolver() = default;

void PartitionedConvolver::prepare (double sr, int numChannels, double maxIrSeconds) { impl->prepare (sr, numChannels, maxIrSeconds); }
void PartitionedConvolver::reset() { impl->resetState(); }
void PartitionedConvolver::setKernel (std::shared_ptr<const Kernel> k) { impl->setKernel (std::move (k)); }
void PartitionedConvolver::process (float* const* channels, int numChannels, int numSamples) { impl->process (channels, numChannels, numSamples); }

} // namespace room
