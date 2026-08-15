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
    // One sentence of what it is, one of what it does to the sound.  Shown in
    // the panel's info bar on hover.  Units are left out -- the value box
    // already says those.
    const char* help;
    // What the panel calls it, when the full name is too wide for a control.
    // The panel is exactly as wide as its longest name, so one long name costs
    // width on all 45 controls -- but the host's automation list has no such
    // constraint and wants the unabbreviated name.  Null means they are the
    // same, which is true of most of them.
    const char* ui = nullptr;

    // The name to draw on the panel.
    const char* panelName() const { return ui != nullptr ? ui : name; }
};

// The panel reads left to right, top to bottom, in the order the instrument
// works: what strikes it, what vibrates, what it is mounted on, what picks it
// up, then what happens to the signal afterwards.  Within each group the order
// is always **level, then tone, then time** -- how loud this part is, what
// colour it has, and how it evolves.  Tuning goes last because it is set once
// and left.
//
// Grouping by physical part is not decoration: the model *is* a hammer, a
// tine, a tone bar and a pickup, and docs/MODEL-NOTES.md explains each control
// in those terms.  What was confusing before was that the grouping was right
// and the ordering inside it was whatever happened to be appended last, with
// the hammer controls filed under the tone bar and the sustain pedal with them.
inline const std::vector<Spec>& table()
{
    static const std::vector<Spec> t = {
        // --- output: the controls reached for first -------------------------
        // Master reaches above unity.  MK1 inherited a 0 dB ceiling from the Pd
        // patch's dbtorms convention, which was fine when the level was fixed;
        // with the excitation rebalanced there has to be make-up available.
        { "master",          "Master",          "Output", Unit::Decibels, -100.0f,    12.0f,    0.0f, false,
          "Output level for the whole instrument. Reaches above unity because the model's own level moved when the excitation was rebalanced." },
        // Polyphony is a CPU control as much as a musical one.  With the
        // sustain pedal down a Rhodes note rings for tens of seconds, so every
        // voice is legitimately busy and cost scales with this directly.
        { "polyphony",       "Polyphony",       "Output", Unit::Count,      4.0f,   128.0f,   32.0f, false,
          "How many notes may sound at once. Costs CPU directly: with the pedal down a note rings for tens of seconds, so voices stay genuinely busy." },
        { "sustain",         "Sustain Pedal",   "Output", Unit::Toggle,     0.0f,     1.0f,    0.0f, false,
          "Lifts the dampers, exactly as MIDI CC64 does. Released notes keep ringing until it drops." },

        // --- hammer: the strike ---------------------------------------------
        { "hammer_level",    "Hammer Level",    "Hammer", Unit::Decibels, -100.0f,     0.0f, -100.0f, false,
          "How much of the raw hammer blow is mixed straight into the output. Off by default -- the tine supplies the attack now, and this only adds a click." },
        { "hammer_contact",  "Contact Time",    "Hammer", Unit::Millis,     0.05f,   20.0f,    0.4f, false,
          "How long the hammer stays against the tine. The main brightness control: a short contact is broadband and drives the high inharmonic modes, a long one is soft and pure." },
        { "hammer_vel_ctc",  "Vel to Contact",  "Hammer", Unit::Ratio,      0.0f,     4.0f,    1.5f, false,
          "How much a harder blow shortens the contact. This is where harder-is-brighter comes from, as distinct from merely louder.",
          "Vel to Ctc" },
        // How much further a bass tine swings than a treble one for the same
        // blow.  The pickup differentiates, which is a real +6 dB/octave, and
        // without this the whole keyboard tilts against the bass.
        { "bass_tilt",       "Bass Tilt",       "Hammer", Unit::Ratio,      0.0f,     1.5f,   0.69f, false,
          "How much further a bass tine swings than a treble one for the same blow. The pickup senses rate of change, which favours the treble by 6 dB per octave; this opposes it. Raise it for more bass." },
        // How much one strike differs from the next.  0 is exactly repeatable.
        { "vel_range",       "Dynamic Range",   "Hammer", Unit::Ratio,      2.0f,    12.0f,    5.0f, false,
          "How far the instrument travels from the softest note to the hardest. 5 is how this has always played. The reference instrument is wider, nearer 8, which gives more room between soft and loud but asks more of your keyboard.",
          "Dyn Range" },
        { "strike_var",      "Strike Variation", "Hammer", Unit::Ratio,     0.0f,     1.0f,    0.3f, false,
          "How much one strike differs from the next in force, contact time and timing. At 0 every note is identical; raise it and repeated notes stop sounding sequenced.",
          "Strike Var" },

        // --- tine: what actually vibrates -----------------------------------
        { "tine_level",      "Tine Level",      "Tine", Unit::Decibels, -100.0f,     0.0f,  -15.0f, false,
          "How much of the tine's own vibration goes straight to the output, bypassing the pickup. Tine to Pickup is the other, more physical path; with that at its default this is most of the tine you hear." },
        { "tine_send",       "Tine to Pickup",  "Tine", Unit::Decibels, -100.0f,    24.0f,  -77.0f, false,
          "How much of the tine reaches the pickup. The pickup faces the tine, so this is the more physical of its two paths to the output.",
          "Tine Send" },
        { "tine_ratio1",     "Mode 1 Ratio",    "Tine", Unit::Ratio,      0.0f,    30.0f,    7.1f, true,
          "Frequency of the tine's first inharmonic mode, as a multiple of the note. Measured at 7.1 on a real Rhodes. Whole numbers make it harmonic, like a string." },
        { "tine_ratio2",     "Mode 2 Ratio",    "Tine", Unit::Ratio,      0.0f,    60.0f,   20.4f, true,
          "The second inharmonic mode, measured at 20.4. Skipped automatically on notes where it would land above Nyquist." },
        { "tine_ratio3",     "Mode 3 Ratio",    "Tine", Unit::Ratio,      0.0f,    60.0f,   39.7f, true,
          "The third inharmonic mode, measured at 39.7. Only exists below about D5; above that it is past Nyquist and this does nothing." },
        { "tine_mode2_lvl",  "Mode 2 Level",    "Tine", Unit::Decibels, -100.0f,     0.0f,    0.0f, false,
          "Level of the second tine mode against the first." },
        { "tine_mode3_lvl",  "Mode 3 Level",    "Tine", Unit::Decibels, -100.0f,     0.0f,   -6.0f, false,
          "Level of the third tine mode against the first." },
        { "tine_hipass",     "Tine High-Pass",  "Tine", Unit::Hertz,     20.0f, 20000.0f,   20.0f, true,
          "Removes low frequencies from the signal driving the tine, before it reaches the modes.",
          "High-Pass" },
        { "tine_decay",      "Tine Decay",      "Tine", Unit::Q,          1.0f,  2000.0f,  225.0f, true,
          "How long the tine modes ring. This is the bark in the attack: short is percussive and woody, long is bell-like." },
        { "tine_mode_damp",  "Mode Damping",    "Tine", Unit::Ratio,      0.0f,     2.0f,    0.0f, true,
          "How much faster the upper modes decay than the first. Real modes damp progressively faster going up; 0 gives them all the same decay.",
          "Mode Damp" },
        // How much one key differs from the next -- fixed per key, not random.
        { "key_var",         "Key Variation",   "Tine", Unit::Ratio,      0.0f,     1.0f,   0.35f, true,
          "How much one key differs from the next in decay, tuning and level. Fixed per key rather than random, so a note sounds like itself every time. Tines are individually cut: measured decay varies fourfold with no pattern in pitch.",
          "Key Var" },

        // --- tone bar: what the tine is mounted on --------------------------
        { "tone_level",      "Tone Level",      "Tone Bar", Unit::Decibels, -100.0f,     0.0f,    0.0f, false,
          "How much of the tone bar, the resonator the tine is mounted on, goes straight to the output, bypassing the pickup. Turning it down does not mute the tone bar: the pickup carries it too, and always at full level." },
        { "tone_decay",      "Tone Decay",      "Tone Bar", Unit::Q,          1.0f,  4000.0f, 1750.0f, true,
          "How long the fundamental rings, measured at A4. This is the body of the note, as against the tine's attack." },
        { "q_tracking",      "Decay Tracking",  "Tone Bar", Unit::Ratio,      0.0f,     1.0f,  0.056f, true,
          "How much the decay changes with pitch. Nearly flat by default: the reference instrument shows no reliable trend of decay against pitch at all.",
          "Decay Track" },
        { "tone_release",    "Tone Release",    "Tone Bar", Unit::Q,          1.0f,  2000.0f,   30.0f, true,
          "How quickly the note dies once the key is up and the damper lands. Much shorter than the sustained decay." },
        { "sympathetic",     "Sympathetic",     "Tone Bar", Unit::Ratio,      0.0f,     1.0f,   0.35f, false,
          "How strongly the tines hear each other through the frame they share. Undamped tines ring in response to whatever else is sounding, which is most obvious with the pedal down." },
        { "sub_level",       "Sub Level",       "Tone Bar", Unit::Decibels, -100.0f,     0.0f,  -30.0f, false,
          "Level of the partial below the fundamental that the tone bar produces. Adds weight and depth without adding pitch." },
        { "sub_ratio",       "Sub Ratio",       "Tone Bar", Unit::Ratio,      0.2f,     1.0f,   0.55f, true,
          "Where that sub-partial sits, as a fraction of the note. Measured between 0.42 and 0.60 on a real instrument, and absent in the treble." },
        { "noteoff_level",   "Damper Thump",    "Tone Bar", Unit::Decibels, -100.0f,     0.0f,  -37.9f, false,
          "The thump of the damper landing on the tine when the key is released.",
          "Damper" },

        // --- pickup: how it is heard ----------------------------------------
        { "pickup_level",    "Pickup Level",    "Pickup", Unit::Decibels, -100.0f,     6.0f,    6.0f, false,
          "Level of the pickup's own output, which is most of what you hear.",
          "Level" },
        { "pickup_gain",     "Pickup Drive",    "Pickup", Unit::Decibels,    0.0f,    24.0f,   15.0f, false,
          "How hard the tine drives the pickup. Past a point the flux curve saturates and the tone thickens and distorts.",
          "Drive" },
        // Geometry, replacing the old symmetry exponent.  Offset is what
        // creates asymmetry, so it is now the Rhodes-to-Wurlitzer axis: at 0
        // the tine sits on the magnetic axis and the response is purely even.
        { "pickup_distance", "Pickup Distance", "Pickup", Unit::Ratio,      0.1f,     3.0f,    0.8f, false,
          "How far the tine rests from the pole piece. Near gives a sharp, spiky response; far is gentler and more linear.",
          "Distance" },
        { "pickup_offset",   "Pickup Offset",   "Pickup", Unit::Ratio,      0.0f,     1.5f,    0.8f, false,
          "How far the tine sits off the pickup's magnetic axis, and the strongest Rhodes-to-Wurlitzer control here. At 0 the response is purely even and the fundamental collapses under its own octave; low values bark, high values are clean and fundamental-heavy.",
          "Offset" },
        { "pickup_lopass",   "Coil Low-Pass",   "Pickup", Unit::Hertz,     20.0f, 20000.0f,  900.0f, true,
          "The coil's own inductance rolling off the top. With the pickup's rate-of-change response it makes a broad band-pass.",
          "Coil LP" },
        { "buzz_level",      "Buzz Level",      "Pickup", Unit::Decibels, -100.0f,     0.0f,    0.0f, false,
          "A fourth-power term added to the pickup output, putting a hard edge on the loud half of the waveform." },
        { "buzz_phase",      "Buzz Phase",      "Pickup", Unit::Toggle,     0.0f,     1.0f,    1.0f, false,
          "Which half of the waveform the buzz lands on. Changes whether the tone reads as hollow or forward." },
        // Off by default: this feeds the hammer pulse straight into the pickup,
        // which no real pickup sees.  It faked an attack the tine modes now
        // supply properly.
        { "pickup_attack",   "Hammer to Pickup", "Pickup", Unit::Decibels, -100.0f,   30.0f, -100.0f, false,
          "Feeds the hammer blow straight into the pickup, which no real pickup sees. Off by default: it faked an attack the tine modes now produce properly, and once differentiated it is audibly a click.",
          "Hammer Send" },

        // --- tremolo --------------------------------------------------------
        { "trem_stereo",     "Stereo",          "Tremolo", Unit::Toggle,     0.0f,     1.0f,    1.0f, false,
          "Swings the two channels in antiphase, as a suitcase does by panning between its two amplifiers. Off moves both together, which is the mono behaviour." },
        { "trem_on",         "Tremolo",         "Tremolo", Unit::Toggle,     0.0f,     1.0f,    0.0f, false,
          "Switches the tremolo: amplitude modulation, the effect a Rhodes suitcase has. Not vibrato, there is no pitch movement." },
        { "trem_depth",      "Tremolo Depth",   "Tremolo", Unit::Decibels, -100.0f,     0.0f,   -9.0f, false,
          "How far the tremolo swings the level.",
          "Depth" },
        { "trem_rate",       "Tremolo Rate",    "Tremolo", Unit::Hertz,      0.0f,    20.0f,    3.0f, false,
          "How fast the tremolo sweeps.",
          "Rate" },
        { "trem_shape",      "Tremolo Shape",   "Tremolo", Unit::Count,      0.0f,   127.0f,    0.0f, false,
          "Blends the tremolo shape from a sine to a triangle. The triangle has harder turnarounds.",
          "Shape" },

        // --- tuning: set once and left --------------------------------------
        { "bass_freq",       "Base Frequency",  "Tuning", Unit::Hertz,    100.0f, 20000.0f,  440.0f, true,
          "The reference pitch the whole keyboard is tuned from.",
          "Base Freq" },
        { "base_note",       "Base MIDI Note",  "Tuning", Unit::Count,      0.0f,   127.0f,   69.0f, true,
          "Which MIDI note sits at the reference pitch.",
          "Base Note" },
        { "divisions",       "Divisions",       "Tuning", Unit::Count,      1.0f,   100.0f,   12.0f, true,
          "How many steps the tuning interval is divided into. 12 is the usual semitone scale. Applies only when the Scale is Equal Divisions -- any other scale brings its own steps." },
        { "interval",        "Interval",        "Tuning", Unit::Ratio,      0.0f,    20.0f,    2.0f, true,
          "The interval the scale repeats over. 2 is the octave; other values give non-octave tunings. Applies only when the Scale is Equal Divisions -- any other scale brings its own period." },
    };
    return t;
}

