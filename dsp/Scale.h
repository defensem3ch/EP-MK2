#pragma once

#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

// Unequal tunings, as Scala's .scl format describes them.
//
// The Divisions and Interval parameters already give any *equal* division of
// any interval, which is more than most instruments offer and is how the Pd
// original did it.  What they cannot express is a table of arbitrary steps --
// a historical temperament, a just scale, anything Scale Workshop exports.
//
// No JUCE here, like the rest of dsp/: the parser is a pure function over a
// string, which is what makes it testable without a plugin around it.
namespace epmk2 {

// The ratio `step` degrees above the base note, from a bare table.
//
// Taking a pointer and a count rather than the Scale itself is what lets the
// audio thread use this: it reads a fixed array the message thread published,
// with no container to be reallocated underneath it.
inline double scaleRatio(const double* cents, int n, int step) noexcept
{
    if (cents == nullptr || n <= 0)
        return 1.0;

    int repeats = step / n;
    int degree  = step % n;
    // C++ truncates division towards zero, so a negative step lands on a
    // negative remainder -- which would put the octave below middle C above
    // it.  Notes below the base note are not an edge case here; they are half
    // the keyboard.
    if (degree < 0) {
        degree += n;
        --repeats;
    }

    const double period = cents[n - 1];
    const double c = (degree == 0 ? 0.0 : cents[degree - 1]) + repeats * period;
    return std::pow(2.0, c / 1200.0);
}

struct Scale {
    // Cents above 1/1 for degrees 1..N, the last being the period the scale
    // repeats at.  Degree 0 is 1/1 and is implicit, exactly as .scl has it --
    // a 12-note scale therefore lists 12 values ending at 1200.
    //
    // Empty means no scale, and the instrument falls back to its equal
    // divisions.  That is the default, so the tuning it has always had is
    // what it still has until someone chooses otherwise.
    std::string name;
    std::vector<double> cents;

    bool empty()   const noexcept { return cents.empty(); }
    int  degrees() const noexcept { return (int) cents.size(); }
    double period() const noexcept { return cents.empty() ? 1200.0 : cents.back(); }

    // The frequency ratio this many degrees above the base note.  Steps past
    // either end of the scale wrap round and move by a period, which is what
    // lets a 5-note or a 53-note scale cover a whole keyboard.
    double ratioForStep(int step) const noexcept
    {
        return scaleRatio(cents.empty() ? nullptr : cents.data(), degrees(), step);
    }
};

// Parse Scala .scl.  False on anything malformed, with a reason in `error`.
//
// The format: '!' comments anywhere; the first line that is not a comment is
// the description, *including* when it is blank; the next is the number of
// degrees; then that many values, one per line.  A value containing '.' is
// cents, anything else is a ratio -- "3/2" or a bare integer.  Everything
// after the value on a line is a comment.
inline bool parseScl(const std::string& text, Scale& out, std::string& error)
{
    auto trim = [](const std::string& s) {
        const auto b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos)
            return std::string();
        return s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
    };

    std::vector<std::string> lines;
    for (size_t i = 0, start = 0; i <= text.size(); ++i)
        if (i == text.size() || text[i] == '\n') {
            lines.push_back(trim(text.substr(start, i - start)));
            start = i + 1;
        }

    size_t at = 0;
    // The description may legitimately be blank, so this skips comments only.
    auto nextRaw = [&](std::string& dst) {
        while (at < lines.size()) {
            const std::string& l = lines[at++];
            if (! l.empty() && l[0] == '!')
                continue;
            dst = l;
            return true;
        }
        return false;
    };
    // Everything after it is a value, where a blank line carries nothing.
    auto nextValue = [&](std::string& dst) {
        while (nextRaw(dst))
            if (! dst.empty())
                return true;
        return false;
    };

    std::string line;
    if (! nextRaw(line)) {
        error = "the file is empty";
        return false;
    }

    Scale s;
    s.name = line;

    if (! nextValue(line)) {
        error = "no note count";
        return false;
    }
    const long count = std::strtol(line.c_str(), nullptr, 10);
    if (count < 1 || count > 1024) {
        error = "note count is " + line + ", which is not between 1 and 1024";
        return false;
    }

    double previous = 0.0;
    for (long i = 0; i < count; ++i) {
        if (! nextValue(line)) {
            error = "only " + std::to_string(i) + " of " + std::to_string(count)
                  + " degrees are present";
            return false;
        }

        // The value is the first field; the rest of the line is a comment.
        const std::string token = line.substr(0, line.find_first_of(" \t"));
        double c = 0.0;

        if (token.find('.') != std::string::npos) {
            c = std::strtod(token.c_str(), nullptr);
        } else {
            const auto slash = token.find('/');
            const double num = std::strtod(token.c_str(), nullptr);
            const double den = slash == std::string::npos
                             ? 1.0 : std::strtod(token.c_str() + slash + 1, nullptr);
            if (! (num > 0.0) || ! (den > 0.0)) {
                error = "degree " + std::to_string(i + 1) + " is \"" + token
                      + "\", which is not a positive ratio";
                return false;
            }
            c = 1200.0 * std::log2(num / den);
        }

        if (! std::isfinite(c)) {
            error = "degree " + std::to_string(i + 1) + " is \"" + token
                  + "\", which is not a number";
            return false;
        }
        // Ascending is not quite required by the format, but a scale that
        // doubles back makes a higher key sound a lower note, and silently
        // playing that is worse than refusing the file.
        if (c <= previous) {
            error = "degree " + std::to_string(i + 1) + " is \"" + token
                  + "\", which does not rise above the degree before it";
            return false;
        }
        previous = c;
        s.cents.push_back(c);
    }

