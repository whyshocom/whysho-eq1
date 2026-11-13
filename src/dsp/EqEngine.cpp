#include "EqEngine.h"

using namespace wsho::dsp;

EqEngine::EqEngine() = default;

static int osFactorToLog2 (int f) { return juce::jlimit (0, 4, f); }

void EqEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
{
    baseSampleRate = sampleRate;
    maxBlock = maxBlockSize;
    numCh = juce::jmax (1, numChannels);

    dryBuffer.setSize (2, maxBlockSize);
    sideUp.setSize (2, maxBlockSize * 16);

    for (auto& b : bands) b.prepare (sampleRate);

    inGain.reset (sampleRate, 0.02);
    outGain.reset (sampleRate, 0.02);
    mixSmooth.reset (sampleRate, 0.02);
    autoGainComp.reset (sampleRate, 0.1);
    autoGainComp.setCurrentAndTargetValue (1.0f);

    // Linear-phase convolution engine.
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlockSize, (juce::uint32) juce::jmax (2, numChannels) };
    linConv.prepare (spec);
    linearDirty = true;

    currentOsFactor = -1; // force rebuild
    reset();
}

void EqEngine::reset()
{
    for (auto& b : bands) b.reset();
    if (osMainProc) osMainProc->reset();
    if (osSideProc) osSideProc->reset();
    linConv.reset();
}

void EqEngine::setParameters (const EngineParams& p)
{
    const bool osChanged = (p.osFactor != currentOsFactor);
    params = p;

    // Oversampling changes the rate the filters actually run at, so rebuild the
    // oversampling objects and re-prepare every band at the new working rate.
    if (osChanged)
    {
        currentOsFactor = p.osFactor;
        if (p.osFactor > 0)
        {
            const int log2 = osFactorToLog2 (p.osFactor);
            using OS = juce::dsp::Oversampling<float>;
            osMainProc = std::make_unique<OS> (2, log2, OS::filterHalfBandPolyphaseIIR);
            osSideProc = std::make_unique<OS> (2, log2, OS::filterHalfBandPolyphaseIIR);
            osMainProc->initProcessing ((size_t) maxBlock);
            osSideProc->initProcessing ((size_t) maxBlock);
        }
        else { osMainProc.reset(); osSideProc.reset(); }

        workRate = baseSampleRate * (p.osFactor > 0 ? (double) (1 << p.osFactor) : 1.0);
        for (auto& b : bands) b.prepare (workRate);
    }

    for (int i = 0; i < kNumBands; ++i)
    {
        BandParams bp = p.bands[(size_t) i];
        bp.gainDb *= p.gainScale;
        bp.on = p.anySolo ? (bp.on && bp.solo) : bp.on;
        bands[(size_t) i].updateCoefficients (bp);
    }

    inGain.setTargetValue (juce::Decibels::decibelsToGain (p.inGainDb));
    outGain.setTargetValue (juce::Decibels::decibelsToGain (p.outGainDb));
    mixSmooth.setTargetValue (p.mix);

    linearDirty = true;

    // Latency bookkeeping.
    int lat = 0;
    if (p.phase == PhaseMode::Linear) lat = 2048;              // firLen/2
    else if (p.osFactor > 0 && osMainProc) lat = (int) osMainProc->getLatencyInSamples();
    latency = lat;
}

