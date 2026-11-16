#pragma once
#include "Biquad.h"
#include "../Parameters.h"
#include <complex>

// ===========================================================================
//  A single EQ band: a cascade of biquads (for slope), channel routing
//  (Stereo/L/R/M/S) and optional dynamic behaviour driven by a band-localized
//  detector (internal or sidechain).
//
//  Dynamics rule. The detector measures the level around the band frequency;
//  "amount" is how far that level is above the threshold: 0 at or below it,
//  rising linearly to 1 at 24 dB above (kDynKneeDb).
//   - Bands with a gain (Bell, shelves, Tilt): Range * amount (dB) is added to
//     the band gain, so the band boosts or cuts more as it is driven.
//   - Bands without a gain (High/Low Pass, Notch, Band Pass, All Pass): the
//     filter is blended in and out, out = x + a * (filtered - x), a in 0..1.
//     depth = min (1, |Range| / 24 dB)   (24 dB is the end of the Range knob)
//       Range <  0 : the filter engages when driven        a = amount * depth
//       Range >= 0 : the filter is on at rest and backs
//                    off when driven                        a = 1 - amount * depth
//     `a` is ramped per sample between control updates, so it never clicks.
// ===========================================================================
namespace wsho::dsp
{
struct BandParams
{
    bool  on = false;
    FilterType type = FilterType::Bell;
    float freq = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.707f;
    int   order = 2;            // from slope
    ChannelMode chan = ChannelMode::Stereo;
    bool  solo = false;

    bool  dyn = false;
    float dynThreshDb = -24.0f;
    float dynRangeDb = -6.0f;   // target gain change when driven
    float dynAttackMs = 10.0f;
    float dynReleaseMs = 120.0f;
    bool  dynSidechain = false;
};

inline bool hasGain (FilterType t)
{
    return t == FilterType::Bell || t == FilterType::LowShelf || t == FilterType::HighShelf || t == FilterType::TiltShelf;
}

// Magnitude of the band's analog prototype at freqHz: the curve the biquads
// approximate, free of the squeeze they get near Nyquist. The linear-phase
// kernel is designed from this (dynamics are not part of it).
inline double analogMagnitude (const BandParams& p, double freqHz)
{
    using cd = std::complex<double>;
    const double x = freqHz / std::max (10.0, (double) p.freq);     // frequency relative to the band's
    const cd s (0.0, x), s2 = s * s;
    const double A = std::pow (10.0, p.gainDb / 40.0);
    const double q = std::max (p.type == FilterType::Bell ? 0.025 : 0.1, (double) p.q);
    const auto lowShelf  = [&] (double a) { const double k = std::sqrt (a) / q; return a * (s2 + k * s + a) / (a * s2 + k * s + 1.0); };
    const auto highShelf = [&] (double a) { const double k = std::sqrt (a) / q; return a * (a * s2 + k * s + 1.0) / (s2 + k * s + a); };

    switch (p.type)
    {
        case FilterType::Bell:      return std::abs ((s2 + s * (A / q) + 1.0) / (s2 + s / (A * q) + 1.0));
        case FilterType::LowShelf:  return std::abs (lowShelf (A));
        case FilterType::HighShelf: return std::abs (highShelf (A));
        case FilterType::TiltShelf: return std::abs (lowShelf (1.0 / A) * highShelf (A));
        case FilterType::Notch:     return std::abs ((s2 + 1.0) / (s2 + s / q + 1.0));
        case FilterType::BandPass:  return std::abs ((s / q) / (s2 + s / q + 1.0));
        case FilterType::AllPass:   return 1.0;
        case FilterType::HighPass:
        case FilterType::LowPass:
        {
            // Butterworth of order N: |H|^2 = 1 / (1 + x^2N), mirrored for high pass.
            if (x <= 0.0) return p.type == FilterType::LowPass ? 1.0 : 0.0;
            const int order = std::max (1, std::min (16, p.order));
            const double xn = std::pow (p.type == FilterType::LowPass ? x : 1.0 / x, 2.0 * order);
            return 1.0 / std::sqrt (1.0 + xn);
        }
    }
    return 1.0;
}

class EqBand
{
public:
    void prepare (double sampleRate)
    {
        fs = sampleRate;
        reset();
    }

    void reset()
    {
        for (auto& lane : state) for (auto& s : lane) s.reset();
        detState.reset();
        env = 0.0;
        dynGainDb = 0.0f;
        dynAmt = 0.0f;
        blend = -1.0f;              // snap to the target on the next block
        ctrlCounter = 0;
    }

