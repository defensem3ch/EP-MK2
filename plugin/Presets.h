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
//
// The pitch wheel and the mod wheel are the player's for a sharper reason:
// something else is already driving them.  A preset that reset them would be
// fighting a controller for ownership, and a patch change in the middle of a
// held bend would snap the note straight.  This is the same rule the sustain
// pedal has had since CC64 was fixed.
inline bool inPresetScope(const juce::String& id)
{
    return id != "bass_freq"  && id != "base_note" && id != "divisions"
        && id != "interval"   && id != "sustain"   && id != "master"
        && id != "polyphony"  && id != "pitch_bend" && id != "vib_depth";
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
            { "tine_level",      -9.0f },
            { "pickup_lopass", 6000.0f },
            { "pickup_offset",    0.55f },
            { "pickup_gain",      8.0f },
            { "pickup_level",     0.0f },
        }},

        // A reed, not a rod, and no tone bar.  A flat cantilever's ideal mode
        // ratios are 6.27 and 17.55, it rings shorter, and the bark is *even*
        // harmonics -- so the tine sits close to the magnetic axis, which is a
        // low offset, not a high one.  No tone bar means no sub-fundamental.
        { "Wurlitzer", {
            { "hammer_contact",   0.30f },
            // A flat cantilever, not a rod: its ideal mode ratios are 6.27
            // and 17.55, against the Rhodes' measured 7.1 and 20.4.
            { "tine_ratio1",      6.27f },
            { "tine_ratio2",     17.55f },
            { "tine_decay",     180.0f },
            { "tine_level",     -50.27f },
            { "tine_send",      -32.38f },
            // The reed *into* the pickup, and hard: a Wurlitzer's bark is its
            // electrostatic pickup being driven, so the tone bar path is
            // pushed above unity rather than trimmed.
            { "tone_send",        6.76f },
            { "tone_decay",    1314.07f },
            { "sub_level",      -63.35f },
            // Low offset is the bark.  The reed sits near the pickup's
            // magnetic axis, where the response is even-order, which is what
            // separates this from a Rhodes more than any other control here.
            { "pickup_offset",    0.21f },
            { "pickup_distance",  0.68f },
            { "pickup_gain",     10.12f },
            { "pickup_level",    -6.0f },
            { "pickup_lopass", 3879.95f },
            { "buzz_level",     -16.53f },
            { "buzz_phase",       0.0f },
        }},

        // Plucked against a fret: harmonic, not inharmonic, so the modes go to
        // integers.  A plectrum is a very short contact, the body is small and
        // dies fast, and there is no tone bar sub.
        { "Clav", {
            // Plucked against a fret, so a very short contact and a strong
            // velocity effect on it -- a clav's bite is how hard it was hit,
            // more than on anything else here.
            { "hammer_contact",   0.19f },
            { "hammer_vel_ctc",   2.17f },
            { "hammer_level",   -53.98f },
            // Not integers.  A string stopped at a fret is not quite an ideal
            // string, and 4.58 and 8.08 against 2.0 are what stops this
            // sounding like an organ.  Mode damping above 1 kills the upper
            // two quickly, which is the pluck rather than the tone.
            { "tine_ratio1",      2.0f },
            { "tine_ratio2",      4.58f },
            { "tine_ratio3",      8.08f },
            { "tine_mode_damp",   1.08f },
            { "tine_decay",     344.59f },
            { "tine_level",     -38.86f },
            { "tine_send",      -52.0f },
            { "tone_decay",     494.11f },
            { "tone_release",    37.74f },
            { "q_tracking",       0.0f },
            { "sub_level",     -100.0f },
            { "noteoff_level",   -9.12f },
            // The pickup driven to its limit, well off axis, with the buzz
            // just present: this is the one preset where the nonlinearity is
            // most of the sound rather than a colouring of it.
            { "pickup_gain",     24.0f },
            { "pickup_level",     0.0f },
            { "pickup_offset",    1.29f },
            { "pickup_distance",  0.59f },
            { "pickup_lopass", 9344.17f },
            { "buzz_level",     -32.01f },
            { "buzz_phase",       0.0f },
        }},

        // Bars are undercut so the first overtone is exactly 4x (a free bar's
        // natural ratio would be 6.27), with the second near 10.8.  A soft
        // mallet is a long contact, which is what keeps it pure; the bar rings
        // a long time and evenly, so decay does not track pitch; and there is
        // no pickup, so the geometry is pulled back and well off axis.
        { "Vibraphone", {
            // Bars are undercut so the overtones land on 4x and 10x, against
            // a free bar's natural 6.27 -- which is the whole reason a
            // vibraphone sounds like a pitch and a marimba sounds like a
            // knock.  Mode 1 sits at 1.0, so the bar carries its own
            // fundamental rather than borrowing the tone bar's.
            { "tine_ratio1",      1.0f },
            { "tine_ratio2",      4.0f },
            { "tine_ratio3",     10.0f },
            { "tine_decay",     923.60f },
            { "tine_level",     -20.04f },
            { "tine_send",      -18.07f },
            // A soft mallet: a long contact is what keeps it pure, and the
            // hammer's own thud is left just audible under the attack.
            { "hammer_contact",   0.58f },
            { "hammer_level",   -51.49f },
            { "bass_tilt",        0.68f },
            { "vel_range",        6.61f },
            { "strike_var",       0.53f },
            // Bars are cut and tuned one at a time, so no two are quite the
            // same: this is the highest key variation of any preset here.
            { "key_var",          0.88f },
            { "tone_send",      -68.85f },
            { "tone_decay",    1465.59f },
            { "tone_release",   208.58f },
            { "sub_level",      -58.14f },
            // Almost no pickup nonlinearity, because there is no pickup: a
            // vibraphone is heard through the air and its resonator tubes.
            { "pickup_gain",      1.02f },
            { "pickup_lopass",15277.49f },
            { "buzz_level",    -100.0f },
        }},

        // Short cantilever tines with almost no resonator behind them: the
        // tine speaks, the body barely does, and it dies quickly.
        { "Kalimba", {
            { "tine_ratio1",      6.27f },
            { "tine_ratio2",     17.5f },
            { "tine_decay",     300.0f },
            { "tine_send",      -56.0f },
            { "tine_level",     -12.0f },
            { "hammer_contact",   0.50f },
            { "tone_decay",     400.0f },
            { "tone_level",      -6.0f },
            { "sub_level",      -60.0f },
            { "pickup_gain",     12.0f },
            { "pickup_offset",    0.70f },
            { "pickup_lopass", 4000.0f },
            { "pickup_level",    -4.0f },
        }},

        // Struck bronze: a short, dense bar over a resonator that barely
        // sustains.  Voiced by ear, and the first preset here that was.
        //
        // The ratios are ascending here only because modes 1 and 2 were
        // swapped after the fact.  That is free: the two carry the same level
        // by default, and each mode is an independent resonator, so which
        // slot holds which frequency changes nothing at all.  Rendered both
        // ways and compared sample for sample to be sure.
        //
        // What is *not* free is the values themselves.  They are not near
        // anything -- 2.52, 4.88, 8.41 -- and the small amounts by which they
        // miss simple relationships are the gamelan character rather than
        // untidiness.  Rounding them to 2.5, 5 and 8.5 would be throwing away
        // the sound in order to make the table look deliberate.
        //
        // Both of the tone bar's paths are nearly shut, which is what "barely
        // sustains" means here and what Tone to Pickup made possible: before
        // that control existed the pickup carried the bar at unity whatever
        // Tone Direct said, so this sound could not have been reached at all.
        { "Gamelan", {
            { "hammer_contact",   0.50f },
            { "tine_ratio1",      2.52f },
            { "tine_ratio2",      8.41f },
            { "tine_ratio3",      4.88f },
            { "tine_decay",     133.27f },
            // Two dB down, for headroom: at 0 this peaked -1.7 dB at the
            // bottom of the keyboard where nothing else here goes above
            // -3.2, and a preset with nothing left in its lowest octave has
            // nowhere to go when someone leans on it.
            //
            // Here rather than on Pickup Level, which does nothing at all in
            // this preset: both sends are nearly shut, so the pickup carries
            // almost none of this sound and turning it down changes the
            // output by exactly zero.  The direct tine *is* the instrument
            // here.
            { "tine_level",      -2.0f },
            { "tine_send",      -84.95f },
            { "tone_level",      -6.0f },
            { "tone_send",      -50.35f },
            { "tone_decay",     234.55f },
            { "sub_level",      -60.0f },
            { "pickup_gain",     12.0f },
            { "pickup_offset",    0.70f },
            { "pickup_lopass", 4000.0f },
            { "pickup_level",    -4.0f },
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
