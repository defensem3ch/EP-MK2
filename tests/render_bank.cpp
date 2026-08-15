// Render the model the way the reference library was captured: one file per
// pitch and velocity, held for ten seconds and then released.
//
// The point is to put the model's output through *the same* analysis as the
// instrument's, rather than measuring each with whatever was to hand.  Anything
// the comparison then shows is a difference in the sound, not in the method.
//
//   ./epmk2-bank out/dir [--seconds 10] [--tail 0.5]
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <utility>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "../plugin/PluginProcessor.h"
#include "../plugin/Presets.h"

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: %s <output dir> [--seconds 10] [--tail 0.5]\n", argv[0]);
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File dir(juce::File::getCurrentWorkingDirectory()
                             .getChildFile(juce::String(argv[1])));
    double seconds = 10.0, tail = 0.5;
    // Which factory preset to start from, by name.  Without this a bank can
    // only ever be the defaults, so nothing but Rhodes MkI could be compared
    // against a recording of the instrument it is meant to be.
    juce::String presetName;
    std::vector<int> onlyNotes, onlyVelocities;
    // --param <id> <value>, repeatable: renders a variant without rebuilding,
    // so a hypothesis about one control can be measured against the instrument
    // rather than argued about.
    std::vector<std::pair<juce::String, float>> overrides;
    for (int i = 2; i < argc - 1; ++i) {
        if      (!strcmp(argv[i], "--seconds")) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--tail"))    tail    = atof(argv[++i]);
        else if (!strcmp(argv[i], "--preset")) presetName = argv[++i];
        else if (!strcmp(argv[i], "--notes")) {
            for (const auto& n : juce::StringArray::fromTokens(argv[++i], ",", ""))
                onlyNotes.push_back(n.getIntValue());
        }
        else if (!strcmp(argv[i], "--velocities")) {
            for (const auto& v : juce::StringArray::fromTokens(argv[++i], ",", ""))
                onlyVelocities.push_back(v.getIntValue());
        }
        else if (!strcmp(argv[i], "--param") && i + 2 < argc) {
            overrides.push_back({ juce::String(argv[i + 1]), (float) atof(argv[i + 2]) });
            i += 2;
        }
    }
    int presetIndex = -1;
    if (presetName.isNotEmpty()) {
        const auto& table = epmk2::presets::table();
        for (size_t i = 0; i < table.size(); ++i)
            if (presetName.equalsIgnoreCase(table[i].name))
                presetIndex = (int) i;
        if (presetIndex < 0) {
            printf("no preset called \"%s\".  There is:\n", presetName.toRawUTF8());
            for (const auto& p : table)
                printf("  %s\n", p.name);
            return 1;
        }
        printf("  preset: %s\n", table[(size_t) presetIndex].name);
    }
    for (const auto& o : overrides)
        printf("  %s = %.4f\n", o.first.toRawUTF8(), o.second);
    dir.createDirectory();

    // The library's own grid: every third semitone from E1 to E7, and the
    // twelve velocity layers Kontakt was driven with.
    const int velocities[] = { 0x0B, 0x16, 0x20, 0x2B, 0x35, 0x40,
                               0x4A, 0x55, 0x5F, 0x6A, 0x74, 0x7F };

    const double sr = 48000.0;
    const int block = 512;
    int written = 0;

    for (int note = 28; note <= 100; note += 3) {
        if (! onlyNotes.empty()
            && std::find(onlyNotes.begin(), onlyNotes.end(), note) == onlyNotes.end())
            continue;
        for (int velocity : velocities) {
            if (! onlyVelocities.empty()
                && std::find(onlyVelocities.begin(), onlyVelocities.end(), velocity)
                       == onlyVelocities.end())
                continue;
            EpMk2Processor proc;
            proc.setPlayConfigDetails(0, 2, sr, block);
            proc.prepareToPlay(sr, block);
            // Before the variation is switched off and before the overrides,
            // so a preset is a starting point rather than the last word.
            if (presetIndex >= 0)
                epmk2::presets::apply(proc.getState(), presetIndex);
            // Variation off: the comparison is with one instrument, not with a
            // distribution, and a render has to be repeatable to be argued with.
            if (auto* p = proc.getState().getParameter("key_var"))
                p->setValueNotifyingHost(0.0f);
            if (auto* p = proc.getState().getParameter("strike_var"))
                p->setValueNotifyingHost(0.0f);
            for (const auto& o : overrides)
                if (auto* p = proc.getState().getParameter(o.first))
                    p->setValueNotifyingHost(p->convertTo0to1(o.second));

            const int held = int(seconds * sr);
            const int total = held + int(tail * sr);
            juce::AudioBuffer<float> out(1, total);
            out.clear();

            juce::AudioBuffer<float> buf(2, block);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) velocity), 0);

            for (int pos = 0; pos < total; pos += block) {
                const int n = juce::jmin(block, total - pos);
                buf.setSize(2, n, false, false, true);
                buf.clear();

                // Release exactly where the library's notes were released.
                if (pos <= held && pos + n > held) {
                    midi.addEvent(juce::MidiMessage::noteOff(1, note),
                                  juce::jlimit(0, n - 1, held - pos));
                }
                proc.processBlock(buf, midi);
                midi.clear();
                out.copyFrom(0, pos, buf, 0, 0, n);
            }

            auto file = dir.getChildFile(juce::String::formatted(
                "note_%03d_vel_%03d.wav", note, velocity));
            file.deleteFile();
            juce::WavAudioFormat wav;
            if (auto* stream = file.createOutputStream().release()) {
                std::unique_ptr<juce::AudioFormatWriter> w(
                    wav.createWriterFor(stream, sr, 1, 24, {}, 0));
                if (w != nullptr)
                    w->writeFromAudioSampleBuffer(out, 0, total);
            }
            ++written;
        }
        printf("  note %3d done\n", note);
        fflush(stdout);
    }

    printf("wrote %d files to %s\n", written, dir.getFullPathName().toRawUTF8());
    return 0;
}
