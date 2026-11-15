// Stress tests for the EQ engine: whatever the settings, sample rate, channel
// count or block size, the output must stay finite, and a flat EQ must be
// transparent.
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

    // 2. Oversampling, sidechain-keyed dynamics, and blocks larger than prepared.
    for (int os = 0; os <= 4; ++os)
        for (int channels : { 1, 2 })
            for (int blockSize : { 16, 256, 1000 })       // 1000 > the 256 announced in prepare()
            {
                EqEngine eng;
                eng.prepare (48000.0, 256, channels);
                EngineParams p;
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
}
