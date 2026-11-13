#pragma once
#include <juce_dsp/juce_dsp.h>
#include "EqBand.h"
#include <atomic>

namespace wsho::dsp
{
// Snapshot of all parameters, lifted from the APVTS once per block (lock-free).
struct EngineParams
{
    std::array<BandParams, kNumBands> bands;
    float inGainDb = 0.0f;
    float outGainDb = 0.0f;
    bool  bypass = false;
    float mix = 1.0f;          // 0..1 dry/wet
    PhaseMode phase = PhaseMode::ZeroLatency;
    int   osFactor = 0;        // 0,1,2,3,4 -> 1x,2x,4x,8x,16x
    bool  autoGain = false;
    bool  scListen = false;
    float gainScale = 1.0f;    // global EQ gain multiplier
    bool  anySolo = false;
};

class EqEngine
{
public:
    EqEngine();
    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    // Update coefficients from a new parameter snapshot (audio thread, no alloc
    // beyond pre-reserved buffers; linear-phase IR regen only on change).
    void setParameters (const EngineParams& p);

    // main: in/out stereo buffer. side: optional sidechain (may be empty).
    void process (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>& side);

    int  getLatencySamples() const { return latency; }
    double getSampleRate() const { return baseSampleRate; }

    // For the analyzer: provide pre/post magnitude of combined response.
    float getMagnitudeDb (float freqHz) const;

    EqBand& band (int i) { return bands[(size_t) i]; }

private:
    void processIIR (juce::AudioBuffer<float>& osMain, const juce::AudioBuffer<float>& osSide, double fsNow);
    void rebuildLinearPhase (double fsNow);
    void generateImpulseResponse (juce::AudioBuffer<float>& ir, double fsNow);

    std::array<EqBand, kNumBands> bands;
    EngineParams params;
    bool paramsDirty = true;
    bool linearDirty = true;

    double baseSampleRate = 44100.0;
    double workRate = 44100.0;     // base * oversampling factor
    int    maxBlock = 512;
    int    numCh = 2;
    int    latency = 0;

    // oversampling
    std::unique_ptr<juce::dsp::Oversampling<float>> osMainProc, osSideProc;
    int currentOsFactor = 0;

    // linear phase
    juce::dsp::Convolution linConv;
    bool linActive = false;

    // scratch
    juce::AudioBuffer<float> dryBuffer, sideUp;
    juce::SmoothedValue<float> inGain, outGain, mixSmooth, autoGainComp;
};

} // namespace wsho::dsp
