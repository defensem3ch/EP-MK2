#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"

// Factory presets, exposed through the AudioProcessor program interface so
// hosts show them in their own preset menu (VST3 program list, LV2 presets)
// without needing preset files on disk.
//
// Each preset is a set of *overrides* on the parameter defaults, not a full
// dump.  Applying one first restores every in-scope parameter to its default
// and then applies the overrides, so presets are absolute: switching between
// them repeatedly cannot accumulate state.
//
// The values come from the physical reasoning in docs/MODEL-NOTES.md -- which
// instrument has which mode ratios, how hard its pickup distorts, how long it
// rings.  They are starting points chosen for the right reasons, not presets
// tuned by ear, and the ones furthest from a Rhodes are the least trustworthy
// because the model was built to be a Rhodes.
namespace epmk2::presets {

struct Override {
    const char* id;
    float value;   // in panel units, exactly as the parameter reads
};

struct Preset {
    const char* name;
    std::vector<Override> values;
};

// Parameters a preset is allowed to touch.  Tuning, master level, polyphony
// and the sustain pedal are the player's, not the preset's: changing timbre
// should not retune the instrument, move the output level, alter the CPU
// budget, or stick the pedal down.
inline bool inPresetScope(const juce::String& id)
{
    return id != "bass_freq" && id != "base_note" && id != "divisions"
        && id != "interval"  && id != "sustain"   && id != "master"
        && id != "polyphony";
}

inline const std::vector<Preset>& table()
{
    static const std::vector<Preset> t = {
        // The shipped defaults: tine modes at the measured 7.1 and 20.4, and
        // a pickup symmetry of 7 dB (= 2.24 as an exponent).
        { "Rhodes MkI", {} },

        // Same instrument, mic'd brighter: more tine into the pickup, a
        // higher pickup corner, and a touch more asymmetry for bite.
        { "Rhodes Bright", {
            { "tine_send",       -70.0f },
            { "pickup_lopass",  3000.0f },
            { "pickup_gain",      17.0f },
            { "pickup_symmetry",   9.0f },
        }},

        // A reed, not a rod, and no tone bar.  A flat cantilever's ideal mode
        // ratios are 6.27 and 17.55; the electrostatic pickup distorts far
        // harder than a magnetic one, which is the bark; and it rings shorter.
        { "Wurlitzer", {
            { "tine_ratio1",       6.27f },
            { "tine_ratio2",      17.55f },
            { "tine_decay",      180.0f },
            { "tine_send",       -68.0f },
            { "tone_decay",      900.0f },
            { "pickup_gain",      17.0f },
            { "pickup_symmetry",  16.0f },
            { "pickup_lopass",  2600.0f },
        }},

        // Plucked string against a fret: harmonic, not inharmonic, so the
        // "tine" modes go to integers.  Short body, bright and percussive.
        { "Clav", {
            { "tine_ratio1",       2.0f },
            { "tine_ratio2",       3.0f },
            { "tine_decay",      700.0f },
            { "tine_send",       -55.0f },
            { "tone_decay",      600.0f },
            { "tone_release",     20.0f },
            { "pickup_gain",      20.0f },
            { "pickup_symmetry",  11.0f },
            { "pickup_lopass",  4000.0f },
        }},

        // Bars are undercut so the first overtone is exactly 4x (a free bar's
        // natural ratio would be 6.27), with the second near 10.8.  Long
        // sustain, and almost no pickup nonlinearity -- there is no pickup.
        { "Vibraphone", {
            { "tine_ratio1",       4.0f },
            { "tine_ratio2",      10.8f },
            { "tine_decay",     1200.0f },
            { "tine_level",      -12.0f },
            { "tone_decay",     2000.0f },
            { "pickup_gain",       6.0f },
            { "pickup_symmetry",   0.0f },
            { "pickup_lopass",  1200.0f },
            { "buzz_level",     -100.0f },
        }},

        // Short cantilever tines with almost no resonator behind them: the
        // tine dominates, the body barely speaks, and it dies quickly.
        { "Kalimba", {
            { "tine_ratio1",       6.27f },
            { "tine_ratio2",      17.5f },
            { "tine_decay",      300.0f },
            { "tine_send",       -62.0f },
            { "tone_decay",      400.0f },
            { "tone_level",       -3.0f },
            { "pickup_gain",      18.0f },
            { "pickup_symmetry",   2.0f },
            { "pickup_lopass",  3000.0f },
        }},
    };
    return t;
}

// Restore every in-scope parameter to its default, then apply the overrides.
inline void apply(juce::AudioProcessorValueTreeState& state, int index)
{
    const auto& presets = table();
    if (index < 0 || index >= (int)presets.size())
        return;

    auto set = [&state](const char* id, float value) {
        if (auto* p = state.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(value));
    };

    for (const auto& spec : params::table())
        if (inPresetScope(spec.id))
            set(spec.id, spec.def);

    for (const auto& o : presets[(size_t)index].values)
        set(o.id, o.value);
}

} // namespace epmk2::presets
