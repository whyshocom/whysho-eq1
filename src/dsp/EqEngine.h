#pragma once
#include <juce_dsp/juce_dsp.h>
#include "EqBand.h"
#include "LinearPhaseEq.h"
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

// ---------------------------------------------------------------------------
//  The three phase modes:
//   Zero Latency  RBJ biquads at the host rate (the curve squeezes toward
//                 Nyquist). No latency. The Oversampling parameter runs them
//                 at a higher rate through minimum-phase (polyphase IIR)
//                 half-band filters, which costs a few samples.
//   Natural Phase the same biquads at 4x (2x from 88.2 kHz up), or the
//                 Oversampling parameter if that asks for more, through
//                 linear-phase FIR half-band filters. Up there the biquads
//                 follow the analog prototype in magnitude and phase across
//                 the audible band; the resampling only adds a pure delay.
//   Linear Phase  the non-dynamic bands as one symmetric FIR (LinearPhaseEq.h)
//                 at the host rate; Oversampling is ignored. Bands with
//                 Dynamic on can't live in a fixed FIR, so they run as
//                 biquads after it.
//  Dry/wet mix and Bypass use the dry signal delayed by the latency, so they
//  never comb-filter and Bypass keeps the reported latency.
// ---------------------------------------------------------------------------
class EqEngine
{
public:
    EqEngine();
    ~EqEngine();

    void prepare (double sampleRate, int maxBlockSize, int numChannels);
    void reset();

    // Update coefficients from a new parameter snapshot. Audio thread: no
    // allocation, no locks. A changed linear-phase curve is handed to the
    // design thread; the new kernel fades in a few milliseconds later.
    // Exceptions, where the kernel is designed right here so the very next
    // block is already correct: the first call after prepare() (make it before
    // audio starts, as prepareToPlay does) and offline rendering.
    void setParameters (const EngineParams& p);

    // Offline rendering (juce::AudioProcessor::isNonRealtime()): linear-phase
    // kernels are designed synchronously instead of in the background.
    void setNonRealtime (bool offline) noexcept { nonRealtime = offline; }

    // main: in/out stereo buffer. side: optional sidechain (may be empty).
    void process (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>& side);

    // Jump gain / mix smoothers to their targets (call after prepare + setParameters,
    // so playback doesn't start with a fade-in).
    void snapSmoothers();

    // Latency of the current settings in samples; exact for Natural and Linear.
    // Valid right after prepare() + setParameters(). Any thread.
    int  getLatencySamples() const noexcept { return latency.load (std::memory_order_relaxed); }
    // How long the output rings on after the input stops (linear phase only). Any thread.
    double getTailSeconds() const noexcept { return (double) tailSeconds.load (std::memory_order_relaxed); }
    double getSampleRate() const { return baseSampleRate; }

    // For the analyzer: provide pre/post magnitude of combined response.
    float getMagnitudeDb (float freqHz) const;

    EqBand& band (int i) { return bands[(size_t) i]; }

    // ---- live dynamics read-out for the GUI (lock-free, any thread) ----------
    // How far band i's dynamics are currently engaged, 0 (idle) .. 1 (fully, i.e.
    // the whole Range is applied). 0 when the band is off or not dynamic.
    float dynamicAmount (int i) const noexcept { return dynAmount[(size_t) i].load (std::memory_order_relaxed); }

private:
    using Oversampler = juce::dsp::Oversampling<float>;
    struct OsPair { std::unique_ptr<Oversampler> main, side; };

    // Fixed-size stereo delay line (dry path, sidechain alignment).
    struct StereoDelay
    {
        void prepare (int maxDelay)
        {
            buf.setSize (2, juce::nextPowerOfTwo (maxDelay + 1), false, true);
            pos = 0;
        }
        void clear() { buf.clear(); }
        // Pushes n samples and writes them, `delay` samples late, to outL / outR.
        void process (const float* l, const float* r, float* outL, float* outR, int n, int delay) noexcept
        {
            const int mask = buf.getNumSamples() - 1;
            float* bl = buf.getWritePointer (0); float* br = buf.getWritePointer (1);
            for (int i = 0; i < n; ++i)
            {
                bl[pos] = l[i]; br[pos] = r[i];
                const int rd = (pos - delay) & mask;
                outL[i] = bl[rd]; outR[i] = br[rd];
                pos = (pos + 1) & mask;
            }
        }
        juce::AudioBuffer<float> buf;
        int pos = 0;
    };

    struct DesignRequest { BandArray bands; juce::uint32 id = 0; };
    struct DesignThread;

    void processChunk (juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>& side);
    void processIIR (juce::AudioBuffer<float>& buf, const juce::AudioBuffer<float>& side, bool dynamicBandsOnly);
    bool queueDesign (const DesignRequest&) noexcept;
    void runQueuedDesigns();                                   // design thread

    std::array<EqBand, kNumBands> bands;
    std::array<std::atomic<float>, kNumBands> dynAmount {};   // written by the audio thread once per block
    EngineParams params;

    double baseSampleRate = 44100.0;
    double workRate = 44100.0;     // the rate the IIR bands run at (base * oversampling factor)
    int    maxBlock = 512;
    int    numCh = 2;
    std::atomic<int>   latency { 0 };
    std::atomic<float> tailSeconds { 0.0f };

    // ---- oversampling: everything is built in prepare(), then only selected ----
    std::array<OsPair, 4> osIir;        // 2x..16x minimum phase (Zero Latency + Oversampling)
    std::array<OsPair, 4> osFir;        // 2x..16x linear phase (Natural)
    std::array<int, 4> firLatency {};   // FIR latency in host samples, rounded up to a whole one ...
    std::array<int, 4> firPad {};       // ... by this many extra samples of delay at the oversampled rate
    int naturalMinLog2 = 2;             // Natural runs at least at 2^this times the host rate
    OsPair* activeOs = nullptr;
    PhaseMode activePhase = PhaseMode::ZeroLatency;
    bool routingDirty = true;
    int  pad = 0, padPos = 0;
    std::array<std::array<float, 16>, 2> padHistory {};

    // ---- linear phase --------------------------------------------------------
    LinearPhaseEq lin;
    BandArray linCurve;                 // what the live / requested kernel was designed for
    bool linDirty = true;               // the kernel needs (re)designing
    bool designInline = true;           // next design happens inside setParameters (first call after prepare)
    bool nonRealtime = false;
    juce::uint32 designCounter = 0;

    // Audio thread -> design thread: single-writer, single-reader queue of curves.
    juce::AbstractFifo designFifo { 8 };
    std::array<DesignRequest, 8> designQueue;
    juce::CriticalSection designLock;   // guards lin.design(), designedId and the queue's read side; never taken by a real-time audio thread
    juce::uint32 designedId = 0;
    std::atomic<bool> linearWanted { false };
    std::unique_ptr<DesignThread> designThread;

    // scratch
    juce::AudioBuffer<float> dryBuffer, sideStereo, monoScratch;
    StereoDelay dryDelay, sideDelay;
    juce::SmoothedValue<float> inGain, outGain, mixSmooth, autoGainComp;
};

} // namespace wsho::dsp
