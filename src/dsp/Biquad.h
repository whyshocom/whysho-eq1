#pragma once
#include <cmath>
#include <array>
#include <algorithm>

#ifndef M_PI
 #define M_PI 3.14159265358979323846
#endif

namespace wsho::dsp { namespace detail {
    inline double wmax (double a, double b) { return a > b ? a : b; }
    inline double wclamp (double lo, double hi, double v) { return std::min (std::max (v, lo), hi); }
} }

// ===========================================================================
//  Transposed Direct Form II biquad + RBJ/Butterworth coefficient builders.
//  Coefficients are stored normalized (a0 == 1). Up to 8 cascaded sections
//  per band support slopes to 96 dB/oct.
// ===========================================================================
namespace wsho::dsp
{
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    inline void setBypass() { b0 = 1.0; b1 = b2 = a1 = a2 = 0.0; }
};

struct BiquadState
{
    double z1 = 0.0, z2 = 0.0;
    inline void reset() { z1 = z2 = 0.0; }
    // Transposed Direct Form II — numerically robust for audio EQ.
    inline float process (float in, const BiquadCoeffs& c) noexcept
    {
        const double x = (double) in;
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }
};

static constexpr int kMaxSections = 8;

// --- RBJ cookbook single-biquad builders -----------------------------------
inline BiquadCoeffs makeBell (double fs, double f, double gainDb, double Q)
{
    const double A  = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * M_PI * f / fs;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * wsho::dsp::detail::wmax (Q, 0.025));
    const double a0 = 1.0 + alpha / A;
    BiquadCoeffs c;
    c.b0 = (1.0 + alpha * A) / a0;
    c.b1 = (-2.0 * cw) / a0;
    c.b2 = (1.0 - alpha * A) / a0;
    c.a1 = (-2.0 * cw) / a0;
    c.a2 = (1.0 - alpha / A) / a0;
    return c;
}

inline BiquadCoeffs makeShelf (double fs, double f, double gainDb, double Q, bool high)
{
    const double A  = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * M_PI * f / fs;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / wsho::dsp::detail::wmax (Q, 0.1) - 1.0) + 2.0);
    const double tsa = 2.0 * std::sqrt (A) * alpha;
    BiquadCoeffs c; double a0;
    if (high)
    {
        a0   =        (A + 1.0) - (A - 1.0) * cw + tsa;
        c.b0 =  A * ((A + 1.0) + (A - 1.0) * cw + tsa) / a0;
        c.b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw) / a0;
        c.b2 =  A * ((A + 1.0) + (A - 1.0) * cw - tsa) / a0;
        c.a1 =  2.0 * ((A - 1.0) - (A + 1.0) * cw) / a0;
        c.a2 =        ((A + 1.0) - (A - 1.0) * cw - tsa) / a0;
    }
    else
    {
        a0   =        (A + 1.0) + (A - 1.0) * cw + tsa;
        c.b0 =  A * ((A + 1.0) - (A - 1.0) * cw + tsa) / a0;
        c.b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cw) / a0;
        c.b2 =  A * ((A + 1.0) - (A - 1.0) * cw - tsa) / a0;
        c.a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw) / a0;
        c.a2 =        ((A + 1.0) + (A - 1.0) * cw - tsa) / a0;
    }
    return c;
}

inline BiquadCoeffs makeNotch (double fs, double f, double Q)
{
    const double w0 = 2.0 * M_PI * f / fs;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * wsho::dsp::detail::wmax (Q, 0.1));
    const double a0 = 1.0 + alpha;
    BiquadCoeffs c;
    c.b0 = 1.0 / a0; c.b1 = (-2.0 * cw) / a0; c.b2 = 1.0 / a0;
    c.a1 = (-2.0 * cw) / a0; c.a2 = (1.0 - alpha) / a0;
    return c;
}

inline BiquadCoeffs makeBandPass (double fs, double f, double Q)
{
    const double w0 = 2.0 * M_PI * f / fs;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * wsho::dsp::detail::wmax (Q, 0.1));
    const double a0 = 1.0 + alpha;
    BiquadCoeffs c;
    c.b0 = alpha / a0; c.b1 = 0.0; c.b2 = -alpha / a0;
    c.a1 = (-2.0 * cw) / a0; c.a2 = (1.0 - alpha) / a0;
    return c;
}

inline BiquadCoeffs makeAllPass (double fs, double f, double Q)
{
    const double w0 = 2.0 * M_PI * f / fs;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * wsho::dsp::detail::wmax (Q, 0.1));
    const double a0 = 1.0 + alpha;
    BiquadCoeffs c;
    c.b0 = (1.0 - alpha) / a0; c.b1 = (-2.0 * cw) / a0; c.b2 = (1.0 + alpha) / a0;
    c.a1 = (-2.0 * cw) / a0; c.a2 = (1.0 - alpha) / a0;
    return c;
}

// Second-order low/high pass with explicit Q (used as one Butterworth stage).
inline BiquadCoeffs makePass (double fs, double f, double Q, bool high)
{
    const double w0 = 2.0 * M_PI * wsho::dsp::detail::wclamp (10.0, fs * 0.49, f) / fs;
    const double cw = std::cos (w0), sw = std::sin (w0);
    const double alpha = sw / (2.0 * wsho::dsp::detail::wmax (Q, 0.1));
    const double a0 = 1.0 + alpha;
    BiquadCoeffs c;
    if (high)
    {
        c.b0 = (1.0 + cw) / 2.0 / a0; c.b1 = -(1.0 + cw) / a0; c.b2 = (1.0 + cw) / 2.0 / a0;
    }
    else
    {
        c.b0 = (1.0 - cw) / 2.0 / a0; c.b1 = (1.0 - cw) / a0; c.b2 = (1.0 - cw) / 2.0 / a0;
    }
    c.a1 = (-2.0 * cw) / a0; c.a2 = (1.0 - alpha) / a0;
    return c;
}

// First-order low/high pass (for odd Butterworth orders).
inline BiquadCoeffs makeOnePole (double fs, double f, bool high)
{
    const double w0 = 2.0 * M_PI * wsho::dsp::detail::wclamp (10.0, fs * 0.49, f) / fs;
    const double a  = std::exp (-w0);
    BiquadCoeffs c;
    if (high) { c.b0 = (1.0 + a) / 2.0; c.b1 = -(1.0 + a) / 2.0; c.b2 = 0.0; c.a1 = -a; c.a2 = 0.0; }
    else      { c.b0 = 1.0 - a;         c.b1 = 0.0;              c.b2 = 0.0; c.a1 = -a; c.a2 = 0.0; }
    return c;
}

} // namespace wsho::dsp
