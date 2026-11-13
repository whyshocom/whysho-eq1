#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>

namespace wsho::dsp
{
// Lock-free-ish FFT analyzer. Audio thread pushes mono samples; GUI thread
// pulls a smoothed magnitude curve. One instance per tap (pre / post / SC).
class SpectrumAnalyzer
{
public:
    static constexpr int fftOrder = 11;            // 2048
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int numBins  = fftSize / 2;

    SpectrumAnalyzer() : fft (fftOrder), window (fftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        smoothed.fill (-120.0f);
    }

    void prepare (double sampleRate) { fs = sampleRate; fifoIndex = 0; blockReady = false; }

    inline void push (float sample) noexcept
    {
        if (fifoIndex == fftSize)
        {
            if (! blockReady.load (std::memory_order_acquire))
            {
                juce::FloatVectorOperations::copy (fftReady.data(), fifo.data(), fftSize);
                blockReady.store (true, std::memory_order_release);
            }
            fifoIndex = 0;
        }
        fifo[(size_t) fifoIndex++] = sample;
    }

    void pushBlock (const float* data, int n) noexcept { for (int i = 0; i < n; ++i) push (data[i]); }

    // GUI thread: returns true if a new curve was produced.
    bool render (float decayDbPerBlock = 6.0f)
    {
        if (! blockReady.load (std::memory_order_acquire)) return false;
        std::array<float, fftSize * 2> fftData {};
        juce::FloatVectorOperations::copy (fftData.data(), fftReady.data(), fftSize);
        blockReady.store (false, std::memory_order_release);

        window.multiplyWithWindowingTable (fftData.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        const float norm = 2.0f / (float) fftSize;
        for (int i = 0; i < numBins; ++i)
        {
            const float db = juce::Decibels::gainToDecibels (fftData[(size_t) i] * norm, -120.0f);
            float& s = smoothed[(size_t) i];
            s = db > s ? db : juce::jmax (db, s - decayDbPerBlock); // fast attack, slow release
        }
        return true;
    }

    float binToFreq (int bin) const { return (float) bin * (float) fs / (float) fftSize; }
    const std::array<float, numBins>& magnitudes() const { return smoothed; }

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::array<float, fftSize> fifo {};
    std::array<float, fftSize> fftReady {};
    std::array<float, numBins> smoothed {};
    int fifoIndex = 0;
    std::atomic<bool> blockReady { false };
    double fs = 44100.0;
};

} // namespace wsho::dsp
