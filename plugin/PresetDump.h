#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "Presets.h"

// Dumping the current settings as text, for getting a sound that was arrived
// at by ear back into the source tree.
//
// This is a development affordance, not a feature: there is no preset browser
// here and there should not be, because a DAW already manages user presets
// better than a plugin can.  What a DAW cannot do is hand a sound back to the
// person maintaining the factory table without someone transcribing forty
// numbers by hand, which is what this is for.
namespace epmk2::presetdump {

// Two halves.  The first is every in-scope control, in panel units, for
// reading and diffing.  The second is only what differs from the defaults,
// formatted as an entry in Presets.h -- because a preset in this project is a
// set of overrides, and the overrides are the part worth keeping.
inline juce::String text(const juce::AudioProcessorValueTreeState& state,
                         const juce::String& name)
{
    juce::StringArray all, overrides;

    for (const auto& spec : params::table()) {
        if (! presets::inPresetScope(spec.id))
            continue;

        auto* p = state.getParameter(spec.id);
        if (p == nullptr)
            continue;
        const float v = p->convertFrom0to1(p->getValue());

        all.add(juce::String(spec.id).paddedRight(' ', 18) + " = "
                + juce::String(v, 4) + "   # " + spec.name);

        // What counts as changed has to allow for the round trip through a
        // normalised parameter: a control that was never touched can come
        // back a hair off its own default.
        const float tolerance = juce::jmax(1.0e-4f, std::abs(spec.def) * 1.0e-4f);
        if (std::abs(v - spec.def) > tolerance)
            overrides.add((juce::String("            { \"") + spec.id + "\",")
                              .paddedRight(' ', 32)
                          + juce::String(v, 4) + "f },");
    }

    juce::StringArray out;
    out.add("EP-MK2 settings dump");
    out.add("saved " + juce::Time::getCurrentTime().toString(true, true, false, true));
    out.add("");
    out.add("--- everything, in panel units ---");
    out.add("");
    out.addArray(all);
    out.add("");
    out.add("--- what differs from the defaults, as a Presets.h entry ---");
    out.add("");
    out.add("        { \"" + name + "\", {");
    if (overrides.isEmpty())
        out.add("            // nothing: this is the shipped default");
    else
        out.addArray(overrides);
    out.add("        }},");
    out.add("");
    return out.joinIntoString("\n");
}

// Where the dumps go.  Documents rather than the config directory the window
// size lives in: this is a file meant to be found and sent to someone.
//
// Not JUCE's userDocumentsDirectory on Linux, which is the *home* directory --
// the same trap the settings file already had a comment about.  With the
// project checked out at ~/EP-MK2 that put the dumps inside the source tree,
// where they were neither found nor safe.
inline juce::File folder()
{
    auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

   #if JUCE_LINUX || JUCE_BSD
    auto documents = juce::File(
        juce::SystemStats::getEnvironmentVariable("XDG_DOCUMENTS_DIR", {}));
    if (! documents.isDirectory())
        documents = home.getChildFile("Documents");
   #else
    auto documents = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
   #endif

    if (! documents.isDirectory())
        documents = home;
    return documents.getChildFile("EP-MK2").getChildFile("dumps");
}

// Returns the file written, or a null File if it could not be.
inline juce::File write(const juce::AudioProcessorValueTreeState& state,
                        const juce::String& name)
{
    auto dir = folder();
    if (! dir.createDirectory())
        return {};

    // Timestamped, because the point is to compare several attempts at the
    // same sound, and a fixed name would leave only the last one.
    const auto stamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S");
    auto file = dir.getChildFile(name + " " + stamp + ".txt");
    return file.replaceWithText(text(state, name)) ? file : juce::File();
}

} // namespace epmk2::presetdump
