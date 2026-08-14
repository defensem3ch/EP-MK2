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
class PickupShaper {
public:
    // 1024 entries is 4 KB: small enough to stay in L1.  One instance is
    // shared by every voice, since the symmetry control is global -- giving
    // each voice its own copy costs more in cache misses than the exp2 did.
    static constexpr int kSize = 1024;

    void setSymmetry(float s) noexcept
    {
        if (s == symmetry && built)
            return;
        symmetry = s;
        built = true;

        const float den = std::exp2(s);
        const float invDen = den > 0.0f ? 1.0f / den : 0.0f;

        for (int i = 0; i <= kSize; ++i) {
            const float x = -1.0f + 2.0f * float(i) / float(kSize);
            curve[i] = (std::exp2(s * x) - 1.0f) * invDen;
        }
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
    float symmetry = -1.0e9f;
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
