// Measure how much each tine mode actually contributes, by rendering one note
// straight from the DSP (no JUCE) and reading the spectrum at each mode
// frequency with a Goertzel filter.
//
//   g++ -O2 -std=c++17 -o probe_modes tests/probe_modes.cpp && ./probe_modes
#include <cmath>
#include <cstdio>
#include <vector>

#include "../dsp/Engine.h"

namespace {

// Energy at one frequency over the whole buffer.
double goertzel(const std::vector<float>& x, double freq, double sr)
{
    const double w = 2.0 * M_PI * freq / sr;
    const double c = 2.0 * std::cos(w);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    for (float v : x) {
        s0 = v + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / x.size();
}

std::vector<float> render(const epmk2::EngineParams& p, int note, double sr, int n)
{
    epmk2::Engine e;
    e.prepare(sr, 8);
    e.noteOn(note, 110, p);
    std::vector<float> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
        out.push_back(e.process(p));
    return out;
}

double dB(double x) { return 20.0 * std::log10(std::max(1.0e-12, x)); }

} // namespace

int main()
{
    const double sr = 48000.0;
    const int n = int(sr);            // one second
    const int note = 45;              // A2, ~110 Hz

    epmk2::EngineParams p;
    const double f0 = p.baseFreq * std::pow(2.0, (note - p.baseNote) / 12.0);

    printf("note %d, f0 %.1f Hz\n\n", note, f0);
    printf("  mode frequencies: 1 = %.0f Hz, 2 = %.0f Hz, 3 = %.0f Hz\n\n",
           f0 * p.voice.tineRatio1, f0 * p.voice.tineRatio2, f0 * p.voice.tineRatio3);

    // Level at each mode frequency, with the whole tine path on and off.
    auto report = [&](const char* label, const epmk2::EngineParams& q) {
        const auto x = render(q, note, sr, n);
        double peak = 0.0;
        for (float v : x) peak = std::max(peak, (double)std::fabs(v));
        printf("  %-22s peak %7.4f | f0 %7.1f dB | m1 %7.1f dB | m2 %7.1f dB | m3 %7.1f dB\n",
               label, peak,
               dB(goertzel(x, f0, sr)),
               dB(goertzel(x, f0 * q.voice.tineRatio1, sr)),
               dB(goertzel(x, f0 * q.voice.tineRatio2, sr)),
               dB(goertzel(x, f0 * q.voice.tineRatio3, sr)));
    };

    report("defaults", p);

    // The engine ends in tanh(), so a peak of 1.0 tells us nothing except that
    // it saturated.  Render with master far down and scale back up to see the
    // level actually arriving at the limiter, and which stage produced it.
    {
        auto peakPre = [&](const char* label, epmk2::EngineParams q) {
            q.masterLin = 1.0e-4f;
            const auto x = render(q, note, sr, n);
            double pk = 0.0;
            for (float v : x) pk = std::max(pk, (double)std::fabs(v));
            printf("  %-30s pre-limiter peak %10.2f (%.1f dB)\n",
                   label, pk * 1.0e4, dB(pk * 1.0e4));
        };
        printf("\n");
        peakPre("everything", p);
        { auto q = p; q.voice.tineLevelLin   = 1.0e-6f; peakPre("without tine sum", q); }
        { auto q = p; q.voice.toneLevelLin   = 1.0e-6f; peakPre("without tone bar sum", q); }
        { auto q = p; q.voice.pickupLevelLin = 1.0e-6f; q.voice.buzzLevelLin = 1.0e-6f;
                                                        peakPre("without pickup out", q); }
        { auto q = p; q.voice.hammerLevelLin = 1.0e-9f; peakPre("without hammer direct", q); }
        { auto q = p; q.voice.pickupAttackLin = 1.0e-9f; peakPre("without pickup attack", q); }
        printf("\n");
    }

    { auto q = p; q.voice.tineLevelLin = 1.0e-5f; report("whole tine path off", q); }
    { auto q = p; q.voice.tineMode3LevelLin = 1.0e-5f; report("mode 3 off", q); }
    { auto q = p; q.voice.tineMode3LevelLin = 1.0f;    report("mode 3 at 0 dB", q); }
    { auto q = p; q.voice.tineMode2LevelLin = 1.0e-5f; report("mode 2 off", q); }

    // How much of the excitation actually reaches each mode?  The strike is a
    // single raised-cosine cycle at the note's own period, so its spectrum
    // rolls off steeply well before 20x or 40x the fundamental.
    printf("\n  excitation spectrum (single raised-cosine cycle at f0):\n");
    const double period = sr / f0;
    std::vector<float> strike;
    strike.reserve(n);
    for (int i = 0; i < n; ++i) {
        const double ramp = 1.0 - i / period;
        strike.push_back(ramp > 0.0
            ? float(std::cos(2.0 * M_PI * (0.75 + 0.5 * ramp))) : 0.0f);
    }
    for (double r : { 1.0, (double)p.voice.tineRatio1,
                      (double)p.voice.tineRatio2, (double)p.voice.tineRatio3 })
        printf("    at %5.1f x f0 (%8.1f Hz): %7.1f dB\n",
               r, f0 * r, dB(goertzel(strike, f0 * r, sr)));

    // --- across the keyboard ----------------------------------------------
    // The trim is one constant, but resonator behaviour varies with pitch, so
    // check the whole range rather than trusting one note.
    printf("\n  across the keyboard (velocity 110):\n");
    printf("    %-6s %9s %8s %10s %9s %9s\n", "note", "f0 Hz", "peak", "pre-limit", "f0 dB", "m1 dB");
    for (int nt : { 21, 33, 45, 57, 69, 81, 93, 105 }) {
        const double fn = p.baseFreq * std::pow(2.0, (nt - p.baseNote) / 12.0);
        const auto x = render(p, nt, sr, n);
        double pk = 0.0;
        for (float v : x) pk = std::max(pk, (double)std::fabs(v));
        auto q = p; q.masterLin = 1.0e-4f;
        const auto xp = render(q, nt, sr, n);
        double pre = 0.0;
        for (float v : xp) pre = std::max(pre, (double)std::fabs(v));
        printf("    %-6d %9.1f %8.4f %10.2f %9.1f %9.1f\n", nt, fn, pk, pre * 1.0e4,
               dB(goertzel(x, fn, sr)), dB(goertzel(x, fn * p.voice.tineRatio1, sr)));
    }

    // --- pickup geometry ---------------------------------------------------
    // Offset is what makes the response asymmetric, so it should govern the
    // even harmonics: at 0 the tine sits on the magnetic axis, the response is
    // purely even, and the fundamental should collapse.
    printf("\n  pickup geometry at A2 (harmonics relative to f0):\n");
    printf("    %-8s %-8s %8s %8s %8s %8s\n",
           "dist", "offset", "f0 dB", "2f0", "3f0", "4f0");
    for (double dist : { 0.8 }) {
        for (double off : { 0.25, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0 }) {
            auto q = p;
            q.voice.pickupDistance = (float)dist;
            q.voice.pickupOffset = (float)off;
            const auto x = render(q, note, sr, n);
            const double h1 = goertzel(x, f0, sr);
            printf("    %-8.2f %-8.2f %8.1f %8.1f %8.1f %8.1f\n", dist, off,
                   dB(h1), dB(goertzel(x, f0*2, sr)) - dB(h1),
                   dB(goertzel(x, f0*3, sr)) - dB(h1),
                   dB(goertzel(x, f0*4, sr)) - dB(h1));
        }
    }

    // --- rebalancing after the excitation change --------------------------
    // The strike now carries ~11x the impulse it did, so every gain calibrated
    // against the old weak pulse is too hot.  Sweep the two that matter and
    // look for a peak near MK1's 0.63 with the modes still ranked under f0.
    printf("\n  rebalance sweep (pickup attack / pickup gain):\n");
    printf("    %-10s %-10s %8s %9s %9s %9s %9s\n",
           "attack dB", "gain dB", "peak", "f0", "m1", "m2", "m3");
    for (double atkDb : { -10.0, -21.0, -31.0, -41.0 }) {
        for (double gainDb : { 15.0, 9.0 }) {
            auto q = p;
            q.voice.pickupAttackLin = (float)std::pow(10.0, atkDb / 20.0);
            q.voice.pickupGainLin   = (float)std::pow(10.0, gainDb / 20.0);
            const auto x = render(q, note, sr, n);
            double peak = 0.0;
            for (float v : x) peak = std::max(peak, (double)std::fabs(v));
            auto q2 = q; q2.masterLin = 1.0e-4f;
            const auto xp = render(q2, note, sr, n);
            double pre = 0.0;
            for (float v : xp) pre = std::max(pre, (double)std::fabs(v));
            printf("    %-10.0f %-10.0f %8.4f pre %6.2f %9.1f %9.1f %9.1f %9.1f\n",
                   atkDb, gainDb, peak, pre * 1.0e4,
                   dB(goertzel(x, f0, sr)),
                   dB(goertzel(x, f0 * q.voice.tineRatio1, sr)),
                   dB(goertzel(x, f0 * q.voice.tineRatio2, sr)),
                   dB(goertzel(x, f0 * q.voice.tineRatio3, sr)));
        }
    }

    return 0;
}
