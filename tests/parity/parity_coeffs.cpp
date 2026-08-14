// Parity test: the C++ biquad designer vs the Pd patch's own coefficient
// generator, compiled through hvcc.
//
// The C++ port is verified against the original patch rather than against a
// reading of it.  This drives [pd resonator.coeff] with a set of frequency/Q
// pairs, captures the five coefficients it hands to [biquad~], and compares
// them with epmk2::designBandpass().
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

#include "Heavy_Coeffs.hpp"
#include "../../dsp/Biquad.h"

namespace {

// Captured via the send hook rather than [print]: Heavy prints floats at six
// significant figures, which is coarser than the agreement being tested for.
// ff2 (the b1 coefficient) is identically zero for a bandpass, and the patch
// never sends it -- [pack f f f f f] just keeps its initial 0 in that slot.
// So four of the five are expected, not all five.
float g_coeff[5];
bool  g_got[5];

void sendHook(HeavyContextInterface*, const char*, hv_uint32_t hash, const HvMessage* m)
{
    if (!hv_msg_isFloat(m, 0))
        return;
    const float v = hv_msg_getFloat(m, 0);
    switch (hash) {
        case Heavy_Coeffs::Parameter::Out::FB1: g_coeff[0] = v; g_got[0] = true; break;
        case Heavy_Coeffs::Parameter::Out::FB2: g_coeff[1] = v; g_got[1] = true; break;
        case Heavy_Coeffs::Parameter::Out::FF1: g_coeff[2] = v; g_got[2] = true; break;
        case Heavy_Coeffs::Parameter::Out::FF2: g_coeff[3] = v; g_got[3] = true; break;
        case Heavy_Coeffs::Parameter::Out::FF3: g_coeff[4] = v; g_got[4] = true; break;
        default: break;
    }
}

struct Case { double freq, q; };

} // namespace

int main()
{
    const double sampleRate = 48000.0;

    // Frequencies and Qs spanning what the instrument actually uses: the tone
    // bar sits near the fundamental at high Q, the tine resonators at 7.1x and
    // 20.4x with a much lower Q.
    const std::vector<Case> cases = {
        {  27.5,  1642.18 },   // A0 tone bar
        { 220.0,  1642.18 },   // A3 tone bar
        { 440.0,  1642.18 },
        { 1760.0, 1642.18 },
        { 195.0,   225.0  },   // A0 tine, 7.1x
        { 561.0,   225.0  },   // A0 tine, 20.4x
        { 3124.0,  225.0  },   // A3 tine, 7.1x
        { 8976.0,  225.0  },   // A3 tine, 20.4x
        {  10.0,   225.0  },   // below the patch's 20 Hz clip
        { 25000.0, 225.0  },   // above the patch's 20000 Hz clip
    };

    Heavy_Coeffs ctx(sampleRate);
    ctx.setSendHook(&sendHook);

    float outL[64], outR[64];
    float* outs[2] = { outL, outR };

    int failures = 0;
    // Both sides do the same arithmetic in double and store the result as
    // float, so agreement should be at float epsilon.
    const float tolerance = 1.0e-6f;

    printf("%9s %9s  %-11s %-11s %-11s %-11s %-11s  %s\n",
           "freq", "Q", "fb1", "fb2", "ff1", "ff2", "ff3", "max err");
    printf("%s\n", std::string(96, '-').c_str());

    for (const Case& c : cases) {
        // Q first: it lands on a cold inlet, so it has to be set before the
        // frequency triggers the calculation.
        ctx.sendFloatToReceiver(Heavy_Coeffs::Parameter::In::Q, (float)c.q);
        ctx.sendFloatToReceiver(Heavy_Coeffs::Parameter::In::FREQ, (float)c.freq);
        std::memset(g_got, 0, sizeof(g_got));
        g_coeff[3] = 0.0f;
        for (int i = 0; i < 4; ++i)
            ctx.process(nullptr, outs, 64);

        if (!(g_got[0] && g_got[1] && g_got[2] && g_got[4])) {
            printf("%9.1f %9.2f  <patch produced no coefficients>\n", c.freq, c.q);
            ++failures;
            continue;
        }

        const epmk2::BiquadCoeffs mine =
            epmk2::designBandpass(c.freq, c.q, sampleRate);
        const float theirs[5] = { g_coeff[0], g_coeff[1], g_coeff[2],
                                  g_coeff[3], g_coeff[4] };
        const float ours[5]   = { mine.fb1, mine.fb2, mine.ff1, mine.ff2, mine.ff3 };

        float maxErr = 0.0f;
        for (int i = 0; i < 5; ++i)
            maxErr = std::max(maxErr, std::fabs(theirs[i] - ours[i]));

        printf("%9.1f %9.2f  %-11.7f %-11.7f %-11.7f %-11.7f %-11.7f  %.2e%s\n",
               c.freq, c.q, theirs[0], theirs[1], theirs[2], theirs[3], theirs[4],
               maxErr, maxErr > tolerance ? "   MISMATCH" : "");

        if (maxErr > tolerance)
            ++failures;
    }

    printf("\n%s (%zu cases, tolerance %.0e)\n",
           failures ? "FAILED" : "all coefficients match the Pd patch",
           cases.size(), (double)tolerance);
    return failures ? 1 : 0;
}