    // Build the static (non-dynamic) coefficient cascade for current params.
    void updateCoefficients (const BandParams& bp)
    {
        p = bp;
        buildCascade (p.gainDb + (p.dyn ? dynGainDb : 0.0f));
        // Detector band-pass for dynamic mode (localized energy around freq).
        if (p.dyn)
            detCoeffs = makeBandPass (fs, p.freq, 1.2);
        // attack/release one-pole coefficients
        atkC = std::exp (-1.0 / (0.001 * std::max (0.1f, p.dynAttackMs) * fs));
        relC = std::exp (-1.0 / (0.001 * std::max (1.0f,  p.dynReleaseMs) * fs));
    }

    bool isActive() const { return p.on; }
    const BandParams& params() const { return p; }

    // Process a stereo block in place. scL/scR may be nullptr.
    void process (float* L, float* R, int n, const float* scL, const float* scR)
    {
        if (! p.on) return;
        const bool gainType = hasGain (p.type);

        int i = 0;
        while (i < n)
        {
            const int chunk = std::min (kCtrl, n - i);

            if (p.dyn)
                updateDynamic (L + i, R + i, scL ? scL + i : nullptr, scR ? scR + i : nullptr, chunk, gainType);

            // Filter blend for this chunk (see the dynamics rule at the top):
            // always 1 unless this is a dynamic band without a gain.
            float target = 1.0f;
            if (p.dyn && ! gainType)
            {
                const float depth = std::min (1.0f, std::abs (p.dynRangeDb) / kDynFullRangeDb);
                target = p.dynRangeDb < 0.0f ? dynAmt * depth : 1.0f - dynAmt * depth;
            }
            if (blend < 0.0f) blend = target;
            const bool  blending = blend < 1.0f || target < 1.0f;
            const float step = (target - blend) / (float) chunk;

            for (int k = 0; k < chunk; ++k)
            {
                const int idx = i + k;
                const float l = L[idx], r = R[idx];
                if (blending) blend += step;
                const float a = blend;
                // x + a * (filtered - x); the plain filter when not blending.
                const auto run = [this, blending, a] (int lane, float x)
                {
                    const float y = runLane (lane, x);
                    return blending ? x + a * (y - x) : y;
                };
                switch (p.chan)
                {
                    case ChannelMode::Stereo:
                        L[idx] = run (0, l); R[idx] = run (1, r); break;
                    case ChannelMode::Left:
                        L[idx] = run (0, l); break;
                    case ChannelMode::Right:
                        R[idx] = run (1, r); break;
                    case ChannelMode::Mid: {
                        const float m = 0.5f * (l + r), s = 0.5f * (l - r);
                        const float mf = run (0, m);
                        L[idx] = mf + s; R[idx] = mf - s; break; }
                    case ChannelMode::Side: {
                        const float m = 0.5f * (l + r), s = 0.5f * (l - r);
                        const float sf = run (1, s);
                        L[idx] = m + sf; R[idx] = m - sf; break; }
                }
            }
            blend = target;     // exact, whatever the float ramp summed to
            i += chunk;
        }
    }

    float currentDynGainDb() const { return dynGainDb; }

    // How far the dynamics are engaged right now: 0 (idle) .. 1 (the whole
    // Range is applied). 0 when the band is off or not dynamic.
    float dynamicAmount() const { return p.on && p.dyn ? dynAmt : 0.0f; }

    // Complex transfer function at normalized angular frequency w (for GUI).
    std::complex<double> response (double w) const
    {
        using cd = std::complex<double>;
        const cd z1 = std::polar (1.0, -w), z2 = z1 * z1;
        cd H (1.0, 0.0);
        for (int s = 0; s < numSections; ++s)
        {
            const auto& c = coeffs[(size_t) s];
            const cd num = c.b0 + c.b1 * z1 + c.b2 * z2;
            const cd den = cd (1.0, 0.0) + c.a1 * z1 + c.a2 * z2;
            H *= num / den;
        }
        return H;
    }

private:
    static constexpr int kCtrl = 32;
    static constexpr double kDynKneeDb = 24.0;          // dB above threshold for the full amount
    static constexpr float  kDynFullRangeDb = 24.0f;    // |Range| that fully engages a filter without a gain

