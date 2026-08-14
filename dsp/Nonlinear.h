// The nonlinear stages of the pickup, ported from the Pd voice.
#pragma once

#include <cmath>

namespace epmk2 {

// The patch's [pd hv.tanh]: clip to +/-3, then a Pade approximant of tanh.
// Kept rather than std::tanh so the port matches the original bit for bit;
// the two differ by up to ~2e-3 in the middle of the range, which is audible
// once it is inside a feedback-free waveshaper driven this hard.
inline float tanhApprox(float x) noexcept
{
    if (x < -3.0f) x = -3.0f;
    if (x >  3.0f) x =  3.0f;
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

// Pd's [pow~] returns 0 for a non-positive base rather than NaN.
inline float powPd(float base, float exponent) noexcept
{
    return base <= 0.0f ? 0.0f : std::pow(base, exponent);
}

// The pickup's asymmetric transfer function.  In the patch this is built from
// three [pow~] objects:
//
//     y = (2^(s*x) - 1) / 2^s
//
// with x the saturated input and s the "symmetry" control in dB-ish units.
// At s -> 0 it approaches a straight line; larger s bends the curve so
// positive and negative excursions are treated differently, which is what
// produces the even harmonics the real pickup generates as the tine moves
// off its axis.
//
// The denominator depends only on the parameter, so it is hoisted out of the
// sample loop, and the numerator uses exp2 rather than a general pow.  In the
// Pd original all three were per-sample [pow~] objects, which is most of what
// a voice costs.
// Evaluating 2^(s*x) per sample was, by profiling, about 30% of a voice --
// the largest single cost.  It does not need to be: the input has already been
// through tanh, which bounds it to exactly [-1, 1], and s only changes when
// the player moves a control.  So the whole curve is tabulated over that range
// whenever the parameter changes, and the audio path is a table lookup with
// linear interpolation.
// The pickup, modelled from its geometry rather than from a symmetry knob.
//
// A Rhodes pickup is a magnet and coil facing the tine.  The flux linking the
// coil depends on how far away the tine is, and the coil senses dPhi/dt, not
// Phi.  Two things follow, and the model needs both:
//
//   * The nonlinearity is the shape of Phi(x), which is set by where the tine
//     sits relative to the pole -- its distance, and its offset off the
//     magnetic axis.  A tine centred on the axis (offset 0) gives a purely
//     even response: no fundamental, only the octave.  Real pickups are
//     deliberately offset, and how far sets the asymmetry that used to be dialled
//     in by hand as "symmetry".
//   * Sensing dPhi/dt is a differentiator, so the pickup has an inherent
//     +6 dB/octave tilt.  That is missing from a static waveshaper, and its
//     absence is why output fell ~20 dB from the bottom of the keyboard to the
//     top (roadmap 1.6).  The differentiation happens in Voice; this class
//     supplies Phi(x).
//
// Tabulated and shared across voices, as before: the geometry is global, and
// giving every voice its own copy costs more in cache misses than it saves.
class PickupShaper {
public:
    static constexpr int kSize = 1024;

    // `distance` is the tine's rest gap from the pole and `offset` how far it
    // sits off the magnetic axis, both in units of the tine's vibration
    // amplitude -- so the interesting range for each is around 1.
    void setGeometry(float distance, float offset) noexcept
    {
        if (built && distance == lastDistance && offset == lastOffset)
            return;
        lastDistance = distance;
        lastOffset = offset;
        built = true;

        const float d = distance < 0.05f ? 0.05f : distance;
        const float d2 = d * d;

        float lo = 1.0e30f, hi = -1.0e30f;
        for (int i = 0; i <= kSize; ++i) {
            const float x = -1.0f + 2.0f * float(i) / float(kSize);
            const float u = offset + x;
            // Dipole falloff: flux ~ 1 / r^3.
            const float v = std::pow(d2 + u * u, -1.5f);
            curve[i] = v;
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }

        // Normalise the swing to unity so moving the pickup changes the shape
        // of the response -- which harmonics appear -- without also acting as a
        // large and very nonlinear volume control.  Flux falls off as 1/r^3, so
        // without this, halving the distance would raise the level by 18 dB.
        const float span = hi - lo;
        const float inv = span > 1.0e-20f ? 1.0f / span : 0.0f;
        for (int i = 0; i <= kSize; ++i)
            curve[i] = (curve[i] - lo) * inv;
    }

    inline float process(float x) const noexcept
    {
        // tanhApprox() clips to [-1, 1], so this cannot leave the table; the
        // clamp is here only so a future change upstream cannot corrupt memory.
        if (x <= -1.0f) return curve[0];
        if (x >=  1.0f) return curve[kSize];

        const float pos = (x + 1.0f) * (0.5f * float(kSize));
        const int   i   = int(pos);
        const float f   = pos - float(i);
        return curve[i] + f * (curve[i + 1] - curve[i]);
    }

private:
    float lastDistance = -1.0e9f, lastOffset = -1.0e9f;
    bool  built = false;
    float curve[kSize + 1] = {};
};

// The buzz stage raises the shaped signal to the fourth power.  Pd's [pow~]
// yields 0 for a negative base, so the sign test has to stay -- but the power
// itself is two multiplies rather than a call to pow().
inline float buzzFourth(float x) noexcept
{
    if (x <= 0.0f)
        return 0.0f;
    const float x2 = x * x;
    return x2 * x2;
}

} // namespace epmk2