    out = s;
    return true;
}

struct BuiltInScale {
    const char* name;
    const char* scl;
};

// GENERATED by tools/scales.py, and re-derivable from it.  The
// tempered scales are computed from their definitions -- which fifths
// are narrowed and by how much -- not copied from a table of cents.
inline const std::vector<BuiltInScale>& builtInScales()
{
    static const std::vector<BuiltInScale> t = {
        { "Pythagorean",
          "3-limit, the chain of pure fifths from Eb to G#\n12\n256/243\n9/8\n32/27\n81/64\n4/3\n729/512\n3/2\n128/81\n27/16\n16/9\n243/128\n2/1\n" },
        { "Just Intonation (5-limit)",
          "Pure thirds and fifths in the home key, and audibly wrong away from it\n12\n16/15\n9/8\n6/5\n5/4\n4/3\n45/32\n3/2\n8/5\n5/3\n9/5\n15/8\n2/1\n" },
        { "Quarter-comma Meantone",
          "Every fifth narrowed a quarter of a syntonic comma: pure major thirds\n12\n76.049\n193.157\n310.265\n386.314\n503.422\n579.471\n696.578\n772.627\n889.735\n1006.843\n1082.892\n1200.000\n" },
        { "Werckmeister III",
          "1691. Four fifths narrowed by a quarter Pythagorean comma; all keys playable\n12\n90.225\n192.180\n294.135\n390.225\n498.045\n588.270\n696.090\n792.180\n888.270\n996.090\n1092.180\n1200.000\n" },
        { "Kirnberger III",
          "Four fifths narrowed by a quarter syntonic comma, one by the schisma\n12\n90.225\n193.157\n294.135\n386.314\n498.045\n590.224\n696.578\n792.180\n889.735\n996.090\n1088.269\n1200.000\n" },
        { "Vallotti",
          "Six fifths narrowed by a sixth of a Pythagorean comma; the rest pure\n12\n94.135\n196.090\n298.045\n392.180\n501.955\n592.180\n698.045\n796.090\n894.135\n1000.000\n1090.225\n1200.000\n" },
        { "Young (1799)",
          "Vallotti's shape, rotated a fifth\n12\n90.225\n196.090\n294.135\n392.180\n498.045\n588.270\n698.045\n792.180\n894.135\n996.090\n1090.225\n1200.000\n" },
        { "19-tone Equal",
          "Nineteen equal steps: meantone-like, with distinct sharps and flats\n19\n63.158\n126.316\n189.474\n252.632\n315.789\n378.947\n442.105\n505.263\n568.421\n631.579\n694.737\n757.895\n821.053\n884.211\n947.368\n1010.526\n1073.684\n1136.842\n1200.000\n" },
        { "22-tone Equal",
          "Twenty-two equal steps\n22\n54.545\n109.091\n163.636\n218.182\n272.727\n327.273\n381.818\n436.364\n490.909\n545.455\n600.000\n654.545\n709.091\n763.636\n818.182\n872.727\n927.273\n981.818\n1036.364\n1090.909\n1145.455\n1200.000\n" },
        { "31-tone Equal",
          "Thirty-one equal steps, very close to quarter-comma meantone\n31\n38.710\n77.419\n116.129\n154.839\n193.548\n232.258\n270.968\n309.677\n348.387\n387.097\n425.806\n464.516\n503.226\n541.935\n580.645\n619.355\n658.065\n696.774\n735.484\n774.194\n812.903\n851.613\n890.323\n929.032\n967.742\n1006.452\n1045.161\n1083.871\n1122.581\n1161.290\n1200.000\n" },
        { "53-tone Equal",
          "Fifty-three equal steps: fifths and thirds both near-pure\n53\n22.642\n45.283\n67.925\n90.566\n113.208\n135.849\n158.491\n181.132\n203.774\n226.415\n249.057\n271.698\n294.340\n316.981\n339.623\n362.264\n384.906\n407.547\n430.189\n452.830\n475.472\n498.113\n520.755\n543.396\n566.038\n588.679\n611.321\n633.962\n656.604\n679.245\n701.887\n724.528\n747.170\n769.811\n792.453\n815.094\n837.736\n860.377\n883.019\n905.660\n928.302\n950.943\n973.585\n996.226\n1018.868\n1041.509\n1064.151\n1086.792\n1109.434\n1132.075\n1154.717\n1177.358\n1200.000\n" },
        { "Bohlen-Pierce",
          "Thirteen equal divisions of 3/1, so it has no octave at all\n13\n146.304\n292.608\n438.913\n585.217\n731.521\n877.825\n1024.130\n1170.434\n1316.738\n1463.042\n1609.347\n1755.651\n1901.955\n" },
        { "Harmonic Series 8-16",
          "The eighth to sixteenth harmonics, as a scale\n8\n9/8\n10/8\n11/8\n12/8\n13/8\n14/8\n15/8\n16/8\n" },
        { "Slendro (5-tone equal)",
          "An equal-step approximation of the Javanese tuning, not a measurement of one\n5\n240.000\n480.000\n720.000\n960.000\n1200.000\n" },
    };
    return t;
}

} // namespace epmk2