    inline float runLane (int lane, float x) noexcept
    {
        for (int s = 0; s < numSections; ++s)
            x = state[lane][s].process (x, coeffs[s]);
        return x;
    }

    void updateDynamic (const float* L, const float* R, const float* scL, const float* scR, int n, bool gainType)
    {
        // Measure band-localized energy of the detector source over the chunk.
        double peak = 0.0;
        for (int k = 0; k < n; ++k)
        {
            float d;
            if (p.dynSidechain && scL)      d = 0.5f * (scL[k] + (scR ? scR[k] : scL[k]));
            else                            d = 0.5f * (L[k] + R[k]);
            const float bp = detState.process (d, detCoeffs);
            const double rect = std::abs (bp);
            const double c = rect > env ? atkC : relC;
            env = rect + c * (env - rect);
            peak = std::max (peak, env);
        }
        const double db = juceGainToDb (peak);
        const double over = db - p.dynThreshDb;           // how far above threshold
        // Map amount above threshold toward the range target (soft knee).
        // The envelope above already has the attack / release times; smoothing
        // again here made them ~32x slower than the knobs said.
        dynAmt = over > 0.0 ? (float) std::min (1.0, over / kDynKneeDb) : 0.0f;
        if (! gainType) return;                           // blended in process() instead
        dynGainDb = p.dynRangeDb * dynAmt;
        buildCascade (p.gainDb + dynGainDb);
    }

    static double juceGainToDb (double g) { return g > 1.0e-6 ? 20.0 * std::log10 (g) : -120.0; }

    void buildCascade (float gainDb)
    {
        numSections = 0;
        auto push = [this] (const BiquadCoeffs& c) { if (numSections < kMaxSections) coeffs[numSections++] = c; };

        switch (p.type)
        {
            case FilterType::Bell:      push (makeBell (fs, p.freq, gainDb, p.q)); break;
            case FilterType::LowShelf:  push (makeShelf (fs, p.freq, gainDb, p.q, false)); break;
            case FilterType::HighShelf: push (makeShelf (fs, p.freq, gainDb, p.q, true)); break;
            case FilterType::TiltShelf:
                push (makeShelf (fs, p.freq, -gainDb, p.q, false));
                push (makeShelf (fs, p.freq,  gainDb, p.q, true));  break;
            case FilterType::Notch:     push (makeNotch (fs, p.freq, p.q)); break;
            case FilterType::BandPass:  push (makeBandPass (fs, p.freq, p.q)); break;
            case FilterType::AllPass:   push (makeAllPass (fs, p.freq, p.q)); break;
            case FilterType::HighPass:  buildButterworth (true);  break;
            case FilterType::LowPass:   buildButterworth (false); break;
        }
        if (numSections == 0) { coeffs[0].setBypass(); numSections = 1; }
    }

    // Cascade of 2nd-order (+ optional 1st-order) Butterworth sections.
    void buildButterworth (bool high)
    {
        const int order = std::max (1, std::min (16, p.order));
        const int nSecond = order / 2;
        const bool hasFirst = (order % 2) != 0;
        for (int k = 0; k < nSecond; ++k)
        {
            // Butterworth pole angles: even orders sit at odd multiples of pi/2N,
            // odd orders (which also have the real pole below) at multiples of pi/N.
            const double theta = hasFirst ? M_PI * (k + 1.0) / order
                                          : M_PI * (2.0 * k + 1.0) / (2.0 * order);
            const double Q = 1.0 / (2.0 * std::cos (theta));
            if (numSections < kMaxSections) coeffs[numSections++] = makePass (fs, p.freq, Q, high);
        }
        if (hasFirst && numSections < kMaxSections)
            coeffs[numSections++] = makeOnePole (fs, p.freq, high);
    }

    double fs = 44100.0;
    BandParams p;
    std::array<BiquadCoeffs, kMaxSections> coeffs {};
    int numSections = 1;
    std::array<std::array<BiquadState, kMaxSections>, 2> state {};

    // dynamics
    BiquadCoeffs detCoeffs {};
    BiquadState  detState {};
    double env = 0.0, atkC = 0.0, relC = 0.0;
    float  dynGainDb = 0.0f;
    float  dynAmt = 0.0f;       // 0..1, how far above threshold (see the rule at the top)
    float  blend = -1.0f;       // current filter blend `a`; < 0 = not started yet
    int    ctrlCounter = 0;
};

} // namespace wsho::dsp
