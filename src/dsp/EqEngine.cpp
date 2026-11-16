#include "EqEngine.h"

using namespace wsho::dsp;

// Designs linear-phase kernels off the audio thread. It polls the request
// queue instead of being woken, so the audio thread never makes a system call.
struct EqEngine::DesignThread final : public juce::Thread
{
    explicit DesignThread (EqEngine& e) : juce::Thread ("whysho EQ1 kernel design"), engine (e) {}

    void run() override
    {
        while (! threadShouldExit())
        {
            engine.runQueuedDesigns();
            wait (engine.linearWanted.load() ? 15 : 100);
        }
    }

    EqEngine& engine;
};

EqEngine::EqEngine() = default;

EqEngine::~EqEngine()
{
    if (designThread != nullptr) designThread->stopThread (10000);
}

static int osFactorToLog2 (int f) { return juce::jlimit (0, 4, f); }

void EqEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    // The design thread must not be working while its buffers are rebuilt.
    const juce::ScopedLock sl (designLock);

    baseSampleRate = sampleRate;
    maxBlock = maxBlockSize;
    numCh = juce::jmax (1, numChannels);

    dryBuffer.setSize (2, maxBlockSize);
    sideStereo.setSize (2, maxBlockSize);
    monoScratch.setSize (1, maxBlockSize);

    for (auto& b : bands) b.prepare (sampleRate);

    inGain.reset (sampleRate, 0.02);
    outGain.reset (sampleRate, 0.02);
    mixSmooth.reset (sampleRate, 0.02);
    autoGainComp.reset (sampleRate, 0.1);
    autoGainComp.setCurrentAndTargetValue (1.0f);

    // Every oversampler a setting can ask for, so that switching never allocates.
    // At 44.1 / 48 kHz 2x still squeezes the top octave by about 1 dB, so
    // Natural starts at 4x there.
    naturalMinLog2 = sampleRate < 50000.0 ? 2 : 1;
    int maxLatency = 0;
    for (int k = 1; k <= 4; ++k)
    {
        const auto make = [&] (Oversampler::FilterType type)
        {
            auto os = std::make_unique<Oversampler> (2, (size_t) k, type, true);
            os->initProcessing ((size_t) maxBlockSize);
            return os;
        };
        auto& iir = osIir[(size_t) (k - 1)];
        iir.main = make (Oversampler::filterHalfBandPolyphaseIIR);
        iir.side = make (Oversampler::filterHalfBandPolyphaseIIR);
        maxLatency = juce::jmax (maxLatency, juce::roundToInt (iir.main->getLatencyInSamples()));

        auto& fir = osFir[(size_t) (k - 1)];
        fir.main.reset(); fir.side.reset();
        if (k < naturalMinLog2) continue;
        fir.main = make (Oversampler::filterHalfBandFIREquiripple);
        fir.side = make (Oversampler::filterHalfBandFIREquiripple);
        // The FIR stages delay by a whole number of samples at their own rate,
        // which can be a fraction of a host sample. A few samples of extra delay
        // at the top rate make it whole without touching the phase.
        const float lat = fir.main->getLatencyInSamples();
        firLatency[(size_t) (k - 1)] = (int) std::ceil (lat - 1.0e-3f);
        firPad[(size_t) (k - 1)] = juce::roundToInt (((float) firLatency[(size_t) (k - 1)] - lat) * (float) (1 << k));
        maxLatency = juce::jmax (maxLatency, firLatency[(size_t) (k - 1)]);
    }

    lin.prepare (sampleRate, numCh == 1);
    maxLatency = juce::jmax (maxLatency, lin.getLatency());
    dryDelay.prepare (maxLatency);
    sideDelay.prepare (lin.getLatency());

    designFifo.reset();
    linDirty = true;
    designInline = true;
    routingDirty = true;
    reset();

    if (designThread == nullptr)
    {
        designThread = std::make_unique<DesignThread> (*this);
        designThread->startThread();
    }
}

void EqEngine::reset()
{
    for (auto& b : bands) b.reset();
    for (auto* set : { &osIir, &osFir })
        for (auto& os : *set)
        {
            if (os.main) os.main->reset();
            if (os.side) os.side->reset();
        }
    lin.reset();
    sideDelay.clear();
    for (auto& h : padHistory) h.fill (0.0f);
}

void EqEngine::snapSmoothers()
{
    inGain.setCurrentAndTargetValue (inGain.getTargetValue());
    outGain.setCurrentAndTargetValue (outGain.getTargetValue());
    mixSmooth.setCurrentAndTargetValue (mixSmooth.getTargetValue());
}

