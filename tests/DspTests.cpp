// Stress tests for the EQ engine: whatever the settings, sample rate, channel
// count or block size, the output must stay finite, and a flat EQ must be
// transparent. Then the claims: Natural Phase follows the analog curve, Linear
// Phase is symmetric / accurate / right per channel, every filter type can be
// dynamic, and mix / bypass never comb-filter. The measured figures are printed.
#include "dsp/EqEngine.h"
#include <iostream>

using namespace wsho;
using namespace wsho::dsp;

extern int failures;
#define CHECK(cond) do { if (! (cond)) { ++failures; std::cout << "FAIL " << __LINE__ << ": " #cond "\n"; } } while (false)

static void fill (juce::AudioBuffer<float>& b, juce::Random& rng)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            b.setSample (c, i, rng.nextFloat() * 1.6f - 0.8f);
}

static bool finite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (c, i)) || std::abs (b.getSample (c, i)) > 1.0e9f) return false;
    return true;
}

// Runs a signal through the engine in host-sized blocks and returns the output.
static juce::AudioBuffer<float> run (EqEngine& eng, const juce::AudioBuffer<float>& in, int block = 256)
{
    juce::AudioBuffer<float> out;
    out.makeCopyOf (in);
    for (int pos = 0; pos < out.getNumSamples(); pos += block)
    {
        juce::AudioBuffer<float> view (out.getArrayOfWritePointers(), out.getNumChannels(), pos,
                                       juce::jmin (block, out.getNumSamples() - pos));
        eng.process (view, {});
    }
    return out;
}

static juce::AudioBuffer<float> impulse (int channels, int length)
{
    juce::AudioBuffer<float> b (channels, length);
    b.clear();
    for (int c = 0; c < channels; ++c) b.setSample (c, 0, 1.0f);
    return b;
}

// Response of an impulse response at one frequency, with `delay` samples taken out.
static std::complex<double> responseAt (const juce::AudioBuffer<float>& ir, int channel, double freq, double fs, int delay = 0)
{
    std::complex<double> sum;
    for (int i = 0; i < ir.getNumSamples(); ++i)
        sum += (double) ir.getSample (channel, i) * std::polar (1.0, -2.0 * M_PI * freq * (double) (i - delay) / fs);
    return sum;
}

static double toDb (double gain) { return 20.0 * std::log10 (std::max (gain, 1.0e-12)); }

// The analog (s-domain) bell, written out here so the test doesn't check the
// engine against its own formula.
static std::complex<double> analogBell (double f, double f0, double gainDb, double q)
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const std::complex<double> s (0.0, f / f0);
    return (s * s + s * (A / q) + 1.0) / (s * s + s / (A * q) + 1.0);
}

static double worstDifference (const juce::AudioBuffer<float>& a, int chanA, int startA,
                               const juce::AudioBuffer<float>& b, int chanB, int startB, int length)
{
    double worst = 0.0;
    for (int i = 0; i < length; ++i)
        worst = std::max (worst, (double) std::abs (a.getSample (chanA, startA + i) - b.getSample (chanB, startB + i)));
    return worst;
}

// How symmetric an impulse response is around `centre` (0 = perfectly).
static double asymmetry (const juce::AudioBuffer<float>& ir, int channel, int centre)
{
    double worst = 0.0;
    for (int k = 1; k <= centre && centre + k < ir.getNumSamples(); ++k)
        worst = std::max (worst, (double) std::abs (ir.getSample (channel, centre + k) - ir.getSample (channel, centre - k)));
    return worst;
}

static int peakIndex (const juce::AudioBuffer<float>& ir, int channel)
{
    int best = 0;
    for (int i = 1; i < ir.getNumSamples(); ++i)
        if (std::abs (ir.getSample (channel, i)) > std::abs (ir.getSample (channel, best))) best = i;
    return best;
}

