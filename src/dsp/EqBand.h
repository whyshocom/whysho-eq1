#pragma once
#include "Biquad.h"
#include "../Parameters.h"
#include <complex>

// ===========================================================================
//  A single EQ band: a cascade of biquads (for slope), channel routing
//  (Stereo/L/R/M/S) and optional dynamic behaviour driven by a band-localized
//  detector (internal or sidechain).
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
        const bool gainType = (p.type == FilterType::Bell || p.type == FilterType::LowShelf
                               || p.type == FilterType::HighShelf || p.type == FilterType::TiltShelf);

        int i = 0;
        while (i < n)
        {
            const int chunk = std::min (kCtrl, n - i);

            if (p.dyn && gainType)
                updateDynamic (L + i, R + i, scL ? scL + i : nullptr, scR ? scR + i : nullptr, chunk);

            for (int k = 0; k < chunk; ++k)
            {
                const int idx = i + k;
                const float l = L[idx], r = R[idx];
                switch (p.chan)
                {
                    case ChannelMode::Stereo:
                        L[idx] = runLane (0, l); R[idx] = runLane (1, r); break;
                    case ChannelMode::Left:
                        L[idx] = runLane (0, l); break;
                    case ChannelMode::Right:
                        R[idx] = runLane (1, r); break;
                    case ChannelMode::Mid: {
                        const float m = 0.5f * (l + r), s = 0.5f * (l - r);
                        const float mf = runLane (0, m);
                        L[idx] = mf + s; R[idx] = mf - s; break; }
                    case ChannelMode::Side: {
                        const float m = 0.5f * (l + r), s = 0.5f * (l - r);
                        const float sf = runLane (1, s);
                        L[idx] = m + sf; R[idx] = m - sf; break; }
                }
            }
            i += chunk;
        }
    }

    float currentDynGainDb() const { return dynGainDb; }

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

    inline float runLane (int lane, float x) noexcept
    {
        for (int s = 0; s < numSections; ++s)
            x = state[lane][s].process (x, coeffs[s]);
        return x;
    }

    void updateDynamic (const float* L, const float* R, const float* scL, const float* scR, int n)
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
        float target = 0.0f;
        if (over > 0.0)
        {
            // Map amount above threshold toward the range target (soft knee).
            const double amt = std::min (1.0, over / 24.0);
            target = (float) (p.dynRangeDb * amt);
        }
        // smooth the applied gain toward target (reuse release for recovery)
        const double sc = target < dynGainDb ? atkC : relC;
        dynGainDb = (float) (target + sc * (dynGainDb - target));
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
            const double theta = M_PI * (2.0 * k + 1.0) / (2.0 * order);
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
    int    ctrlCounter = 0;
};

} // namespace wsho::dsp
