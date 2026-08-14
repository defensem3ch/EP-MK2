#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "../dsp/Engine.h"

// One table describing every exposed control: its id, its range *in the units
// shown to the player*, and how it lands in EngineParams.
//
// Keeping the conversion beside the range is deliberate.  The two bugs that
// made this model sound like a Wurlitzer were both unit mismatches between a
// panel value and what the DSP expected, and both were invisible because the
// conversion lived somewhere other than the declaration.
namespace epmk2::params {

enum class Unit { Decibels, Linear, Hertz, Q, Ratio, Toggle, Count, Millis };

struct Spec {
    const char* id;
    const char* name;
    const char* section;   // panel grouping, mirroring the original layout
    Unit unit;
    float min, max, def;
    // Where it goes.  Coefficient-affecting parameters are flagged so the
    // engine only re-derives filters when one of them actually moves.
    bool affectsCoefficients;
};

inline const std::vector<Spec>& table()
{
    static const std::vector<Spec> t = {
        // --- tuning ---------------------------------------------------------
        { "bass_freq",       "Base Frequency",  "Tuning", Unit::Hertz,    100.0f, 20000.0f,  440.0f, true  },
        { "base_note",       "Base MIDI Note",  "Tuning", Unit::Count,      0.0f,   127.0f,   69.0f, true  },
        { "divisions",       "Divisions",       "Tuning", Unit::Count,      1.0f,   100.0f,   12.0f, true  },
        { "interval",        "Interval",        "Tuning", Unit::Ratio,      0.0f,    20.0f,    2.0f, true  },
        // --- tine -----------------------------------------------------------
        { "tine_ratio1",     "Tine Ratio 1",    "Tine", Unit::Ratio,      0.0f,    30.0f,    7.1f, true  },
        { "tine_ratio2",     "Tine Ratio 2",    "Tine", Unit::Ratio,      0.0f,    60.0f,   20.4f, true  },
        { "tine_ratio3",     "Tine Ratio 3",    "Tine", Unit::Ratio,      0.0f,    60.0f,   39.7f, true  },
        { "tine_mode2_lvl",  "Mode 2 Level",    "Tine", Unit::Decibels, -100.0f,     0.0f,    0.0f, false },
        { "tine_mode3_lvl",  "Mode 3 Level",    "Tine", Unit::Decibels, -100.0f,     0.0f,   -6.0f, false },
        { "tine_mode_damp",  "Mode Damping",    "Tine", Unit::Ratio,      0.0f,     2.0f,    0.0f, true  },
        { "tine_hipass",     "Tine High-Pass",  "Tine", Unit::Hertz,     20.0f, 20000.0f,   20.0f, true  },
        { "tine_decay",      "Tine Decay",      "Tine", Unit::Q,          1.0f,  2000.0f,  225.0f, true  },
        { "tine_level",      "Tine Level",      "Tine", Unit::Decibels, -100.0f,     0.0f,    0.0f, false },
        { "tine_send",       "Tine Send",       "Tine", Unit::Decibels, -100.0f,    24.0f,  -77.0f, false },
        // --- tone bar -------------------------------------------------------
        { "tone_decay",      "Tone Decay",      "Tone Bar", Unit::Q,          1.0f,  2000.0f, 1642.18f, true },
        { "tone_release",    "Tone Release",    "Tone Bar", Unit::Q,          1.0f,  2000.0f,   30.0f, true  },
        { "tone_level",      "Tone Level",      "Tone Bar", Unit::Decibels, -100.0f,     0.0f,    0.0f, false },
        { "hammer_level",    "Hammer Level",    "Tone Bar", Unit::Decibels, -100.0f,     0.0f, -100.0f, false },
        { "hammer_contact",  "Hammer Contact",  "Tone Bar", Unit::Millis,     0.05f,   20.0f,    0.4f, false },
        { "hammer_vel_ctc",  "Vel to Contact",  "Tone Bar", Unit::Ratio,      0.0f,     4.0f,    1.5f, false },
        { "noteoff_level",   "Note-Off Level",  "Tone Bar", Unit::Decibels, -100.0f,     0.0f,  -37.9f, false },
        // --- pickup ---------------------------------------------------------
        { "pickup_gain",     "Pickup Gain",     "Pickup", Unit::Decibels,    0.0f,    24.0f,   15.0f, false },
        { "pickup_attack",   "Pickup Attack",   "Pickup", Unit::Decibels, -100.0f,    30.0f,  -31.0f, false },
        { "pickup_lopass",   "Pickup Low-Pass", "Pickup", Unit::Hertz,     20.0f, 20000.0f, 2000.0f, true  },
        // Geometry, replacing the old symmetry exponent.  Offset is what
        // creates asymmetry, so it is now the Rhodes-to-Wurlitzer axis: at 0
        // the tine sits on the magnetic axis and the response is purely even.
        { "pickup_distance", "Pickup Distance", "Pickup", Unit::Ratio,      0.1f,     3.0f,    0.8f, true  },
        { "pickup_offset",   "Pickup Offset",   "Pickup", Unit::Ratio,      0.0f,     1.5f,    0.8f, true  },
        { "pickup_level",    "Pickup Level",    "Pickup", Unit::Decibels, -100.0f,     6.0f,    6.0f, false },
        { "buzz_level",      "Buzz Level",      "Pickup", Unit::Decibels, -100.0f,     0.0f,    0.0f, false },
        { "buzz_phase",      "Buzz Phase",      "Pickup", Unit::Toggle,     0.0f,     1.0f,    1.0f, false },
        { "sustain",         "Sustain Pedal",   "Tone Bar", Unit::Toggle,     0.0f,     1.0f,    0.0f, false },
        // --- tremolo --------------------------------------------------------
        { "trem_on",         "Tremolo",         "Tremolo", Unit::Toggle,     0.0f,     1.0f,    0.0f, false },
        { "trem_rate",       "Tremolo Rate",    "Tremolo", Unit::Hertz,      0.0f,    20.0f,    3.0f, false },
        { "trem_shape",      "Tremolo Shape",   "Tremolo", Unit::Count,      0.0f,   127.0f,    0.0f, false },
        { "trem_depth",      "Tremolo Depth",   "Tremolo", Unit::Decibels, -100.0f,     0.0f,   -9.0f, false },
        // --- output ---------------------------------------------------------
        // Master reaches above unity now.  MK1 inherited a 0 dB ceiling from the Pd
        // patch's dbtorms convention, which was fine when the level was fixed; with
        // the excitation rebalanced there has to be make-up available.
        { "master",          "Master",          "Output", Unit::Decibels, -100.0f,    12.0f,    0.0f, false },
        // Polyphony is a CPU control as much as a musical one.  With the
        // sustain pedal down a Rhodes note rings for tens of seconds, so every
        // voice is legitimately busy and cost scales with this directly.
        { "polyphony",       "Polyphony",       "Output", Unit::Count,      4.0f,    64.0f,   32.0f, false },
    };
    return t;
}

// Panel colours, echoing the section headers of the original Pd layout.
inline juce::Colour sectionColour(const juce::String& section)
{
    if (section == "Output")   return juce::Colour(0xffc4fcc4);
    if (section == "Tuning")   return juce::Colour(0xffe0c4fc);
    if (section == "Tine")     return juce::Colour(0xfffcfcc4);
    if (section == "Tone Bar") return juce::Colour(0xfffcc4c4);
    if (section == "Pickup")   return juce::Colour(0xfffce0c4);
    if (section == "Tremolo")  return juce::Colour(0xffc4c4fc);
    return juce::Colour(0xffdcdcdc);
}

inline juce::StringArray sectionOrder()
{
    return { "Output", "Tuning", "Tine", "Tone Bar", "Pickup", "Tremolo" };
}

inline float toLinear(const Spec& s, float value)
{
    return s.unit == Unit::Decibels ? std::pow(10.0f, value / 20.0f) : value;
}

// How many decimals are worth showing.  A Q of 1642.1800537 is noise; a tine
// ratio of 7.1 is meaningful to one place.
inline int decimalsFor(Unit u)
{
    switch (u) {
        case Unit::Decibels: return 1;
        case Unit::Hertz:    return 1;
        case Unit::Ratio:    return 2;
        case Unit::Q:        return 0;
        case Unit::Millis:   return 2;
        case Unit::Count:    return 0;
        default:             return 0;
    }
}

inline juce::String suffixFor(Unit u)
{
    switch (u) {
        case Unit::Decibels: return " dB";
        case Unit::Hertz:    return " Hz";
        case Unit::Millis:   return " ms";
        default:             return {};
    }
}

inline juce::AudioProcessorValueTreeState::ParameterLayout layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (const auto& s : table()) {
        if (s.unit == Unit::Toggle) {
            l.add(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID(s.id, 1), s.name, s.def > 0.5f));
        } else {
            // The display text has to come from the parameter, not the
            // slider: an APVTS attachment defers to the parameter's own
            // conversion, so setNumDecimalPlacesToDisplay() on the slider is
            // ignored and every value reads as "1642.1800537".
            const int places = decimalsFor(s.unit);
            const juce::String suffix = suffixFor(s.unit);
            l.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID(s.id, 1), s.name,
                juce::NormalisableRange<float>(s.min, s.max), s.def,
                juce::AudioParameterFloatAttributes()
                    .withStringFromValueFunction(
                        [places, suffix](float v, int) {
                            return juce::String(v, places) + suffix;
                        })
                    .withValueFromStringFunction(
                        [](const juce::String& t) { return t.getFloatValue(); })));
        }
    }
    return l;
}

// Read the tree into EngineParams.  Returns true when something that feeds a
// filter coefficient has moved, so the caller knows to re-derive them.
bool apply(const juce::AudioProcessorValueTreeState& tree,
           EngineParams& p,
           std::vector<float>& previous);

} // namespace epmk2::params