static BandParams makeBand (FilterType type, float freq, float gainDb, float q, ChannelMode chan = ChannelMode::Stereo)
{
    BandParams b;
    b.on = true; b.type = type; b.freq = freq; b.gainDb = gainDb; b.q = q; b.chan = chan;
    return b;
}

// ---------------------------------------------------------------------------
//  Natural Phase: the analog curve (magnitude and phase) at a pure delay.
// ---------------------------------------------------------------------------
static void testNaturalPhase()
{
    const double fs = 44100.0;
    const double points[] = { 10000.0, 14000.0, 16000.0, 18000.0 };
    double worstNatural = 0.0, worstZero = 0.0, worstPhaseDeg = 0.0;

    for (auto mode : { PhaseMode::Natural, PhaseMode::ZeroLatency })
    {
        EqEngine eng;
        eng.prepare (fs, 256, 2);
        EngineParams p;
        p.phase = mode;
        p.bands[0] = makeBand (FilterType::Bell, 16000.0f, 12.0f, 1.0f);
        eng.setParameters (p);
        eng.snapSmoothers();
        const int lat = eng.getLatencySamples();
        const auto ir = run (eng, impulse (2, 4096));

        std::cout << "  " << (mode == PhaseMode::Natural ? "Natural " : "Zero Lat") << " (latency " << lat << "):";
        for (double f : points)
        {
            const auto want = analogBell (f, 16000.0, 12.0, 1.0);
            const auto got  = responseAt (ir, 0, f, fs, lat);
            const double err = toDb (std::abs (got)) - toDb (std::abs (want));
            std::cout << "  " << f / 1000.0 << "k " << toDb (std::abs (got)) << " dB (analog " << toDb (std::abs (want)) << ")";
            if (mode == PhaseMode::Natural)
            {
                worstNatural = std::max (worstNatural, std::abs (err));
                worstPhaseDeg = std::max (worstPhaseDeg, std::abs (std::arg (got / want)) * 180.0 / M_PI);
            }
            else worstZero = std::max (worstZero, std::abs (err));
        }
        std::cout << "\n";
        CHECK (mode == PhaseMode::Natural ? lat > 0 : lat == 0);
    }
    std::cout << "  Natural worst error " << worstNatural << " dB, " << worstPhaseDeg << " deg; Zero Latency worst " << worstZero << " dB\n";
    CHECK (worstNatural < 0.75);
    CHECK (worstPhaseDeg < 5.0);
    CHECK (worstZero > 1.5);

    // With a flat EQ, Natural is a pure delay of exactly the reported latency at
    // every rate and oversampling setting: a symmetric pulse centred on it.
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int os = 0; os <= 4; ++os)
        {
            EqEngine eng;
            eng.prepare (rate, 256, 2);
            EngineParams p;
            p.phase = PhaseMode::Natural;
            p.osFactor = os;
            eng.setParameters (p);
            eng.snapSmoothers();
            const int lat = eng.getLatencySamples();
            const auto ir = run (eng, impulse (2, 2048));
            if (os == 0)
                std::cout << "  Natural at " << rate << " Hz: latency " << lat << " samples; flat EQ reads "
                          << toDb (std::abs (responseAt (ir, 0, 18000.0, rate))) << " dB at 18 kHz, "
                          << toDb (std::abs (responseAt (ir, 0, 20000.0, rate))) << " dB at 20 kHz\n";
            CHECK (peakIndex (ir, 0) == lat);
            CHECK (asymmetry (ir, 0, lat) < 1.0e-5);
            // (JUCE's half-band filters are 0.001 dB flat at 2x / 4x, 0.04 dB at 16x.)
            CHECK (std::abs (toDb (std::abs (responseAt (ir, 1, 1000.0, rate)))) < 0.05);
        }
}

