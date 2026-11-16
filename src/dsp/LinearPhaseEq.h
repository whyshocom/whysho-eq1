#pragma once
#include <juce_dsp/juce_dsp.h>
#include "EqBand.h"
#include <atomic>
#include <vector>

// ===========================================================================
//  Linear-phase EQ: a symmetric FIR designed from the bands' analytic
//  magnitude response, run as a uniformly partitioned FFT convolution.
//
//  Stereo. Every band is a 2x2 real gain matrix per frequency (Stereo scales
//  both channels, Left / Right one of them, Mid / Side act on L+R / L-R). The
//  matrices are multiplied in band order, exactly as the IIR path chains the
//  bands, giving four zero-phase responses: L->L, R->R, R->L and L->R. Each
//  becomes one symmetric kernel, so a Side band really only touches the side
//  signal, a Left band only the left channel.
//
//  Latency. The kernel (N - 2B taps, windowed) is centred on tap N/2 - B
//  and the convolution works in blocks of B = N/32 samples (one block of
//  delay), so input to output is exactly N/2 samples: 2048 below 40 kHz, 4096
//  at 44.1 / 48 kHz, 8192 at 88.2 / 96 kHz, 16384 at 176.4 / 192 kHz. N
//  doubles with the sample rate to keep the low-frequency resolution about
//  constant: bands narrower than roughly 10 Hz (a Q 4 bell at 30 Hz, a Q 10
//  notch at 60 Hz) come out wider and shallower than drawn.
//
//  Threads. design() is slow (FFTs, and JUCE's FFT may allocate scratch for
//  the big design transform): call it from a background thread, or from the
//  audio thread only when rendering offline.
//  It fills a kernel slot the audio thread is not reading and publishes it
//  through an atomic; process() picks it up at its next block boundary and
//  crossfades from the old kernel over that block (B samples), so a redesign
//  never clicks. process() and reset() never allocate, lock or wait.
//  Only one thread may be inside design() at a time.
// ===========================================================================
namespace wsho::dsp
{
using BandArray = std::array<BandParams, kNumBands>;

class LinearPhaseEq
{
public:
    // Allocates everything and installs a flat kernel (a pure delay). `mono`:
    // the track is one channel, where every band but a Side band applies (as in
    // the IIR path). Not for the audio thread.
    void prepare (double sampleRate, bool mono)
    {
        fs = sampleRate;
        isMono = mono;
        fullLen = sampleRate < 40000.0 ? 4096 : sampleRate < 80000.0 ? 8192 : sampleRate < 160000.0 ? 16384 : 32768;
        B = fullLen / (kParts + 2);
        bins = B + 1;
        centre = fullLen / 2 - B;
        designSize = 2 * fullLen;

        blockFft  = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (2.0 * B)));
        partFft   = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 (2.0 * B)));
        designFft = std::make_unique<juce::dsp::FFT> (juce::roundToInt (std::log2 ((double) designSize)));

        for (auto& s : slots)
        {
            s.re.assign ((size_t) (4 * kParts * bins), 0.0f);
            s.im.assign ((size_t) (4 * kParts * bins), 0.0f);
        }
        for (int c = 0; c < 2; ++c)
        {
            in[c].assign ((size_t) (2 * B), 0.0f);
            out[c].assign ((size_t) B, 0.0f);
            fade[c].assign ((size_t) B, 0.0f);
            fdlRe[c].assign ((size_t) (kParts * bins), 0.0f);
            fdlIm[c].assign ((size_t) (kParts * bins), 0.0f);
        }
        work.assign ((size_t) (4 * B), 0.0f);
        accRe.assign ((size_t) bins, 0.0f);
        accIm.assign ((size_t) bins, 0.0f);

        // Window over the kernel, symmetric about `centre` and zero at tap 0: flat
        // over the middle half, raised-cosine tapers outside (Tukey 0.5). Narrow
        // low bands ring for long; this keeps more of that than a Hann window
        // would (a 50 Hz Q 2 bell is 0.2 dB off instead of 1.4 dB) and still
        // ends the kernel smoothly.
        window.resize ((size_t) getKernelLength());
        for (size_t i = 0; i < window.size(); ++i)
        {
            const double d = std::abs ((double) i - (double) centre) / (double) centre;    // 0 centre .. 1 ends
            window[i] = (float) (d <= 0.5 ? 1.0 : 0.5 + 0.5 * std::cos (M_PI * (d - 0.5) / 0.5));
        }

        for (auto& m : mag) m.assign ((size_t) (designSize / 2 + 1), 0.0f);
        spec.assign ((size_t) (2 * designSize), 0.0f);
        partWork.assign ((size_t) (4 * B), 0.0f);

        reset();
        BandArray none;                 // all bands off = flat
        design (none, true);
    }

    int getLatency() const noexcept       { return fullLen / 2; }
    int getKernelLength() const noexcept  { return kParts * B; }

    // Clears the audio history (not the kernel).
    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            std::fill (in[c].begin(), in[c].end(), 0.0f);
            std::fill (out[c].begin(), out[c].end(), 0.0f);
            std::fill (fdlRe[c].begin(), fdlRe[c].end(), 0.0f);
            std::fill (fdlIm[c].begin(), fdlIm[c].end(), 0.0f);
        }
        pos = 0;
        fdlPos = 0;
    }

    // Design the kernels for the bands that are `on` (their dynamics are ignored).
    // installNow: make it the live kernel directly, no crossfade; only while
    // process() is not running (prepare time).
    void design (const BandArray& bands, bool installNow)
    {
        int slot = 0;
        if (! installNow)
        {
            // A slot the audio thread isn't reading and that isn't waiting to be
            // picked up. The audio thread holds at most two and one can be
            // waiting, so one of the four is always free.
            const auto busy = audioSlots.load();
            while (slot < kSlots - 1 && (slot == lastPublished || (busy & (1u << slot)) != 0))
                ++slot;
        }
        auto& ks = slots[(size_t) slot];
        std::fill (ks.re.begin(), ks.re.end(), 0.0f);
        std::fill (ks.im.begin(), ks.im.end(), 0.0f);

        bool any = false;
        ks.cross = false;
        for (auto& b : bands)
        {
            any = any || b.on;
            ks.cross = ks.cross || (b.on && ! isMono && (b.chan == ChannelMode::Mid || b.chan == ChannelMode::Side));
        }

        if (! any)
        {
            // Flat: a single 1 at the centre tap, which is the first tap of the
            // middle partition, whose spectrum is all ones.
            for (int k = 0; k < 2; ++k)
                std::fill_n (ks.re.begin() + (k * kParts + kParts / 2) * bins, bins, 1.0f);
        }
        else
        {
            computeResponse (bands);
            makeKernel (ks, 0, mag[0]);                         // L -> L
            makeKernel (ks, 1, mag[isMono ? 0 : 3]);            // R -> R
            if (ks.cross)
            {
                makeKernel (ks, 2, mag[1]);                     // R -> L
                makeKernel (ks, 3, mag[2]);                     // L -> R
            }
        }

        if (installNow)
        {
            active = slot;
            audioSlots.store (1u << slot);
            pending.store (-1);
        }
        else pending.store (slot);
        lastPublished = slot;
    }

    // In place. R may be nullptr (one channel).
    void process (float* L, float* R, int n) noexcept
    {
        const int numCh = R != nullptr ? 2 : 1;
        float* chans[2] = { L, R };
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < numCh; ++c)
            {
                in[c][(size_t) (B + pos)] = chans[c][i];
                chans[c][i] = out[c][(size_t) pos];
            }
            if (++pos == B)
            {
                computeBlock (numCh);
                pos = 0;
            }
        }
    }

