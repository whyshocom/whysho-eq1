#include "SpectrumAnalyzer.h"

namespace wsho::dsp
{
SpectrumAnalyzer::SpectrumAnalyzer() : ring ((size_t) ringSize, 0.0f)
{
    display.fill (floorDb);
}

void SpectrumAnalyzer::configure (int newOrder, double sampleRate)
{
    order = newOrder;
    size  = 1 << order;
    rate  = sampleRate;
    fft = std::make_unique<juce::dsp::FFT> (order);
    fftData.assign ((size_t) size * 2, 0.0f);
    binDb.assign ((size_t) size / 2, floorDb);
    cumulative.assign ((size_t) size / 2 + 1, 0.0);

    window.resize ((size_t) size);   // periodic Hann, coherent gain 0.5
    for (int n = 0; n < size; ++n)
        window[(size_t) n] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) n / (float) size);

    clear();
}

void SpectrumAnalyzer::clear()
{
    display.fill (floorDb);
    readPos = writePos.load (std::memory_order_acquire);
}

bool SpectrumAnalyzer::render()
{
    const double sampleRate = fs.load (std::memory_order_relaxed);
    if (sampleRate <= 0.0) return false;
    if (fftOrderFor (sampleRate) != order || ! juce::exactlyEqual (sampleRate, rate))
        configure (fftOrderFor (sampleRate), sampleRate);

    const uint32_t hop = (uint32_t) hopSize();
    const uint32_t hops = (writePos.load (std::memory_order_acquire) - readPos) / hop;
    if (hops == 0) return false;
    readPos += hops * hop;          // if the GUI fell behind, skip to the newest frame

    const uint32_t start = readPos - (uint32_t) size;
    for (int n = 0; n < size; ++n)
        fftData[(size_t) n] = ring[(size_t) ((start + (uint32_t) n) & ringMask)] * window[(size_t) n];
    fft->performFrequencyOnlyForwardTransform (fftData.data());

    // Bin powers, 0 dB = full-scale sine: |X| * 2 / (N * coherent gain) = |X| * 4 / N.
    const int numBins = size / 2;
    const double norm = 4.0 / (double) size;
    cumulative[0] = cumulative[1] = 0.0;    // DC is left out
    binDb[0] = floorDb;
    for (int k = 1; k < numBins; ++k)
    {
        const double a = (double) fftData[(size_t) k] * norm;
        const double p = a * a;
        cumulative[(size_t) k + 1] = cumulative[(size_t) k] + p;
        binDb[(size_t) k] = (float) (10.0 * std::log10 (p + 1.0e-12));
    }

    // Sum of the bin powers up to position x (in bins); bin k spans k - 0.5 .. k + 0.5.
    auto powerUpTo = [this, numBins] (double x)
    {
        x = juce::jlimit (0.0, (double) numBins, x + 0.5);
        const int i = juce::jmin (numBins - 1, (int) x);
        return cumulative[(size_t) i] + (x - i) * (cumulative[(size_t) i + 1] - cumulative[(size_t) i]);
    };
    // Smooth (Catmull-Rom) curve through the bin levels, for where bins are sparse.
    auto interpolatedDb = [this, numBins] (double x)
    {
        const int i = juce::jlimit (1, numBins - 3, (int) x);
        const float t = (float) juce::jlimit (0.0, 1.0, x - i);
        const float y0 = binDb[(size_t) i - 1], y1 = binDb[(size_t) i], y2 = binDb[(size_t) i + 1], y3 = binDb[(size_t) i + 2];
        return y1 + 0.5f * t * (y2 - y0 + t * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + t * (3.0f * (y1 - y2) + y3 - y0)));
    };

    const double binHz = sampleRate / (double) size;
    const double halfBand = std::pow (2.0, 0.5 * (double) smoothingOctaves);
    const float dt = (float) ((double) (hops * hop) / sampleRate);
    const float attack = 1.0f - std::exp (-dt / attackSeconds), release = 1.0f - std::exp (-dt / releaseSeconds);

    std::array<float, numPoints> level;     // dB, before tilt and ballistics
    for (int i = 0; i < numPoints; ++i)
    {
        const float f = pointFreq (i);
        const double pos = juce::jmin ((double) f / binHz, (double) numBins - 1.0);
        double lo = pos / halfBand, hi = pos * halfBand;
        const double bandBins = hi - lo;
        if (bandBins < 1.0) { lo = pos - 0.5; hi = pos + 0.5; }

        float db = (float) (10.0 * std::log10 ((powerUpTo (hi) - powerUpTo (lo)) / (hi - lo) + 1.0e-12));
        if (bandBins < 2.0)   // sparse bins (the bass): interpolate, blending into the band mean
            db = juce::jmap ((float) juce::jlimit (0.0, 1.0, bandBins - 1.0), interpolatedDb (pos), db);

        level[(size_t) i] = juce::jmax (floorDb, db);
    }

    // A second pass of the same width over the points (in dB) turns the flat-topped
    // band mean into a rounded one; then tilt and ballistics.
    const int reach = juce::roundToInt (0.5f * smoothingOctaves * (float) (numPoints - 1) / std::log2 (maxHz / minHz));
    for (int i = 0; i < numPoints; ++i)
    {
        const int from = juce::jmax (0, i - reach), to = juce::jmin (numPoints - 1, i + reach);
        float db = 0.0f;
        for (int j = from; j <= to; ++j) db += level[(size_t) j];
        db = db / (float) (to - from + 1) + tiltDbPerOctave * std::log2 (pointFreq (i) / tiltPivotHz);

        float& s = display[(size_t) i];
        s += (db - s) * (db > s ? attack : release);
    }
    return true;
}

} // namespace wsho::dsp