// Does anything that shapes the EQ curve differ? (Decides whether the
// linear-phase FIR must be redesigned.) Gain scale and solo are already folded
// into the bands; bands that are off don't matter.
static bool sameCurve (const BandArray& a, const BandArray& b)
{
    for (size_t i = 0; i < a.size(); ++i)
    {
        const auto& x = a[i]; const auto& y = b[i];
        if (x.on != y.on) return false;
        if (! x.on) continue;
        if (x.type != y.type || ! juce::exactlyEqual (x.freq, y.freq) || ! juce::exactlyEqual (x.gainDb, y.gainDb) || ! juce::exactlyEqual (x.q, y.q)
            || x.order != y.order || x.chan != y.chan) return false;
    }
    return true;
}

void EqEngine::setParameters (const EngineParams& p)
{
    params = p;

    // ---- which rate the IIR bands run at, and through which resampler ----
    int osLog2 = 0;
    if (p.phase == PhaseMode::Natural)          osLog2 = juce::jmax (naturalMinLog2, osFactorToLog2 (p.osFactor));
    else if (p.phase == PhaseMode::ZeroLatency) osLog2 = osFactorToLog2 (p.osFactor);
    // Linear: the kernel and the dynamic bands are at the host rate.

    OsPair* os = osLog2 == 0 ? nullptr
                             : &(p.phase == PhaseMode::Natural ? osFir : osIir)[(size_t) (osLog2 - 1)];
    if (os != activeOs || p.phase != activePhase || routingDirty)
    {
        activeOs = os;
        activePhase = p.phase;
        routingDirty = false;
        if (os != nullptr) { os->main->reset(); os->side->reset(); }
        pad = p.phase == PhaseMode::Natural ? firPad[(size_t) (osLog2 - 1)] : 0;
        padPos = 0;
        for (auto& h : padHistory) h.fill (0.0f);
        sideDelay.clear();
        lin.reset();        // or the audio from the last time Linear was on would play out

        // The filters now run at another rate: re-prepare every band for it.
        workRate = baseSampleRate * (double) (1 << osLog2);
        for (auto& b : bands) b.prepare (workRate);
    }

    for (int i = 0; i < kNumBands; ++i)
    {
        BandParams bp = p.bands[(size_t) i];
        bp.gainDb *= p.gainScale;
        bp.on = p.anySolo ? (bp.on && bp.solo) : bp.on;
        bands[(size_t) i].updateCoefficients (bp);
    }

    // ---- linear phase: the non-dynamic bands live in the FIR kernel ----
    linearWanted.store (p.phase == PhaseMode::Linear);
    if (p.phase == PhaseMode::Linear)
    {
        DesignRequest req;
        for (size_t i = 0; i < req.bands.size(); ++i)
        {
            req.bands[i] = bands[i].params();
            req.bands[i].on = req.bands[i].on && ! req.bands[i].dyn;
        }
        if (linDirty || ! sameCurve (req.bands, linCurve))
        {
            linCurve = req.bands;
            linDirty = false;
            req.id = ++designCounter;
            if (designInline || nonRealtime)
            {
                // Not a real-time thread here (see the header), so waiting for
                // the design thread to finish its current kernel is fine.
                const juce::ScopedLock sl (designLock);
                lin.design (req.bands, designInline);
                designedId = req.id;
            }
            else if (! queueDesign (req))
                linDirty = true;        // queue full: try again next block
        }
    }
    designInline = false;

    inGain.setTargetValue (juce::Decibels::decibelsToGain (p.inGainDb));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (p.outGainDb));
    // Bypass is "all dry": the EQ keeps running, so the latency stays the same
    // and switching it is a 20 ms crossfade instead of a jump.
    mixSmooth.setTargetValue (p.bypass ? 0.0f : p.mix);

    // Latency bookkeeping.
    int lat = 0;
    if (p.phase == PhaseMode::Linear)       lat = lin.getLatency();
    else if (p.phase == PhaseMode::Natural) lat = firLatency[(size_t) (osLog2 - 1)];
    else if (os != nullptr)                 lat = juce::roundToInt (os->main->getLatencyInSamples());
    latency.store (lat, std::memory_order_relaxed);
    tailSeconds.store (p.phase == PhaseMode::Linear ? (float) (lin.getKernelLength() / baseSampleRate) : 0.0f,
                       std::memory_order_relaxed);
}