// Panel colours, echoing the section headers of the original Pd layout.
inline juce::Colour sectionColour(const juce::String& section)
{
    if (section == "Output")   return juce::Colour(0xffc4fcc4);
    if (section == "Hammer")   return juce::Colour(0xffd8d8d8);
    if (section == "Tine")     return juce::Colour(0xfffcfcc4);
    if (section == "Tone Bar") return juce::Colour(0xfffcc4c4);
    if (section == "Pickup")   return juce::Colour(0xfffce0c4);
    if (section == "Tremolo")  return juce::Colour(0xffc4c4fc);
    if (section == "Tuning")   return juce::Colour(0xffe0c4fc);
    return juce::Colour(0xffdcdcdc);
}

inline juce::StringArray sectionOrder()
{
    // The two sections that are about the instrument as a whole, then signal
    // flow: hammer, tine, the bar it is mounted on, the pickup, the amplifier.
    //
    // The editor lays these out as three contiguous runs, one per column, so
    // this order is also the order they are read in -- and where the runs fall
    // decides how even the columns come out.  Output and Tuning lead because
    // they are the two that belong to no stage, and because putting them
    // together is what lets the split land evenly.
    return { "Output", "Tuning", "Hammer", "Tine", "Tone Bar", "Pickup", "Tremolo" };
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
