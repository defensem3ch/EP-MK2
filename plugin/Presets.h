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
        // The shipped defaults: measured tine mode ratios, a pickup offset of
        // 0.8 where the harmonics sit evenly, and Q from the sample library.
        { "Rhodes MkI", {} },

        // The same instrument played in, by ear rather than by measurement.
        // Softer and shorter than the shipped defaults: the hammer contact is
        // half again as long, so the tine modes are driven less hard and the
        // attack is rounder; the tine rings about half as long and sits 5 dB
        // further back; and the pickup is driven 1.5 dB harder to make that up.
        //
        // Hammer Level is the odd one: the raw blow is off by default because
        // no real pickup sees the hammer, and at -51.5 dB it is not being used
        // as a model of anything -- it is a thump under the attack.
        { "Basic Rhodes", {
            { "hammer_level",   -51.5f },
            { "hammer_contact",   0.58f },
            { "pickup_gain",     16.5f },
            { "tine_level",     -20.0f },
            { "tine_decay",     122.0f },
        }},

        // Same instrument, brighter: a harder, shorter hammer contact excites
        // the tine modes further up, more tine reaches the pickup, the coil
        // rolls off later, and the tine sits nearer the magnetic axis where
        // the harmonics either side of the fundamental come up.
        { "Rhodes Bright", {
            { "hammer_contact",   0.22f },
            { "tine_send",      -62.0f },
            { "tine_level",      -3.0f },
            { "pickup_lopass", 6000.0f },
            { "pickup_offset",    0.55f },
            { "pickup_gain",     12.0f },
        }},

        // A reed, not a rod, and no tone bar.  A flat cantilever's ideal mode
        // ratios are 6.27 and 17.55, it rings shorter, and the bark is *even*
        // harmonics -- so the tine sits close to the magnetic axis, which is a
        // low offset, not a high one.  No tone bar means no sub-fundamental.
        { "Wurlitzer", {
            { "tine_ratio1",      6.27f },
            { "tine_ratio2",     17.55f },
            { "tine_decay",     180.0f },
            { "tine_send",      -58.0f },
            { "tine_level",      -4.0f },
            { "hammer_contact",   0.30f },
            { "tone_decay",     900.0f },
            { "sub_level",     -100.0f },
            { "pickup_gain",     11.0f },
            { "pickup_offset",    0.40f },
            { "pickup_distance",  0.55f },
            { "pickup_lopass", 3000.0f },
        }},

        // Plucked against a fret: harmonic, not inharmonic, so the modes go to
        // integers.  A plectrum is a very short contact, the body is small and
        // dies fast, and there is no tone bar sub.
        { "Clav", {
            { "tine_ratio1",      2.0f },
            { "tine_ratio2",      3.0f },
            { "tine_ratio3",      4.0f },
            { "tine_decay",     700.0f },
            { "tine_send",      -52.0f },
            { "tine_level",      -8.0f },
            { "hammer_contact",   0.12f },
            { "tone_decay",     600.0f },
            { "tone_release",    20.0f },
            { "sub_level",     -100.0f },
            { "pickup_gain",     16.0f },
            { "pickup_offset",    0.65f },
            { "pickup_lopass", 7000.0f },
        }},

        // Bars are undercut so the first overtone is exactly 4x (a free bar's
        // natural ratio would be 6.27), with the second near 10.8.  A soft
        // mallet is a long contact, which is what keeps it pure; the bar rings
        // a long time and evenly, so decay does not track pitch; and there is
        // no pickup, so the geometry is pulled back and well off axis.
        { "Vibraphone", {
            { "tine_ratio1",      4.0f },
            { "tine_ratio2",     10.8f },
            { "tine_mode3_lvl",-100.0f },
            { "tine_decay",    1200.0f },
            { "tine_level",     -14.0f },
            { "hammer_contact",   4.00f },
            { "hammer_vel_ctc",   0.60f },
            { "tone_decay",    3000.0f },
            { "q_tracking",       0.0f },
            { "sub_level",     -100.0f },
            { "pickup_gain",     19.5f },
            { "pickup_offset",    1.00f },
            { "pickup_distance",  1.60f },
            { "pickup_lopass", 1400.0f },
            { "buzz_level",    -100.0f },
        }},

        // Short cantilever tines with almost no resonator behind them: the
        // tine speaks, the body barely does, and it dies quickly.
        { "Kalimba", {
            { "tine_ratio1",      6.27f },
            { "tine_ratio2",     17.5f },
            { "tine_decay",     300.0f },
            { "tine_send",      -56.0f },
            { "tine_level",      -5.0f },
            { "hammer_contact",   0.50f },
            { "tone_decay",     400.0f },
            { "tone_level",      -6.0f },
            { "sub_level",      -60.0f },
            { "pickup_gain",     18.5f },
            { "pickup_offset",    0.70f },
            { "pickup_lopass", 4000.0f },
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