bool EqEngine::queueDesign (const DesignRequest& req) noexcept
{
    const auto scope = designFifo.write (1);
    if (scope.blockSize1 + scope.blockSize2 == 0) return false;
    designQueue[(size_t) (scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2)] = req;
    return true;
}

void EqEngine::runQueuedDesigns()
{
    const juce::ScopedLock sl (designLock);

    // Only the newest queued curve matters.
    DesignRequest latest;
    bool have = false;
    {
        const auto scope = designFifo.read (designFifo.getNumReady());
        scope.forEach ([&] (int i) { latest = designQueue[(size_t) i]; have = true; });
    }
    // Skip anything older than a kernel that was designed inline meanwhile.
    if (have && (juce::int32) (latest.id - designedId) > 0)
    {
        lin.design (latest.bands, false);
        designedId = latest.id;
    }
}

void EqEngine::process (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>& side)
{
    // Hosts may hand over more samples than they announced in prepare(). All
    // scratch buffers are sized for maxBlock, so feed larger blocks in pieces.
    const int total = main.getNumSamples();
    const bool haveSide = side.getNumChannels() > 0 && side.getNumSamples() >= total;
    for (int pos = 0; pos < total; pos += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - pos);
        juce::AudioBuffer<float> m (main.getArrayOfWritePointers(), main.getNumChannels(), pos, n);
        if (haveSide)
        {
            juce::AudioBuffer<float> sc (const_cast<float* const*> (side.getArrayOfReadPointers()),
                                         side.getNumChannels(), pos, n);
            processChunk (m, sc);
        }
        else processChunk (m, {});
    }
}