void EqEngine::process (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>& side)
{
    const int n = main.getNumSamples();
    const int ch = juce::jmin (2, main.getNumChannels());
    if (ch == 0 || n == 0) return;

    // Mono-safety: operate on 2 channels internally.
    float* L = main.getWritePointer (0);
    float* R = ch > 1 ? main.getWritePointer (1) : L;

    // ---- input gain + dry capture ----
    dryBuffer.setSize (2, n, false, false, true);
    for (int i = 0; i < n; ++i)
    {
        const float g = inGain.getNextValue();
        L[i] *= g; if (ch > 1) R[i] *= g;
        dryBuffer.setSample (0, i, L[i]);
        dryBuffer.setSample (1, i, ch > 1 ? R[i] : L[i]);
    }

    if (params.bypass)
    {
        // leave as (input-gained) dry; still apply output gain for consistency
        for (int i = 0; i < n; ++i) { const float g = outGain.getNextValue(); L[i] *= g; if (ch > 1) R[i] *= g; }
        return;
    }

    const bool haveSide = side.getNumChannels() > 0 && side.getNumSamples() >= n;

    // ---- sidechain listen monitor ----
    if (params.scListen && haveSide)
    {
        for (int i = 0; i < n; ++i)
        {
            L[i] = side.getSample (0, i);
            if (ch > 1) R[i] = side.getSample (juce::jmin (1, side.getNumChannels() - 1), i);
        }
        for (int i = 0; i < n; ++i) { const float g = outGain.getNextValue(); L[i] *= g; if (ch > 1) R[i] *= g; }
        return;
    }

    if (params.phase == PhaseMode::Linear)
    {
        if (linearDirty) rebuildLinearPhase (baseSampleRate);
        juce::dsp::AudioBlock<float> block (main);
        juce::dsp::ProcessContextReplacing<float> ctx (block);
        linConv.process (ctx);
    }
    else if (params.osFactor > 0 && osMainProc)
    {
        juce::dsp::AudioBlock<float> mb (main);
        auto up = osMainProc->processSamplesUp (mb);

        // Upsample sidechain to match.
        juce::AudioBuffer<float> upSideBuf;
        if (haveSide)
        {
            juce::AudioBuffer<float> sbuf (2, n);
            for (int i = 0; i < n; ++i) { sbuf.setSample (0, i, side.getSample (0, i));
                sbuf.setSample (1, i, side.getSample (juce::jmin (1, side.getNumChannels()-1), i)); }
            juce::dsp::AudioBlock<float> sb (sbuf);
            auto sUp = osSideProc->processSamplesUp (sb);
            upSideBuf.setSize (2, (int) sUp.getNumSamples());
            for (int c = 0; c < 2; ++c) for (int i = 0; i < (int) sUp.getNumSamples(); ++i)
                upSideBuf.setSample (c, i, sUp.getSample (c, i));
        }

        juce::AudioBuffer<float> upMain (up.getNumChannels() >= 2 ? 2 : 1, (int) up.getNumSamples());
        for (int c = 0; c < upMain.getNumChannels(); ++c)
            juce::FloatVectorOperations::copy (upMain.getWritePointer (c), up.getChannelPointer ((size_t) c), (int) up.getNumSamples());

        processIIR (upMain, upSideBuf, baseSampleRate * (1 << params.osFactor));

        for (int c = 0; c < upMain.getNumChannels(); ++c)
            juce::FloatVectorOperations::copy (up.getChannelPointer ((size_t) c), upMain.getReadPointer (c), (int) up.getNumSamples());

        osMainProc->processSamplesDown (mb);
    }
    else
    {
        juce::AudioBuffer<float> sbuf;
        if (haveSide)
        {
            sbuf.setSize (2, n);
            for (int i = 0; i < n; ++i) { sbuf.setSample (0, i, side.getSample (0, i));
                sbuf.setSample (1, i, side.getSample (juce::jmin (1, side.getNumChannels()-1), i)); }
        }
        processIIR (main, sbuf, baseSampleRate);
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
}

void EqEngine::processIIR (juce::AudioBuffer<float>& buf, const juce::AudioBuffer<float>& sideBuf, double fsNow)
{
    juce::ignoreUnused (fsNow);
    const int n = buf.getNumSamples();
    float* L = buf.getWritePointer (0);
    float* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : L;
    const bool haveSide = sideBuf.getNumChannels() > 0 && sideBuf.getNumSamples() >= n;
    const float* scL = haveSide ? sideBuf.getReadPointer (0) : nullptr;
    const float* scR = haveSide ? sideBuf.getReadPointer (juce::jmin (1, sideBuf.getNumChannels()-1)) : nullptr;

    for (auto& b : bands)
        if (b.isActive())
            b.process (L, R, n, scL, scR);
}

void EqEngine::generateImpulseResponse (juce::AudioBuffer<float>& ir, double fsNow)
{
    const int firLen = 4096;
    ir.setSize (1, firLen, false, false, true);
    ir.clear();

    // Min-phase IR = impulse through the live IIR cascade (gain bands only, no dynamics).
    juce::AudioBuffer<float> work (2, firLen);
    work.clear();
    work.setSample (0, 0, 1.0f);
    work.setSample (1, 0, 1.0f);
    float* wl = work.getWritePointer (0);
    float* wr = work.getWritePointer (1);

    for (auto& b : bands)
    {
        if (! b.isActive()) continue;
        EqBand tmp = b;            // copy coeffs; fresh state
        tmp.reset();
        BandParams bp = b.params();
        bp.dyn = false;            // static response for linear phase
        tmp.updateCoefficients (bp);
        tmp.process (wl, wr, firLen, nullptr, nullptr);
    }

    // Convert min-phase magnitude to a linear-phase (symmetric) FIR via FFT.
    const int order = 12;          // 4096
    juce::dsp::FFT fft (order);
    std::vector<float> fd ((size_t) firLen * 2, 0.0f);
    for (int i = 0; i < firLen; ++i) fd[(size_t) i] = 0.5f * (wl[i] + wr[i]);
    fft.performRealOnlyForwardTransform (fd.data());

    // magnitude spectrum -> zero-phase, then shift to linear phase (center tap).
    std::vector<float> mag ((size_t) firLen * 2, 0.0f);
    for (int k = 0; k <= firLen / 2; ++k)
    {
        const float re = fd[(size_t) (2 * k)];
        const float im = fd[(size_t) (2 * k + 1)];
        const float m  = std::sqrt (re * re + im * im);
        const float phase = -2.0f * juce::MathConstants<float>::pi * (float) k * (firLen / 2) / (float) firLen;
        mag[(size_t) (2 * k)]     = m * std::cos (phase);
        mag[(size_t) (2 * k + 1)] = m * std::sin (phase);
    }
    fft.performRealOnlyInverseTransform (mag.data());

    // window (Hann) to tame ringing and copy out.
    for (int i = 0; i < firLen; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (firLen - 1));
        ir.setSample (0, i, mag[(size_t) i] * w);
    }
    juce::ignoreUnused (fsNow);
}

void EqEngine::rebuildLinearPhase (double fsNow)
{
    juce::AudioBuffer<float> ir;
    generateImpulseResponse (ir, fsNow);
    linConv.loadImpulseResponse (std::move (ir), fsNow,
                                 juce::dsp::Convolution::Stereo::no,
                                 juce::dsp::Convolution::Trim::no,
                                 juce::dsp::Convolution::Normalise::no);
    linearDirty = false;
    linActive = true;
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