// ---------------------------------------------------------------------------
//  Linear Phase.
// ---------------------------------------------------------------------------
static void testLinearPhase (juce::Random& rng)
{
    const double fs = 48000.0;

    // (a) symmetric impulse response = truly linear phase, (b) the gains asked
    // for at the band centres, (d) reported latency = measured delay.
    {
        EqEngine eng;
        eng.prepare (fs, 256, 2);
        EngineParams p;
        p.phase = PhaseMode::Linear;
        p.osFactor = 2;                                         // ignored in this mode
        p.bands[0] = makeBand (FilterType::Bell, 100.0f, 6.0f, 1.0f);
        p.bands[1] = makeBand (FilterType::Bell, 5000.0f, -9.0f, 1.0f);
        p.bands[2] = makeBand (FilterType::HighShelf, 12000.0f, 4.0f, 0.707f);
        p.bands[3] = makeBand (FilterType::HighPass, 20.0f, 0.0f, 0.707f);
        eng.setParameters (p);
        eng.snapSmoothers();
        const int lat = eng.getLatencySamples();
        const auto ir = run (eng, impulse (2, 2 * lat + 1024));

        const double at100 = toDb (std::abs (responseAt (ir, 0, 100.0, fs)));
        const double at5k  = toDb (std::abs (responseAt (ir, 0, 5000.0, fs)));
        std::cout << "  Linear at 48k: latency " << lat << ", tail " << eng.getTailSeconds() << " s, impulse peak at "
                  << peakIndex (ir, 0) << ", asymmetry " << asymmetry (ir, 0, lat)
                  << ", 100 Hz " << at100 << " dB (want +6), 5 kHz " << at5k << " dB (want -9)\n";
        CHECK (lat == 4096);
        CHECK (peakIndex (ir, 0) == lat);
        CHECK (asymmetry (ir, 0, lat) < 1.0e-5);
        CHECK (asymmetry (ir, 1, lat) < 1.0e-5);
        // The 12 kHz shelf is still worth +0.1 dB at 5 kHz, the 20 Hz high pass and the other bell nothing.
        CHECK (std::abs (at100 - 6.0) < 0.5);
        CHECK (std::abs (at5k + 9.0) < 0.5);
        CHECK (eng.getTailSeconds() > 0.1);
    }

    // (d) again, flat: the impulse comes out as an impulse, exactly `latency` late.
    for (double rate : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int channels : { 1, 2 })
        {
            EqEngine eng;
            eng.prepare (rate, 512, channels);
            EngineParams p;
            p.phase = PhaseMode::Linear;
            eng.setParameters (p);
            eng.snapSmoothers();
            const int lat = eng.getLatencySamples();
            const auto in = impulse (channels, lat + 1024);
            const auto out = run (eng, in, 512);
            if (channels == 2) std::cout << "  Linear latency at " << rate << " Hz: " << lat << " samples\n";
            CHECK (worstDifference (out, channels - 1, lat, in, channels - 1, 0, 1024) < 1.0e-5);
            double early = 0.0;
            for (int i = 0; i < lat; ++i) early = std::max (early, (double) std::abs (out.getSample (0, i)));
            CHECK (early < 1.0e-5);
        }

    // (c) channel modes. A Side band leaves a mono signal alone and changes the
    // side signal; a Left band leaves the right channel alone; Mid / Right likewise.
    for (auto chan : { ChannelMode::Side, ChannelMode::Mid, ChannelMode::Left, ChannelMode::Right })
    {
        EqEngine eng;
        eng.prepare (fs, 256, 2);
        EngineParams p;
        p.phase = PhaseMode::Linear;
        p.bands[0] = makeBand (FilterType::Bell, 1000.0f, 12.0f, 1.0f, chan);
        eng.setParameters (p);
        eng.snapSmoothers();
        const int lat = eng.getLatencySamples();
        const int len = 2 * lat + 1024;

        // The band's gain at 1 kHz as seen by a mono (L = R) signal, a side
        // (L = -R) signal and each channel on its own.
        auto gains = [&] (float l, float r)
        {
            eng.reset();
            juce::AudioBuffer<float> in (2, len);
            in.clear();
            in.setSample (0, 0, l); in.setSample (1, 0, r);
            const auto ir = run (eng, in);
            return std::pair<double, double> { toDb (std::abs (responseAt (ir, 0, 1000.0, fs))), toDb (std::abs (responseAt (ir, 1, 1000.0, fs))) };
        };
        const auto mono = gains (1.0f, 1.0f), side = gains (1.0f, -1.0f), left = gains (1.0f, 0.0f), right = gains (0.0f, 1.0f);
        std::cout << "  Linear " << kChannelModeNames[(int) chan] << " band +12 dB: mono in " << mono.first << " dB, side in " << side.first
                  << " dB, L only -> L " << left.first << " dB, R only -> R " << right.second << " dB\n";
        if (chan == ChannelMode::Side)  { CHECK (std::abs (mono.first) < 0.01); CHECK (std::abs (mono.second) < 0.01); CHECK (std::abs (side.first - 12.0) < 0.5); CHECK (std::abs (side.second - 12.0) < 0.5); }
        if (chan == ChannelMode::Mid)   { CHECK (std::abs (side.first) < 0.01); CHECK (std::abs (mono.first - 12.0) < 0.5); }
        if (chan == ChannelMode::Left)  { CHECK (std::abs (right.second) < 0.01); CHECK (right.first < -100.0); CHECK (std::abs (left.first - 12.0) < 0.5); }
        if (chan == ChannelMode::Right) { CHECK (std::abs (left.first) < 0.01); CHECK (left.second < -100.0); CHECK (std::abs (right.second - 12.0) < 0.5); }

        if (chan == ChannelMode::Side || chan == ChannelMode::Left)
        {
            // In the time domain too: the untouched signal is the input, delayed.
            eng.reset();
            juce::AudioBuffer<float> in (2, len);
            fill (in, rng);
            if (chan == ChannelMode::Side) in.copyFrom (1, 0, in, 0, 0, len);       // L = R
            const auto out = run (eng, in);
            const double worst = worstDifference (out, 1, lat, in, 1, 0, len - lat);
            std::cout << "    untouched channel vs delayed input: " << worst << "\n";
            CHECK (worst < 1.0e-4);
            if (chan == ChannelMode::Side) CHECK (worstDifference (out, 0, lat, in, 0, 0, len - lat) < 1.0e-4);
            else                           CHECK (worstDifference (out, 0, lat, in, 0, 0, len - lat) > 0.1);
        }
    }

    // (e) no NaN, any rate / channel count / block size, with every filter type in the kernel.
    for (double rate : { 22050.0, 32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
        for (int channels : { 1, 2 })
        {
            EqEngine eng;
            eng.prepare (rate, 256, channels);
            EngineParams p;
            p.phase = PhaseMode::Linear;
            p.osFactor = 3;
            for (int b = 0; b < 9; ++b)
            {
                p.bands[(size_t) b] = makeBand ((FilterType) b, 20.0f * std::pow (1000.0f, (float) b / 8.0f), b % 2 ? 30.0f : -30.0f,
                                                b % 3 ? 18.0f : 0.1f, (ChannelMode) (b % 5));
                p.bands[(size_t) b].order = 16;
            }
            p.bands[9] = makeBand (FilterType::Bell, 2000.0f, 6.0f, 2.0f);
            p.bands[9].dyn = true;                                  // runs as a biquad after the FIR
            eng.setParameters (p);
            juce::AudioBuffer<float> buf (channels, 1000);          // > the 256 announced
            bool ok = true;
            for (int block = 0; block < 40 && ok; ++block) { fill (buf, rng); eng.process (buf, {}); ok = finite (buf); }
            if (! ok) std::cout << "  linear fs " << rate << " ch " << channels << "\n";
            CHECK (ok);
            CHECK (eng.getLatencySamples() == (rate < 40000.0 ? 2048 : rate < 80000.0 ? 4096 : rate < 160000.0 ? 8192 : 16384));
        }

    // Real-time path: a curve change is designed in the background and fades in;
    // the audio thread just keeps running, and nothing wild comes out meanwhile.
    {
        EqEngine eng;
        eng.prepare (fs, 256, 2);
        EngineParams p;
        p.phase = PhaseMode::Linear;
        eng.setParameters (p);                                      // flat, designed inline
        eng.snapSmoothers();
        p.bands[0] = makeBand (FilterType::Bell, 1000.0f, 12.0f, 1.0f);

        juce::AudioBuffer<float> buf (2, 256);
        double phase = 0.0, lastPeak = 0.0, worstPeak = 0.0;
        bool ok = true;
        int blocks = 0;
        for (; blocks < 2000 && lastPeak < 3.9; ++blocks)
        {
            for (int i = 0; i < 256; ++i, phase += 2.0 * M_PI * 1000.0 / fs)
                for (int c = 0; c < 2; ++c) buf.setSample (c, i, (float) std::sin (phase));
            eng.setParameters (p);
            eng.process (buf, {});
            ok = ok && finite (buf);
            lastPeak = buf.getMagnitude (0, 256);
            worstPeak = std::max (worstPeak, lastPeak);
            juce::Thread::sleep (1);
        }
        std::cout << "  Linear, real time: new kernel audible after " << blocks << " blocks of 256, peak " << lastPeak << " (want 3.98)\n";
        CHECK (ok);
        CHECK (lastPeak > 3.9 && worstPeak < 4.1);
    }

    // Leaving Linear and coming back must not replay what was in the FIR's memory.
    {
        EqEngine eng;
        eng.prepare (fs, 256, 2);
        EngineParams p;
        p.phase = PhaseMode::Linear;
        eng.setParameters (p);
        eng.snapSmoothers();
        juce::AudioBuffer<float> noise (2, 8192), silence (2, 8192);
        fill (noise, rng);
        silence.clear();
        run (eng, noise);
        p.phase = PhaseMode::ZeroLatency; eng.setParameters (p);
        run (eng, silence);
        p.phase = PhaseMode::Linear; eng.setParameters (p);
        CHECK (run (eng, silence).getMagnitude (0, 8192) < 1.0e-6f);
    }

    // Offline rendering: the change is designed on the spot, so it is in
    // effect one latency later, with no waiting for a thread.
    {
        EqEngine eng;
        eng.prepare (fs, 256, 2);
        eng.setNonRealtime (true);
        EngineParams p;
        p.phase = PhaseMode::Linear;
        eng.setParameters (p);
        eng.snapSmoothers();
        p.bands[0] = makeBand (FilterType::Bell, 1000.0f, 12.0f, 1.0f);
        eng.setParameters (p);
        const auto ir = run (eng, impulse (2, 3 * 4096));
        CHECK (std::abs (toDb (std::abs (responseAt (ir, 0, 1000.0, fs))) - 12.0) < 0.5);
    }
}

// ---------------------------------------------------------------------------
//  Dynamic EQ on every filter type, and the read-out the GUI shows.
// ---------------------------------------------------------------------------
static void testDynamics()
{
    const double fs = 48000.0;
    const int len = 24000;

    // Tones below, at and above the band; returns how far the output is from
    // the input (rms of the difference / rms of the input) and the read-out.
    auto measure = [&] (EngineParams p, float level, double& deviation, float& amount)
    {
        EqEngine eng;
        eng.prepare (fs, 256, 2);
        eng.setParameters (p);
        eng.snapSmoothers();
        const int lat = eng.getLatencySamples();
        juce::AudioBuffer<float> in (2, len + lat);
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const double t = (double) i / fs;
            const float v = level * (float) (std::sin (2.0 * M_PI * 500.0 * t) + std::sin (2.0 * M_PI * 1000.0 * t) + std::sin (2.0 * M_PI * 2000.0 * t));
            in.setSample (0, i, v); in.setSample (1, i, v);
        }
        const auto out = run (eng, in);
        amount = eng.dynamicAmount (0);
        double diff = 0.0, ref = 0.0;
        for (int i = len / 2; i < len; ++i)                    // settled part
        {
            const double x = in.getSample (0, i), y = out.getSample (0, i + lat);
            diff += (y - x) * (y - x); ref += x * x;
        }
        deviation = std::sqrt (diff / ref);
        CHECK (finite (out));
        for (int b = 1; b < kNumBands; ++b) CHECK (juce::exactlyEqual (eng.dynamicAmount (b), 0.0f));     // off = 0
    };

    for (int type = 0; type < 9; ++type)
        for (float range : { -24.0f, 24.0f })
        {
            EngineParams p;
            p.bands[0] = makeBand ((FilterType) type, 1000.0f, 0.0f, 1.0f);
            p.bands[0].dyn = true;
            p.bands[0].dynThreshDb = -40.0f;
            p.bands[0].dynRangeDb = range;
            double quiet = 0.0, loud = 0.0;
            float quietAmount = 1.0f, loudAmount = 0.0f;
            measure (p, 0.0003f, quiet, quietAmount);
            measure (p, 0.3f, loud, loudAmount);
            std::cout << "  dynamic " << kFilterTypeNames[type] << ", range " << range << " dB: change to the signal quiet " << quiet
                      << " / loud " << loud << ", read-out " << quietAmount << " / " << loudAmount << "\n";
            CHECK (std::abs (loud - quiet) > 0.05);
            CHECK (quietAmount < 0.01f);
            CHECK (loudAmount > 0.5f);

            // Not dynamic: a linear filter again, the same at any level, read-out 0.
            p.bands[0].dyn = false;
            measure (p, 0.0003f, quiet, quietAmount);
            measure (p, 0.3f, loud, loudAmount);
            CHECK (std::abs (loud - quiet) < 1.0e-3);
            CHECK (juce::exactlyEqual (loudAmount, 0.0f));
        }

    // The same in the other two modes (in Linear the dynamic band runs after the FIR).
    for (auto mode : { PhaseMode::Natural, PhaseMode::Linear })
        for (auto type : { FilterType::Bell, FilterType::HighPass })
        {
            EngineParams p;
            p.phase = mode;
            p.bands[0] = makeBand (type, 1000.0f, 0.0f, 1.0f);
            p.bands[0].dyn = true;
            p.bands[0].dynThreshDb = -40.0f;
            p.bands[0].dynRangeDb = -24.0f;
            double quiet = 0.0, loud = 0.0;
            float quietAmount = 1.0f, loudAmount = 0.0f;
            measure (p, 0.0003f, quiet, quietAmount);
            measure (p, 0.3f, loud, loudAmount);
            std::cout << "  dynamic " << kFilterTypeNames[(int) type] << " in " << kPhaseModeNames[(int) mode] << ": change quiet " << quiet
                      << " / loud " << loud << ", read-out " << quietAmount << " / " << loudAmount << "\n";
            CHECK (quiet < 0.01 && loud > 0.3);
            CHECK (quietAmount < 0.01f && loudAmount > 0.5f);
        }
}

// ---------------------------------------------------------------------------
//  Mix and Bypass use a dry signal that is as late as the processed one.
// ---------------------------------------------------------------------------
static void testDryAlignment (juce::Random& rng)
{
    const double fs = 48000.0;

    // Linear, no bands, half wet: just the input, delayed. No comb filter.
    {
        EqEngine eng;
        eng.prepare (fs, 256, 2);
        EngineParams p;
        p.phase = PhaseMode::Linear;
        p.mix = 0.5f;
        eng.setParameters (p);
        eng.snapSmoothers();
        const int lat = eng.getLatencySamples();
        juce::AudioBuffer<float> in (2, lat + 8192);
        fill (in, rng);
        const auto out = run (eng, in);
        const double worst = std::max (worstDifference (out, 0, lat, in, 0, 0, 8192), worstDifference (out, 1, lat, in, 1, 0, 8192));
        std::cout << "  Linear, mix 50%, no bands: worst difference from the delayed input " << worst << "\n";
        CHECK (worst < 1.0e-4);
    }

    // Bypass, in every mode: same latency as when active, output = delayed input.
    for (auto mode : { PhaseMode::ZeroLatency, PhaseMode::Natural, PhaseMode::Linear })
    {
        EqEngine eng;
        eng.prepare (fs, 256, 2);
        EngineParams p;
        p.phase = mode;
        p.bands[0] = makeBand (FilterType::Bell, 1000.0f, 12.0f, 1.0f);
        eng.setParameters (p);
        const int activeLatency = eng.getLatencySamples();
        p.bypass = true;
        eng.setParameters (p);
        eng.snapSmoothers();
        const int lat = eng.getLatencySamples();
        juce::AudioBuffer<float> in (2, lat + 4096);
        fill (in, rng);
        const auto out = run (eng, in);
        const double worst = worstDifference (out, 0, lat, in, 0, 0, 4096);
        std::cout << "  Bypass in " << kPhaseModeNames[(int) mode] << ": latency " << lat << ", worst difference from the delayed input " << worst << "\n";
        CHECK (lat == activeLatency);
        CHECK (worst < 1.0e-6);
    }

    // Natural, half wet, on a band-limited signal: still the delayed input
    // (above ~19 kHz the resampling filters roll off, so not for white noise).
    {
        EqEngine eng;
        eng.prepare (fs, 256, 2);
        EngineParams p;
        p.phase = PhaseMode::Natural;
        p.mix = 0.5f;
        eng.setParameters (p);
        eng.snapSmoothers();
        const int lat = eng.getLatencySamples();
        juce::AudioBuffer<float> in (2, lat + 8192);
        for (int i = 0; i < in.getNumSamples(); ++i)
        {
            const double t = (double) i / fs;
            const float v = 0.25f * (float) (std::sin (2.0 * M_PI * 100.0 * t) + std::sin (2.0 * M_PI * 3000.0 * t) + std::sin (2.0 * M_PI * 15000.0 * t));
            in.setSample (0, i, v); in.setSample (1, i, v);
        }
        const auto out = run (eng, in);
        const double worst = worstDifference (out, 0, lat + 2048, in, 0, 2048, 4096);
        std::cout << "  Natural, mix 50%, no bands: worst difference from the delayed input " << worst << "\n";
        CHECK (worst < 1.0e-3);
    }
}

void runDspTests()
{
    juce::Random rng (7);

    // 1. Every filter type at extreme Q / gain / frequency, at every rate.
    for (double fs : { 22050.0, 32000.0, 44100.0, 96000.0, 192000.0 })
        for (int type = 0; type < 9; ++type)
            for (float q : { 0.1f, 0.707f, 4.0f, 18.0f })
                for (float gain : { -30.0f, 15.0f, 30.0f })
                    for (int channels : { 1, 2 })
                    {
                        EqEngine eng;
                        eng.prepare (fs, 256, channels);
                        EngineParams p;
                        for (int b = 0; b < 3; ++b)
                        {
                            auto& bp = p.bands[(size_t) b];
                            bp.on = true; bp.type = (FilterType) type; bp.q = q; bp.gainDb = gain;
                            bp.freq = b == 0 ? 20.0f : b == 1 ? 1000.0f : 20000.0f;   // 20 kHz is above Nyquist at 22/32 kHz
                            bp.order = 3;                                             // the odd Butterworth order
                            bp.chan = b == 1 ? ChannelMode::Mid : ChannelMode::Stereo;
                        }
                        eng.setParameters (p);
                        juce::AudioBuffer<float> buf (channels, 256);
                        bool ok = true;
                        for (int block = 0; block < 8 && ok; ++block) { fill (buf, rng); eng.process (buf, {}); ok = finite (buf); }
                        if (! ok) std::cout << "  fs " << fs << " type " << type << " q " << q << " gain " << gain << " ch " << channels << "\n";
                        CHECK (ok);
                    }

    // 2. Oversampling, sidechain-keyed dynamics, and blocks larger than prepared,
    //    in every phase mode.
    for (auto mode : { PhaseMode::ZeroLatency, PhaseMode::Natural, PhaseMode::Linear })
      for (int os = 0; os <= 4; ++os)
        for (int channels : { 1, 2 })
            for (int blockSize : { 16, 256, 1000 })       // 1000 > the 256 announced in prepare()
            {
                EqEngine eng;
                eng.prepare (48000.0, 256, channels);
                EngineParams p;
                p.phase = mode;
                p.osFactor = os;
                auto& bp = p.bands[0];
                bp.on = true; bp.type = FilterType::Bell; bp.freq = 3000.0f; bp.gainDb = 6.0f; bp.q = 2.0f;
                bp.dyn = true; bp.dynSidechain = true; bp.chan = ChannelMode::Side;
                p.bands[1].on = true; p.bands[1].type = FilterType::HighShelf; p.bands[1].freq = 8000.0f; p.bands[1].gainDb = 12.0f;
                eng.setParameters (p);
                juce::AudioBuffer<float> buf (channels, blockSize), sc (2, blockSize);
                bool ok = true;
                for (int block = 0; block < 6 && ok; ++block) { fill (buf, rng); fill (sc, rng); eng.process (buf, sc); ok = finite (buf); }
                CHECK (ok);
            }

    // 3. No bands on = bit-transparent from the very first sample (no fade-in).
    {
        EqEngine eng;
        eng.prepare (48000.0, 512, 2);
        eng.setParameters ({});
        eng.snapSmoothers();
        juce::AudioBuffer<float> buf (2, 512), ref;
        fill (buf, rng); ref.makeCopyOf (buf);
        eng.process (buf, {});
        float worst = 0.0f;
        for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i)
            worst = std::max (worst, std::abs (buf.getSample (c, i) - ref.getSample (c, i)));
        CHECK (worst < 1.0e-6f);
    }

    // 4. Slopes are real Butterworth: -3 dB at the corner, for odd and even orders.
    for (int order : { 2, 3, 4, 8 })
    {
        EqBand band;
        band.prepare (48000.0);
        BandParams bp; bp.on = true; bp.type = FilterType::HighPass; bp.freq = 1000.0f; bp.order = order;
        band.updateCoefficients (bp);
        const double db = 20.0 * std::log10 (std::abs (band.response (2.0 * M_PI * 1000.0 / 48000.0)));
        if (std::abs (db + 3.01) > 0.6) std::cout << "  order " << order << ": " << db << " dB at corner\n";
        CHECK (std::abs (db + 3.01) < 0.6);
    }

    // 5. Natural Phase: the analog curve at a pure delay.
    testNaturalPhase();
    // 6. Linear Phase: symmetric, accurate, right per channel, right latency.
    testLinearPhase (rng);
    // 7. Dynamic EQ on every filter type + the GUI read-out.
    testDynamics();
    // 8. Mix / Bypass against a latency-aligned dry signal.
    testDryAlignment (rng);

    // 9. Latency is known right after prepare() + setParameters(), in every mode.
    for (auto mode : { PhaseMode::ZeroLatency, PhaseMode::Natural, PhaseMode::Linear })
    {
        std::cout << "  latency at 48 kHz, " << kPhaseModeNames[(int) mode] << ", oversampling off / 2x / 4x / 8x / 16x:";
        for (int os = 0; os <= 4; ++os)
        {
            EqEngine eng;
            eng.prepare (48000.0, 512, 2);
            EngineParams p;
            p.phase = mode;
            p.osFactor = os;
            eng.setParameters (p);
            std::cout << " " << eng.getLatencySamples();
            if (mode == PhaseMode::Linear) CHECK (eng.getLatencySamples() == 4096);      // oversampling is ignored
            else                           CHECK ((eng.getLatencySamples() == 0) == (mode == PhaseMode::ZeroLatency && os == 0));
        }
        std::cout << "\n";
    }
}
