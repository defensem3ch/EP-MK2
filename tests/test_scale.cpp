#include "../dsp/Scale.h"
#include <cassert>
#include <cstdio>
#include <cmath>
using namespace epmk2;

static int failures = 0;
static void check(bool ok, const char* what, const char* extra = "")
{
    printf("  %-52s %s%s\n", what, ok ? "ok" : "FAILED", extra);
    if (! ok) ++failures;
}

int main()
{
    // Every built-in must parse, and say what it claims to say.
    for (const auto& b : builtInScales()) {
        Scale s; std::string err;
        char msg[160];
        if (! parseScl(b.scl, s, err)) {
            snprintf(msg, sizeof msg, "  (%s)", err.c_str());
            check(false, b.name, msg);
            continue;
        }
        snprintf(msg, sizeof msg, "  (%d degrees, period %.1f cents)",
                 s.degrees(), s.period());
        check(true, b.name, msg);
    }

    Scale s; std::string err;

    // 12-equal expressed as cents must land on the ratios 12-equal gives.
    parseScl("twelve\n12\n100.\n200.\n300.\n400.\n500.\n600.\n700.\n800.\n900.\n1000.\n1100.\n2/1\n", s, err);
    double worst = 0.0;
    for (int step = -36; step <= 36; ++step)
        worst = std::fmax(worst, std::fabs(s.ratioForStep(step)
                                           - std::pow(2.0, step / 12.0)));
    char b1[96]; snprintf(b1, sizeof b1, "  (worst error %.2e)", worst);
    check(worst < 1e-12, "cents and ratios agree with 12-equal", b1);

    // Below the base note is half the keyboard, and C's % truncates towards
    // zero -- so this is the case that breaks if the wrap is done naively.
    parseScl("five\n5\n240.\n480.\n720.\n960.\n1200.\n", s, err);
    check(std::fabs(s.ratioForStep(-1) - std::pow(2.0, -240.0 / 1200.0)) < 1e-12,
          "one degree below the base note wraps down, not up");
    check(std::fabs(s.ratioForStep(-5) - 0.5) < 1e-12, "a period below is half");
    check(std::fabs(s.ratioForStep(-6) - 0.5 * std::pow(2.0, -240.0 / 1200.0)) < 1e-12,
          "and below that keeps descending");

    // A scale with no octave still has to repeat at its own period.
    parseScl("bp\n13\n146.304\n292.608\n438.913\n585.217\n731.521\n877.825\n1024.130\n"
             "1170.434\n1316.738\n1463.042\n1609.347\n1755.651\n1901.955\n", s, err);
    check(std::fabs(s.ratioForStep(13) - 3.0) < 1e-4, "a non-octave period repeats at 3/1");

    // Things that must be refused rather than played.
    struct { const char* text; const char* what; } bad[] = {
        { "", "an empty file" },
        { "d\n0\n", "a count of zero" },
        { "d\n3\n100.\n200.\n", "fewer degrees than the count promises" },
        { "d\n2\n200.\n100.\n", "a scale that descends" },
        { "d\n2\n100.\n100.\n", "a repeated degree" },
        { "d\n2\n0/1\n1200.\n", "a zero ratio" },
        { "d\n2\n-3/2\n1200.\n", "a negative ratio" },
    };
    for (const auto& t : bad) {
        Scale junk; std::string why;
        check(! parseScl(t.text, junk, why), t.what);
    }

    // Comments, blank lines and a blank description are all legal.
    const char* messy = "! name.scl\n!\n\n! a comment after the blank description\n"
                        "2\n! and here\n3/2  the fifth\n2/1 ! the octave\n";
    const bool ok = parseScl(messy, s, err);
    check(ok && s.degrees() == 2 && s.name.empty()
          && std::fabs(s.ratioForStep(1) - 1.5) < 1e-12,
          "comments, blank lines and a blank description");

    printf("%s\n", failures ? "FAILED" : "all scale checks passed");
    return failures ? 1 : 0;
}