void EqEngine::processChunk (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>& side)
{
    const int n = main.getNumSamples();
    const int ch = juce::jmin (2, main.getNumChannels());
    if (ch == 0 || n == 0) return;

    // Mono-safety: operate on 2 channels internally.
    float* L = main.getWritePointer (0);
    float* R = ch > 1 ? main.getWritePointer (1) : L;

    // ---- input gain + dry capture ----
    for (int i = 0; i < n; ++i)
    {
        const float g = inGain.getNextValue();
        L[i] *= g; if (ch > 1) R[i] *= g;
    }
    // The dry signal, as late as the processed one: what mix, bypass and auto
    // gain compare against.
    dryDelay.process (L, R, dryBuffer.getWritePointer (0), dryBuffer.getWritePointer (1), n, getLatencySamples());

    const bool haveSide = side.getNumChannels() > 0 && side.getNumSamples() >= n;

    // ---- sidechain listen monitor ----
    if (params.scListen && haveSide && ! params.bypass)
    {
        for (int i = 0; i < n; ++i)
        {
            L[i] = side.getSample (0, i);
            if (ch > 1) R[i] = side.getSample (juce::jmin (1, side.getNumChannels() - 1), i);
        }
        for (int i = 0; i < n; ++i) { const float g = outGain.getNextValue(); L[i] *= g; if (ch > 1) R[i] *= g; }
        for (auto& a : dynAmount) a.store (0.0f, std::memory_order_relaxed);
        return;
    }

    if (params.phase == PhaseMode::Linear)
    {
        if (ch > 1) lin.process (L, R, n);
        else if (numCh == 1) lin.process (L, nullptr, n);
        else
        {
            // One channel into an engine prepared for stereo: feed it as L = R.
            float* r = monoScratch.getWritePointer (0);
            std::copy (L, L + n, r);
            lin.process (L, r, n);
        }

        // Dynamic bands can't be part of a fixed FIR: they follow it as biquads,
        // keyed from a sidechain that is as late as the audio now is.
        bool anyDynamic = false;
        for (auto& b : bands) anyDynamic = anyDynamic || (b.isActive() && b.params().dyn);
        if (anyDynamic && haveSide)
        {
            sideDelay.process (side.getReadPointer (0), side.getReadPointer (juce::jmin (1, side.getNumChannels() - 1)),
                               sideStereo.getWritePointer (0), sideStereo.getWritePointer (1), n, lin.getLatency());
            processIIR (main, juce::AudioBuffer<float> (sideStereo.getArrayOfWritePointers(), 2, n), true);
        }
        else if (anyDynamic) processIIR (main, {}, true);
    }
    else if (activeOs != nullptr)
    {
        // Only the channels that carry audio (a mono track is 1 channel; the
        // oversampler's second channel would otherwise recirculate stale data).
        juce::dsp::AudioBlock<float> mb (main.getArrayOfWritePointers(), (size_t) ch, (size_t) n);
        auto up = activeOs->main->processSamplesUp (mb);
        const int upN = (int) up.getNumSamples();

        float* upPtrs[2] = { up.getChannelPointer (0), ch > 1 ? up.getChannelPointer (1) : nullptr };
        juce::AudioBuffer<float> upMain (upPtrs, ch, upN);          // refers to, doesn't allocate

        if (haveSide)
        {
            for (int c = 0; c < 2; ++c)
                sideStereo.copyFrom (c, 0, side, juce::jmin (c, side.getNumChannels() - 1), 0, n);
            juce::dsp::AudioBlock<float> sb (sideStereo.getArrayOfWritePointers(), 2, (size_t) n);
            auto sUp = activeOs->side->processSamplesUp (sb);
            float* scPtrs[2] = { sUp.getChannelPointer (0), sUp.getChannelPointer (1) };
            processIIR (upMain, juce::AudioBuffer<float> (scPtrs, 2, upN), false);
        }
        else processIIR (upMain, {}, false);

        // Natural: round the resampler's delay up to a whole host sample.
        if (pad > 0)
        {
            int p = padPos;
            for (int c = 0; c < ch; ++c)
            {
                p = padPos;
                auto& hist = padHistory[(size_t) c];
                for (int i = 0; i < upN; ++i)
                {
                    std::swap (upPtrs[c][i], hist[(size_t) p]);
                    if (++p == pad) p = 0;
                }
            }
            padPos = p;
        }

        activeOs->main->processSamplesDown (mb);
    }
    else
    {
        const juce::AudioBuffer<float> noSide;
        processIIR (main, haveSide ? side : noSide, false);
    }

    // A filter that ever produced NaN/inf stays broken (it lives in the filter
    // state). Never pass that to the host: reset and output the dry signal.
    if (! std::isfinite (L[n - 1]) || ! std::isfinite (R[n - 1]))
    {
        reset();
        for (int c = 0; c < ch; ++c) main.copyFrom (c, 0, dryBuffer, c, 0, n);
    }

    // ---- auto gain compensation ----
    if (params.autoGain)
    {
        double dryPow = 0.0, wetPow = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const float d = dryBuffer.getSample (0, i);
            dryPow += d * (double) d;
            wetPow += L[i] * (double) L[i];
        }
        const float comp = (wetPow > 1.0e-9) ? (float) std::sqrt (juce::jlimit (0.0, 16.0, dryPow / wetPow)) : 1.0f;
        autoGainComp.setTargetValue (comp);
    }
    else autoGainComp.setTargetValue (1.0f);

    // ---- output gain + dry/wet mix ----
    for (int i = 0; i < n; ++i)
    {
        const float ag = autoGainComp.getNextValue();
        const float og = outGain.getNextValue();
        const float m  = mixSmooth.getNextValue();
        const float wl = L[i] * ag, wr = (ch > 1 ? R[i] : L[i]) * ag;
        L[i] = (wl * m + dryBuffer.getSample (0, i) * (1.0f - m)) * og;
        if (ch > 1) R[i] = (wr * m + dryBuffer.getSample (1, i) * (1.0f - m)) * og;
    }

    // ---- dynamics read-out for the GUI ----
    for (size_t i = 0; i < bands.size(); ++i)
        dynAmount[i].store (params.bypass ? 0.0f : bands[i].dynamicAmount(), std::memory_order_relaxed);
}

void EqEngine::processIIR (juce::AudioBuffer<float>& buf, const juce::AudioBuffer<float>& sideBuf, bool dynamicBandsOnly)
{
    const int n = buf.getNumSamples();
    float* L = buf.getWritePointer (0);
    float* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : L;
    const bool haveSide = sideBuf.getNumChannels() > 0 && sideBuf.getNumSamples() >= n;
    const float* scL = haveSide ? sideBuf.getReadPointer (0) : nullptr;
    const float* scR = haveSide ? sideBuf.getReadPointer (juce::jmin (1, sideBuf.getNumChannels()-1)) : nullptr;

    for (auto& b : bands)
        if (b.isActive() && (! dynamicBandsOnly || b.params().dyn))
            b.process (L, R, n, scL, scR);
}

float EqEngine::getMagnitudeDb (float freqHz) const
{
    const double w = 2.0 * M_PI * juce::jlimit (1.0, baseSampleRate * 0.5 - 1.0, (double) freqHz) / baseSampleRate;
    double mag = 1.0;
    for (auto& b : bands)
        if (b.isActive())
            mag *= std::abs (b.response (w));
    return (float) juce::Decibels::gainToDecibels (juce::jmax (1.0e-6, mag));
}