private:
    static constexpr int kParts = 30;       // kernel partitions of B samples
    static constexpr int kSlots = 4;

    // Kernel spectra, split real / imaginary: [kernel][partition][bin].
    // Kernels: 0 L->L, 1 R->R, 2 R->L, 3 L->R (the last two only if `cross`).
    struct KernelSet
    {
        std::vector<float> re, im;
        bool cross = false;
    };

    // ---- audio thread --------------------------------------------------------
    void computeBlock (int numCh) noexcept
    {
        // Take a freshly designed kernel, if there is one. The slot is marked as
        // in use before it is claimed, so the designer can never pick it again.
        const int previous = active;
        int next = pending.load();
        if (next >= 0)
        {
            audioSlots.store ((1u << active) | (1u << next));
            const int wanted = next;
            if (pending.compare_exchange_strong (next, -1)) active = wanted;
            else audioSlots.store (1u << active);       // replaced meanwhile: next block
        }

        // Spectrum of the last two input blocks goes to the front of the delay line.
        fdlPos = (fdlPos + kParts - 1) % kParts;
        for (int c = 0; c < numCh; ++c)
        {
            std::copy (in[c].begin(), in[c].end(), work.begin());
            blockFft->performRealOnlyForwardTransform (work.data(), true);
            float* re = fdlRe[c].data() + fdlPos * bins;
            float* im = fdlIm[c].data() + fdlPos * bins;
            for (int k = 0; k < bins; ++k) { re[k] = work[(size_t) (2 * k)]; im[k] = work[(size_t) (2 * k + 1)]; }
            std::copy (in[c].begin() + B, in[c].end(), in[c].begin());
        }

        render (slots[(size_t) active], numCh, out);
        if (previous != active)
        {
            render (slots[(size_t) previous], numCh, fade);
            for (int c = 0; c < numCh; ++c)
                for (int i = 0; i < B; ++i)
                {
                    const float g = (float) (i + 1) / (float) B;
                    out[c][(size_t) i] = fade[c][(size_t) i] + g * (out[c][(size_t) i] - fade[c][(size_t) i]);
                }
            audioSlots.store (1u << active);
        }
    }

    // One output block per channel: sum over partitions of (input spectrum p
    // blocks ago) x (kernel partition p), back to time; the second half of the
    // transform is the alias-free part (overlap-save).
    void render (const KernelSet& ks, int numCh, std::vector<float>* dest) noexcept
    {
        for (int o = 0; o < numCh; ++o)
        {
            std::fill (accRe.begin(), accRe.end(), 0.0f);
            std::fill (accIm.begin(), accIm.end(), 0.0f);
            for (int i = 0; i < numCh; ++i)
            {
                if (i != o && ! ks.cross) continue;
                const int kernel = i == o ? o : (o == 0 ? 2 : 3);
                for (int p = 0; p < kParts; ++p)
                {
                    const size_t x = (size_t) (((fdlPos + p) % kParts) * bins);
                    const size_t h = (size_t) ((kernel * kParts + p) * bins);
                    const float* xr = fdlRe[i].data() + x; const float* xi = fdlIm[i].data() + x;
                    const float* hr = ks.re.data() + h;    const float* hi = ks.im.data() + h;
                    float* ar = accRe.data(); float* ai = accIm.data();
                    for (int k = 0; k < bins; ++k)
                    {
                        ar[k] += xr[k] * hr[k] - xi[k] * hi[k];
                        ai[k] += xr[k] * hi[k] + xi[k] * hr[k];
                    }
                }
            }
            for (int k = 0; k < bins; ++k) { work[(size_t) (2 * k)] = accRe[(size_t) k]; work[(size_t) (2 * k + 1)] = accIm[(size_t) k]; }
            blockFft->performRealOnlyInverseTransform (work.data());
            std::copy (work.begin() + B, work.begin() + 2 * B, dest[o].begin());
        }
    }

    // ---- design thread -------------------------------------------------------
    // The four zero-phase responses on the design grid: mag[0] L->L, mag[1] R->L,
    // mag[2] L->R, mag[3] R->R.
    void computeResponse (const BandArray& bands)
    {
        const int half = designSize / 2;
        for (int k = 0; k <= half; ++k)
        {
            const double f = fs * (double) k / (double) designSize;
            double ll = 1.0, lr = 0.0, rl = 0.0, rr = 1.0;      // lr: from R into L
            for (auto& b : bands)
            {
                if (! b.on) continue;
                double h = analogMagnitude (b, f);
                h = std::isfinite (h) ? juce::jlimit (0.0, 1.0e4, h) : 1.0;
                auto chan = b.chan;
                if (isMono) { if (chan == ChannelMode::Side) continue; chan = ChannelMode::Stereo; }
                switch (chan)
                {
                    case ChannelMode::Stereo: ll *= h; lr *= h; rl *= h; rr *= h; break;
                    case ChannelMode::Left:   ll *= h; lr *= h; break;
                    case ChannelMode::Right:  rl *= h; rr *= h; break;
                    case ChannelMode::Mid:
                    case ChannelMode::Side:
                    {
                        // L' = u L + v R, R' = v L + u R
                        const double u = 0.5 * (h + 1.0);
                        const double v = chan == ChannelMode::Mid ? 0.5 * (h - 1.0) : 0.5 * (1.0 - h);
                        const double a = u * ll + v * rl, c = u * lr + v * rr;
                        rl = v * ll + u * rl; rr = v * lr + u * rr;
                        ll = a; lr = c;
                        break;
                    }
                }
            }
            mag[0][(size_t) k] = (float) ll; mag[1][(size_t) k] = (float) lr;
            mag[2][(size_t) k] = (float) rl; mag[3][(size_t) k] = (float) rr;
        }
    }

    // Zero-phase response -> symmetric windowed FIR -> partition spectra.
    void makeKernel (KernelSet& ks, int kernel, const std::vector<float>& response)
    {
        const int half = designSize / 2;
        std::fill (spec.begin(), spec.end(), 0.0f);
        for (int k = 0; k <= half; ++k) spec[(size_t) (2 * k)] = response[(size_t) k];   // real: zero phase
        designFft->performRealOnlyInverseTransform (spec.data());
        // spec now holds the impulse response, even around time 0 (wrapping).

        for (int p = 0; p < kParts; ++p)
        {
            std::fill (partWork.begin(), partWork.end(), 0.0f);
            for (int j = 0; j < B; ++j)
            {
                const int tap = p * B + j;
                const int lag = tap - centre;
                partWork[(size_t) j] = spec[(size_t) (lag < 0 ? lag + designSize : lag)] * window[(size_t) tap];
            }
            partFft->performRealOnlyForwardTransform (partWork.data(), true);
            float* re = ks.re.data() + (kernel * kParts + p) * bins;
            float* im = ks.im.data() + (kernel * kParts + p) * bins;
            for (int k = 0; k < bins; ++k) { re[k] = partWork[(size_t) (2 * k)]; im[k] = partWork[(size_t) (2 * k + 1)]; }
        }
    }

    double fs = 44100.0;
    bool isMono = false;
    int fullLen = 8192, B = 256, bins = 257, centre = 3840, designSize = 16384;

    std::array<KernelSet, kSlots> slots;
    std::atomic<int> pending { -1 };            // designed slot waiting for the audio thread
    std::atomic<unsigned> audioSlots { 1u };    // bit per slot the audio thread may be reading
    int active = 0;                             // audio thread
    int lastPublished = 0;                      // design thread

    // audio thread
    std::unique_ptr<juce::dsp::FFT> blockFft;
    std::vector<float> in[2], out[2], fade[2], fdlRe[2], fdlIm[2], work, accRe, accIm;
    int pos = 0, fdlPos = 0;

    // design thread
    std::unique_ptr<juce::dsp::FFT> partFft, designFft;
    std::vector<float> window, mag[4], spec, partWork;
};

} // namespace wsho::dsp
