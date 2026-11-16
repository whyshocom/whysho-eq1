#pragma once
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <memory>
#include <vector>

namespace wsho::dsp
{
// FFT analyzer, one instance per tap (pre / post / sidechain).
//
// The audio thread only writes samples into a ring buffer (push / pushBlock: no
// locks, no allocation). The GUI thread calls render(), which takes the newest
// FFT-size window off the ring whenever another hop of audio has arrived and
// turns it into a display curve: numPoints levels, evenly spaced on a
// log-frequency axis from minHz to maxHz.
//
// FFT: Hann window, 75% overlap. 8192 points up to 48 kHz, 16384 from 88.2 kHz,
// 32768 from 176.4 kHz, so a bin is always 5.4 .. 5.9 Hz wide.
//
// Level reference: bins are amplitude-normalised, 0 dB = a full-scale sine
// (peak 1.0) sitting on a bin, whatever the FFT size. The curve is then the
// mean power over smoothingOctaves around each point (between neighbouring bins
// it is interpolated instead, where bins are wider than that), so broadband
// material keeps its per-bin level while a pure tone, being narrower than the
// smoothing band, reads lower by about 10*log10 (bins in the band / 1.5):
// a 1 kHz sine reads 12 dB under its level.
// Finally the curve is tilted by tiltDbPerOctave around tiltPivotHz.
class SpectrumAnalyzer
{
public:
    static constexpr float minHz = 20.0f, maxHz = 22000.0f;   // span of the display curve
    static constexpr int   numPoints = 512;                   // about 50 per octave

    static constexpr float tiltDbPerOctave  = 4.5f;           // makes typical programme material look level
    static constexpr float tiltPivotHz      = 1000.0f;
    static constexpr float smoothingOctaves = 1.0f / 6.0f;
    static constexpr float attackSeconds    = 0.02f;          // ballistics (time constants, in dB)
    static constexpr float releaseSeconds   = 0.35f;
    static constexpr float floorDb          = -120.0f;

    static int fftOrderFor (double sampleRate) noexcept
    {
        return sampleRate >= 176400.0 ? 15 : sampleRate >= 88200.0 ? 14 : 13;
    }

    SpectrumAnalyzer();

    // Any thread. The GUI side picks the new rate up on its next render().
    void prepare (double sampleRate) noexcept { fs.store (sampleRate, std::memory_order_relaxed); }

    // ---- audio thread ----
    inline void push (float sample) noexcept
    {
        const auto w = writePos.load (std::memory_order_relaxed);
        ring[(size_t) (w & ringMask)] = sample;
        writePos.store (w + 1, std::memory_order_release);
    }

    void pushBlock (const float* data, int n) noexcept { for (int i = 0; i < n; ++i) push (data[i]); }

    // ---- GUI thread ----
    // Returns true if the curve changed (a new FFT frame was available).
    bool render();

    // Forget the displayed curve and wait for fresh audio (the tap was just switched on).
    void clear();

    // Tilted dB, one value per display point.
    const std::array<float, numPoints>& levels() const noexcept { return display; }
    static float pointFreq (int i) noexcept
    {
        return minHz * std::pow (maxHz / minHz, (float) i / (float) (numPoints - 1));
    }

    int fftSize() const noexcept { return size; }
    int hopSize() const noexcept { return size / 4; }

private:
    void configure (int order, double sampleRate);

    // The ring holds twice the largest FFT, so the window being read is never
    // the part being written.
    static constexpr uint32_t ringSize = 1u << 16, ringMask = ringSize - 1;
    std::vector<float> ring;
    std::atomic<uint32_t> writePos { 0 };
    std::atomic<double> fs { 44100.0 };

    // GUI-thread state
    std::unique_ptr<juce::dsp::FFT> fft;
    std::vector<float> window, fftData, binDb;
    std::vector<double> cumulative;          // running sum of bin powers
    std::array<float, numPoints> display {};
    int order = 0, size = 0;
    double rate = 0.0;
    uint32_t readPos = 0;
};

} // namespace wsho::dsp
