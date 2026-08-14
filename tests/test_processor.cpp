// Drives the plugin's AudioProcessor directly with MIDI, so the wrapper is
// exercised the way a host exercises it -- buffer handling, sample-accurate
// event scheduling, note-off, sustain pedal, all-notes-off -- without needing
// to load it into a DAW.
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>

#include "../plugin/PluginProcessor.h"
#include "../plugin/PluginEditor.h"

namespace {

float renderPeak(juce::AudioProcessor& p, juce::MidiBuffer& midi,
                 int numBlocks, int blockSize)
{
    juce::AudioBuffer<float> buf(2, blockSize);
    float peak = 0.0f;
    for (int b = 0; b < numBlocks; ++b) {
        buf.clear();
        p.processBlock(buf, midi);
        midi.clear();
        peak = std::max(peak, buf.getMagnitude(0, blockSize));
    }
    return peak;
}

int failures = 0;

void check(bool ok, const char* what, const char* detail = "")
{
    printf("  %-46s %s%s\n", what, ok ? "ok" : "FAILED", detail);
    if (!ok) ++failures;
}

} // namespace

int main()
{
    const double sr = 48000.0;
    const int block = 512;
    const int blocksPerSecond = int(sr) / block;

    EpMk2Processor proc;
    proc.setPlayConfigDetails(0, 2, sr, block);
    proc.prepareToPlay(sr, block);

    printf("EP-MK2 processor\n");
    check(proc.acceptsMidi(), "accepts MIDI");
    check(!proc.producesMidi(), "does not produce MIDI");
    check(proc.getTotalNumOutputChannels() == 2, "stereo output");

    // silence before anything is played
    {
        juce::MidiBuffer midi;
        check(renderPeak(proc, midi, 4, block) == 0.0f, "silent with no input");
    }

    // a note should sound
    float held = 0.0f;
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 45, (juce::uint8)100), 0);
        held = renderPeak(proc, midi, blocksPerSecond, block);
        char d[64]; snprintf(d, sizeof d, "  (peak %.4f)", held);
        check(held > 0.05f, "note on produces audio", d);
    }

    // and decay away after release
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOff(1, 45), 0);
        const float tail = renderPeak(proc, midi, blocksPerSecond * 3, block);
        const float after = renderPeak(proc, midi, blocksPerSecond, block);
        char d[64]; snprintf(d, sizeof d, "  (tail %.4f -> %.5f)", tail, after);
        check(after < held * 0.05f, "note off decays to silence", d);
    }

    // sustain pedal should hold a released note
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8)100), 1);
        renderPeak(proc, midi, blocksPerSecond / 2, block);

        juce::MidiBuffer off;
        off.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
        const float pedalled = renderPeak(proc, off, blocksPerSecond * 2, block);

        juce::MidiBuffer up;
        up.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
        renderPeak(proc, up, blocksPerSecond * 3, block);
        juce::MidiBuffer none;
        const float released = renderPeak(proc, none, blocksPerSecond, block);

        char d[80]; snprintf(d, sizeof d, "  (held %.4f, after pedal up %.5f)",
                             pedalled, released);
        check(pedalled > 0.02f && released < pedalled * 0.05f,
              "CC64 sustains, and releases on pedal up", d);
    }

    // polyphony: a chord should be louder than one note
    {
        juce::MidiBuffer midi;
        for (int n : { 45, 49, 52, 57 })
            midi.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8)100), 0);
        const float chord = renderPeak(proc, midi, blocksPerSecond, block);
        juce::MidiBuffer off;
        for (int n : { 45, 49, 52, 57 })
            off.addEvent(juce::MidiMessage::noteOff(1, n), 0);
        renderPeak(proc, off, blocksPerSecond * 4, block);
        char d[64]; snprintf(d, sizeof d, "  (chord %.4f vs note %.4f)", chord, held);
        check(chord > held, "four-note chord louder than one note", d);
    }

    // CC123 releases the keys: the note should ring on and then decay, not cut
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
        renderPeak(proc, midi, blocksPerSecond / 4, block);
        juce::MidiBuffer panic;
        panic.addEvent(juce::MidiMessage::allNotesOff(1), 0);
        const float justAfter = renderPeak(proc, panic, 2, block);
        juce::MidiBuffer none;
        const float later = renderPeak(proc, none, blocksPerSecond * 4, block);
        const float settled = renderPeak(proc, none, blocksPerSecond, block);
        char d[96]; snprintf(d, sizeof d, "  (%.4f -> %.4f -> %.6f)",
                             justAfter, later, settled);
        check(justAfter > 0.01f && settled < 1.0e-4f,
              "CC123 releases notes rather than cutting them", d);
    }

    // CC120 is immediate, filter state included
    {
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
        renderPeak(proc, midi, blocksPerSecond / 4, block);
        juce::MidiBuffer panic;
        panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
        renderPeak(proc, panic, 1, block);
        juce::MidiBuffer none;
        const float after = renderPeak(proc, none, 2, block);
        char d[64]; snprintf(d, sizeof d, "  (%.7f)", after);
        check(after < 1.0e-5f, "CC120 silences immediately", d);
    }

    // Parameters: every entry in the table must exist, defaults must survive a
    // state round-trip, and moving one must actually change the audio.
    {
        const auto& specs = epmk2::params::table();
        auto& tree = proc.getState();

        int missing = 0;
        for (const auto& sp : specs)
            if (tree.getParameter(sp.id) == nullptr) ++missing;
        char d[80]; snprintf(d, sizeof d, "  (%d exposed)", (int)specs.size());
        check(missing == 0, "every parameter in the table is exposed", d);

        int wrongDefault = 0;
        for (const auto& sp : specs) {
            const auto* raw = tree.getRawParameterValue(sp.id);
            if (raw != nullptr && std::fabs(raw->load() - sp.def) > 1.0e-3f) ++wrongDefault;
        }
        check(wrongDefault == 0, "defaults match the table");

        // A state round-trip should preserve a changed value.
        if (auto* pm = tree.getParameter("pickup_symmetry")) {
            pm->setValueNotifyingHost(pm->convertTo0to1(18.0f));
            juce::MemoryBlock blob;
            proc.getStateInformation(blob);
            pm->setValueNotifyingHost(pm->convertTo0to1(3.0f));
            proc.setStateInformation(blob.getData(), (int)blob.getSize());
            const float back = tree.getRawParameterValue("pickup_symmetry")->load();
            char e[64]; snprintf(e, sizeof e, "  (%.2f)", back);
            check(std::fabs(back - 18.0f) < 0.1f, "state round-trips a changed value", e);
            pm->setValueNotifyingHost(pm->convertTo0to1(7.0f));
        }

        // Moving a parameter must be audible.
        auto renderNote = [&](const char* id, float value) {
            if (auto* q = tree.getParameter(id))
                q->setValueNotifyingHost(q->convertTo0to1(value));
            proc.prepareToPlay(sr, block);
            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8)100), 0);
            juce::AudioBuffer<float> b(2, block);
            float energy = 0.0f;
            for (int i = 0; i < blocksPerSecond; ++i) {
                b.clear();
                proc.processBlock(b, m);
                m.clear();
                for (int n = 0; n < block; ++n) {
                    const float v = b.getSample(0, n);
                    energy += v * v;
                }
            }
            juce::MidiBuffer panic;
            panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            renderPeak(proc, panic, 1, block);
            return energy;
        };

        const float quiet = renderNote("master", -40.0f);
        const float loud  = renderNote("master", 0.0f);
        char m[80]; snprintf(m, sizeof m, "  (energy %.1f vs %.4f)", loud, quiet);
        check(loud > quiet * 50.0f, "master attenuates", m);

        const float dull   = renderNote("pickup_symmetry", 0.0f);
        const float barky  = renderNote("pickup_symmetry", 20.0f);
        char n2[80]; snprintf(n2, sizeof n2, "  (energy %.2f vs %.2f)", barky, dull);
        check(std::fabs(barky - dull) > dull * 0.05f, "pickup symmetry changes the tone", n2);
        renderNote("pickup_symmetry", 7.0f);
    }

    // The editor exists so hosts do not fall back to a generic view of JUCE's
    // ~2080 MIDI-CC parameters.  Render it through JUCE's software rasteriser
    // rather than a real window, so this is verifiable headlessly.
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        check(ed != nullptr, "creates an editor");
        if (ed != nullptr) {
            const int w = ed->getWidth(), h = ed->getHeight();
            char d[64]; snprintf(d, sizeof d, "  (%d x %d)", w, h);
            check(w >= 560 && h >= 420, "editor is panel-sized", d);

            // createComponentSnapshot paints the children too; paint() alone
            // would only draw the editor's own background.
            ed->setBounds(0, 0, w, h);
            juce::Image img = ed->createComponentSnapshot(ed->getLocalBounds());
            check(img.isValid(), "editor renders");

            // A real panel has many distinct colours: section headers in six
            // different tints, knobs, value boxes, text.
            std::set<juce::uint32> seen;
            for (int y = 0; y < h; y += 3)
                for (int x = 0; x < w; x += 3)
                    seen.insert(img.getPixelAt(x, y).getARGB());
            char c[64]; snprintf(c, sizeof c, "  (%d distinct colours)", (int)seen.size());
            check(seen.size() > 50, "panel has controls drawn on it", c);

            // Every parameter should have produced a visible control.
            int leaves = 0;
            std::function<void(juce::Component&)> countLeaves = [&](juce::Component& comp) {
                for (auto* child : comp.getChildren()) {
                    if (child->getNumChildComponents() == 0) ++leaves;
                    countLeaves(*child);
                }
            };
            countLeaves(*ed);
            char n3[80]; snprintf(n3, sizeof n3, "  (%d widgets for %d parameters)",
                                  leaves, (int)epmk2::params::table().size());
            check(leaves >= (int)epmk2::params::table().size(),
                  "every parameter has a control", n3);

            // Scaling: the panel is laid out at a design size and scaled by a
            // transform, so the header bar should grow in proportion.
            auto headerDepth = [](const juce::Image& im) {
                const auto top = im.getPixelAt(im.getWidth() / 2, 2);
                int y = 0;
                while (y < im.getHeight() && im.getPixelAt(im.getWidth() / 2, y) == top)
                    ++y;
                return y;
            };
            const int base = headerDepth(img);

            ed->setSize(int(w * 1.5f), int(h * 1.5f));
            juce::Image big = ed->createComponentSnapshot(ed->getLocalBounds());
            const int scaled = headerDepth(big);
            juce::File bigOut("/tmp/epmk2_editor_large.png");
            bigOut.deleteFile();
            juce::FileOutputStream bfs(bigOut);
            if (bfs.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream(big, bfs); }
            ed->setSize(w, h);

            char sc[96]; snprintf(sc, sizeof sc, "  (header %d px -> %d px at 1.5x)", base, scaled);
            check(scaled > int(base * 1.35f), "panel scales with the window", sc);

            juce::File out("/tmp/epmk2_editor.png");
            out.deleteFile();   // FileOutputStream does not truncate
            juce::FileOutputStream fs(out);
            if (fs.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream(img, fs); }
        }
    }

    printf("\n%s\n", failures ? "FAILED" : "all processor checks passed");
    return failures ? 1 : 0;
}
