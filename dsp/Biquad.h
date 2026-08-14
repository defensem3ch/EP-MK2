// Biquad filter, in Pure Data's coefficient convention.
//
// The Pd original this port replaces drives [biquad~] directly from coefficients
// computed in the patch, so matching Pd's sign convention exactly is what lets
// the C++ be diffed against the Heavy build sample-for-sample.
//
//   Pd:  y[n] = ff1*x[n] + ff2*x[n-1] + ff3*x[n-2] + fb1*y[n-1] + fb2*y[n-2]
//
// Note the feedback terms are *added*, i.e. fb1/fb2 are already negated
// relative to the usual a1/a2 of the RBJ cookbook.
#pragma once

#include <cmath>

namespace epmk2 {

struct BiquadCoeffs {
    float ff1 = 1.0f, ff2 = 0.0f, ff3 = 0.0f;   // feed-forward  (b0, b1, b2)
    float fb1 = 0.0f, fb2 = 0.0f;               // feedback      (-a1, -a2)
};

class Biquad {
public:
    void setCoeffs(const BiquadCoeffs& c) noexcept { coeffs = c; }

    void reset() noexcept { x1 = x2 = y1 = y2 = 0.0f; }

    inline float process(float x) noexcept
    {
        const float y = coeffs.ff1 * x
                      + coeffs.ff2 * x1
                      + coeffs.ff3 * x2
                      + coeffs.fb1 * y1
                      + coeffs.fb2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

private:
    BiquadCoeffs coeffs;
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
};

// ---------------------------------------------------------------------------
// Coefficient designers, following the Audio EQ Cookbook the Pd patch cites.

// Bandpass with constant skirt gain (peak gain = Q).  This is what the
// patch's [pd resonator.coeff] computes, and it is the core of both the tine
// and tone bar resonators.
inline BiquadCoeffs designBandpass(double freqHz, double q, double sampleRate) noexcept
{
    // The patch clips resonator frequencies to a sane audio range before use.
    if (freqHz < 20.0)    freqHz = 20.0;
    if (freqHz > 20000.0) freqHz = 20000.0;
    if (q < 1.0e-6)       q = 1.0e-6;

    const double w0    = 2.0 * M_PI * freqHz / sampleRate;
    const double sinw0 = std::sin(w0);
    const double cosw0 = std::cos(w0);
    const double alpha = sinw0 / (2.0 * q);

    const double b0 =  sinw0 * 0.5;
    const double b1 =  0.0;
    const double b2 = -sinw0 * 0.5;
    const double a0 =  1.0 + alpha;
    const double a1 = -2.0 * cosw0;
    const double a2 =  1.0 - alpha;

    BiquadCoeffs c;
    c.ff1 = float(b0 / a0);
    c.ff2 = float(b1 / a0);
    c.ff3 = float(b2 / a0);
    c.fb1 = float(-a1 / a0);
    c.fb2 = float(-a2 / a0);
    return c;
}

namespace detail {

// Shared front half of the cookbook designs.  The patch clips frequency to
// [20, Nyquist] and floors Q at the smallest positive float before use.
struct Cookbook {
    double sinw0, cosw0, alpha, a0, a1, a2;

    Cookbook(double freqHz, double q, double sampleRate) noexcept
    {
        const double nyquist = sampleRate * 0.5;
        if (freqHz < 20.0)      freqHz = 20.0;
        if (freqHz > nyquist)   freqHz = nyquist;
        if (q < 5.96046e-08)    q = 5.96046e-08;

        const double w0 = 2.0 * M_PI * freqHz / sampleRate;
        sinw0 = std::sin(w0);
        cosw0 = std::cos(w0);
        alpha = sinw0 / (2.0 * q);
        a0    = 1.0 + alpha;
        a1    = -2.0 * cosw0;
        a2    = 1.0 - alpha;
    }

    BiquadCoeffs normalise(double b0, double b1, double b2) const noexcept
    {
        BiquadCoeffs c;
        c.ff1 = float(b0 / a0);
        c.ff2 = float(b1 / a0);
        c.ff3 = float(b2 / a0);
        c.fb1 = float(-a1 / a0);
        c.fb2 = float(-a2 / a0);
        return c;
    }
};

} // namespace detail

// [pd lowpass~] in the original voice.
inline BiquadCoeffs designLowpass(double freqHz, double q, double sampleRate) noexcept
{
    const detail::Cookbook k(freqHz, q, sampleRate);
    const double b1 = 1.0 - k.cosw0;
    return k.normalise(b1 * 0.5, b1, b1 * 0.5);
}

// [pd highpass~] in the original voice.
inline BiquadCoeffs designHighpass(double freqHz, double q, double sampleRate) noexcept
{
    const detail::Cookbook k(freqHz, q, sampleRate);
    const double b1 = 1.0 + k.cosw0;
    return k.normalise(b1 * 0.5, -b1, b1 * 0.5);
}

// One-pole lowpass, matching Pd's [lop~]:
//   y[n] = y[n-1] + c * (x[n] - y[n-1]),  c = clamp(2*pi*f/sr, 0, 1)
class OnePole {
public:
    void setCutoff(double freqHz, double sampleRate) noexcept
    {
        double c = 2.0 * M_PI * freqHz / sampleRate;
        if (c < 0.0) c = 0.0;
        if (c > 1.0) c = 1.0;
        coeff = float(c);
    }

    void reset() noexcept { y1 = 0.0f; }

    inline float process(float x) noexcept
    {
        y1 += coeff * (x - y1);
        return y1;
    }

private:
    float coeff = 1.0f, y1 = 0.0f;
};

} // namespace epmk2
