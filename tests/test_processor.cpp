// Drives the plugin's AudioProcessor directly with MIDI, so the wrapper is
// exercised the way a host exercises it -- buffer handling, sample-accurate
// event scheduling, note-off, sustain pedal, all-notes-off -- without needing
// to load it into a DAW.
#include <cmath>
#include <cstdio>
#include <functional>
#include <set>
#include <vector>
#include <algorithm>

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

    // Sustain pedal.  The old version of this check passed even while CC64 was
    // being wiped out at every block boundary, because it only asked whether
    // anything was still audible.  What matters is the *difference* the pedal
    // makes, measured well after the block in which it was pressed.
    {
        auto playAndMeasure = [&](bool pedal, bool viaParam) {
            juce::MidiBuffer panic;
            panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            renderPeak(proc, panic, 1, block);
            if (auto* q = proc.getState().getParameter("sustain"))
                q->setValueNotifyingHost(pedal && viaParam ? 1.0f : 0.0f);

            juce::MidiBuffer midi;
            if (pedal && !viaParam)
                midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, (juce::uint8)100), 1);
            renderPeak(proc, midi, blocksPerSecond / 2, block);

            juce::MidiBuffer off;
            off.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
            // Let a full second pass and throw it away -- renderPeak reports
            // the peak across its whole window, so measuring through the decay
            // just measures the moment the key came up.  What matters is what
            // is left afterwards.
            juce::MidiBuffer none;
            renderPeak(proc, off, blocksPerSecond, block);
            return renderPeak(proc, none, blocksPerSecond / 4, block);
        };

        const float dry = playAndMeasure(false, false);
        const float cc  = playAndMeasure(true,  false);
        char d[96]; snprintf(d, sizeof d, "  (pedal %.4f vs no pedal %.5f)", cc, dry);
        check(cc > dry * 10.0f && cc > 0.01f, "CC64 holds a released note", d);

        const float panel = playAndMeasure(true, true);
        char d2[80]; snprintf(d2, sizeof d2, "  (%.4f)", panel);
        check(panel > dry * 10.0f && panel > 0.01f, "the panel toggle holds too", d2);

        // ...and letting it go must actually release.
        if (auto* q = proc.getState().getParameter("sustain"))
            q->setValueNotifyingHost(0.0f);
        juce::MidiBuffer up;
        up.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
        renderPeak(proc, up, blocksPerSecond * 3, block);
        juce::MidiBuffer none;
        const float released = renderPeak(proc, none, blocksPerSecond, block);
        char d3[80]; snprintf(d3, sizeof d3, "  (%.6f)", released);
        check(released < 0.01f, "releasing the pedal lets the note go", d3);
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

    // ---- resonator invariants --------------------------------------------
    // Tested on the filter itself rather than through the instrument, because
    // the audible symptom is a poor detector.  The hammer pulse leaking
    // through a resonator with direct feedthrough was a smooth hump that broke
    // no discontinuity check and sat only ~1.4x above the note's own level --
    // and it was plainly a click.  The property that was actually violated is
    // exact, so test that instead.
    {
        // A resonator's displacement cannot instantaneously follow the force
        // applied to it: h[0] must be zero.  With direct feedthrough the
        // excitation appears in the output as itself, at full level, which is
        // a click by construction.
        bool noFeedthrough = true;
        for (double f : { 30.0, 110.0, 440.0, 4000.0 })
            for (double q : { 5.0, 225.0, 1642.0 })
                if (epmk2::designBandpass((float)f, (float)q, 48000.0).ff1 != 0.0f)
                    noFeedthrough = false;
        check(noFeedthrough, "resonators have no direct feedthrough");

        // Ringing amplitude must not depend on frequency or Q -- that is what
        // lets decay time and level be set independently, and it is what makes
        // a keyboard-varying Q possible at all (roadmap 1.3).
        auto ringPeak = [](double f, double q) {
            epmk2::Biquad b;
            b.setCoeffs(epmk2::designBandpass((float)f, (float)q, 48000.0));
            float peak = 0.0f, x = 1.0f;
            for (int n = 0; n < 48000; ++n) {
                peak = std::max(peak, std::fabs(b.process(x)));
                x = 0.0f;
            }
            return peak;
        };
        float lo = 1.0e9f, hi = 0.0f;
        for (double f : { 30.0, 110.0, 440.0, 4000.0 })
            for (double q : { 225.0, 1642.0 }) {
                const float r = ringPeak(f, q);
                lo = std::min(lo, r);
                hi = std::max(hi, r);
            }
        char rp[96];
        snprintf(rp, sizeof rp, "  (%.3f to %.3f, %.1f dB spread)",
                 lo, hi, 20.0f * std::log10(hi / std::max(1.0e-9f, lo)));
        check(hi < lo * 2.0f, "ringing level is independent of pitch and Q", rp);
    }

    // ---- no click at the note onset --------------------------------------
    // A click is not necessarily a discontinuity, so the slope detector in
    // play_midi does not catch it: the hammer pulse leaking through a
    // resonator with direct feedthrough was a perfectly smooth hump, and still
    // a click.  What gives it away is that it does not scale with the note --
    // it is loudest, relative to the tone, at quiet velocities.
    //
    // So: the first few milliseconds must not stick out above what the note
    // settles to, at any velocity.
    {
        auto onsetVsBody = [&](int velocity, float& onset, float& body) {
            juce::MidiBuffer panic;
            panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            renderPeak(proc, panic, 1, block);

            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, 45, (juce::uint8)velocity), 0);
            juce::AudioBuffer<float> b(2, block);
            std::vector<float> out;
            for (int i = 0; i < blocksPerSecond / 2; ++i) {
                b.clear();
                proc.processBlock(b, m);
                m.clear();
                for (int n = 0; n < block; ++n)
                    out.push_back(b.getSample(0, n));
            }
            // The strike is delayed 2 ms; look at the 4 ms following it, then
            // at where the note settles.
            const int from = int(sr * 0.002), to = int(sr * 0.006);
            onset = 0.0f;
            for (int n = from; n < to && n < (int)out.size(); ++n)
                onset = std::max(onset, std::fabs(out[n]));
            body = 0.0f;
            for (int n = int(sr * 0.05); n < int(sr * 0.25) && n < (int)out.size(); ++n)
                body = std::max(body, std::fabs(out[n]));

            renderPeak(proc, panic, 1, block);
        };

        bool clean = true;
        for (int vel : { 20, 64, 117 }) {
            float onset = 0.0f, body = 0.0f;
            onsetVsBody(vel, onset, body);
            const float ratio = onset / std::max(1.0e-9f, body);
            printf("      velocity %3d: first 6 ms %.5f vs body %.5f  (%.2fx)\n",
                   vel, onset, body, ratio);
            if (ratio > 2.0f) clean = false;
        }
        check(clean, "no spike at the note onset");
    }

    // ---- restriking a still-ringing tine ---------------------------------
    // The hammer strikes a tine that is already moving, so the result depends
    // on where in its cycle the strike lands.  Two repeats at different gaps
    // must therefore differ -- and they must differ *because* of the phase,
    // with no randomness anywhere in the model.
    {
        // Strike, wait `gapBlocks`, strike again, and capture what follows.
        auto restrikeTail = [&](int gapBlocks, std::vector<float>& out) {
            juce::MidiBuffer panic;
            panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            renderPeak(proc, panic, 1, block);

            juce::MidiBuffer first;
            first.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8)100), 0);
            renderPeak(proc, first, gapBlocks, block);

            juce::MidiBuffer again;
            again.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8)100), 0);
            juce::AudioBuffer<float> b(2, block);
            out.clear();
            for (int i = 0; i < blocksPerSecond / 2; ++i) {
                b.clear();
                proc.processBlock(b, again);
                again.clear();
                for (int n = 0; n < block; ++n)
                    out.push_back(b.getSample(0, n));
            }
        };

        auto rms = [](const std::vector<float>& v) {
            double e = 0.0;
            for (float s : v) e += double(s) * double(s);
            return float(std::sqrt(e / std::max<size_t>(1, v.size())));
        };

        // Two gaps chosen to land the second strike at different points in the
        // tine's cycle.  A2 is ~110 Hz, so a single block at 48 kHz is already
        // more than a full period.
        std::vector<float> a, b2;
        restrikeTail(20, a);
        restrikeTail(23, b2);

        double d = 0.0;
        const size_t n = std::min(a.size(), b2.size());
        for (size_t k = 0; k < n; ++k) {
            const double delta = double(a[k]) - double(b2[k]);
            d += delta * delta;
        }
        const float diff = float(std::sqrt(d / std::max<size_t>(1, n)))
                         / std::max(1.0e-9f, rms(a));
        char rd[80]; snprintf(rd, sizeof rd, "  (%.1f%% apart)", diff * 100.0f);
        check(diff > 0.02f, "repeat strikes differ with strike phase", rd);

        // A restrike must reuse the same tine rather than stacking a second
        // voice on the same pitch.
        juce::MidiBuffer panic;
        panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
        renderPeak(proc, panic, 1, block);
        juce::MidiBuffer one;
        one.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8)100), 0);
        renderPeak(proc, one, 20, block);
        juce::MidiBuffer two;
        two.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8)100), 0);
        renderPeak(proc, two, 20, block);
        char vc[64]; snprintf(vc, sizeof vc, "  (%d voice)", proc.getActiveVoiceCount());
        check(proc.getActiveVoiceCount() == 1, "a repeat restrikes one tine", vc);

        renderPeak(proc, panic, 1, block);
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
        if (auto* pm = tree.getParameter("pickup_offset")) {
            pm->setValueNotifyingHost(pm->convertTo0to1(1.10f));
            juce::MemoryBlock blob;
            proc.getStateInformation(blob);
            pm->setValueNotifyingHost(pm->convertTo0to1(0.20f));
            proc.setStateInformation(blob.getData(), (int)blob.getSize());
            const float back = tree.getRawParameterValue("pickup_offset")->load();
            char e[64]; snprintf(e, sizeof e, "  (%.2f)", back);
            check(std::fabs(back - 1.10f) < 0.01f, "state round-trips a changed value", e);
            pm->setValueNotifyingHost(pm->convertTo0to1(0.45f));
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

        const float dull   = renderNote("pickup_offset", 0.05f);
        const float barky  = renderNote("pickup_offset", 1.30f);
        char n2[80]; snprintf(n2, sizeof n2, "  (energy %.2f vs %.2f)", barky, dull);
        check(std::fabs(barky - dull) > dull * 0.05f, "pickup offset changes the tone", n2);
        renderNote("pickup_offset", 0.45f);
    }

    // ---- tine modes and the Nyquist cull ---------------------------------
    // A mode past Nyquist has to be skipped, not clamped.  designBandpass
    // clips to 20 kHz, so clamping parks a Q-225 resonator just under Nyquist
    // where the real instrument has nothing -- which is what used to happen to
    // mode 2 above roughly C6.
    //
    // The measurement is a Goertzel at the mode's own frequency, not broadband
    // energy.  The modes sit 40-75 dB below the fundamental (see
    // tests/probe_modes.cpp and docs/ROADMAP.md 1.6), so they are invisible in
    // any wideband measure even when working perfectly.
    {
        auto goertzel = [](const std::vector<float>& x, double freq, double rate) {
            const double w = 2.0 * M_PI * freq / rate;
            const double c = 2.0 * std::cos(w);
            double s0 = 0.0, s1 = 0.0, s2 = 0.0;
            for (float v : x) { s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
            return std::sqrt(std::max(0.0, s1*s1 + s2*s2 - c*s1*s2)) / std::max<size_t>(1, x.size());
        };

        auto renderNoteAt = [&](int midiNote, const char* id, float valueDb,
                                std::vector<float>& out) {
            if (auto* q = proc.getState().getParameter(id))
                q->setValueNotifyingHost(q->convertTo0to1(valueDb));
            proc.prepareToPlay(sr, block);

            juce::MidiBuffer panic;
            panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            renderPeak(proc, panic, 1, block);

            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, midiNote, (juce::uint8)110), 0);
            juce::AudioBuffer<float> b(2, block);
            out.clear();
            for (int i = 0; i < blocksPerSecond; ++i) {
                b.clear();
                proc.processBlock(b, m);
                m.clear();
                for (int n = 0; n < block; ++n)
                    out.push_back(b.getSample(0, n));
            }
            renderPeak(proc, panic, 1, block);
        };

        // Note 45 is ~110 Hz: modes at 781 Hz, 2244 Hz and 4367 Hz, all real.
        const double f0low = 110.0;
        std::vector<float> off, on;

        renderNoteAt(45, "tine_mode3_lvl", -100.0f, off);
        renderNoteAt(45, "tine_mode3_lvl",    0.0f, on);
        const double m3Off = goertzel(off, f0low * 39.7, sr);
        const double m3On  = goertzel(on,  f0low * 39.7, sr);
        char d1[96]; snprintf(d1, sizeof d1, "  (%.1f -> %.1f dB)",
                              20.0 * std::log10(std::max(1e-12, m3Off)),
                              20.0 * std::log10(std::max(1e-12, m3On)));
        check(m3On > m3Off * 2.0, "mode 3 sounds where it fits below Nyquist", d1);

        renderNoteAt(45, "tine_mode2_lvl", -100.0f, off);
        renderNoteAt(45, "tine_mode2_lvl",    0.0f, on);
        const double m2Off = goertzel(off, f0low * 20.4, sr);
        const double m2On  = goertzel(on,  f0low * 20.4, sr);
        char d2[96]; snprintf(d2, sizeof d2, "  (%.1f -> %.1f dB)",
                              20.0 * std::log10(std::max(1e-12, m2Off)),
                              20.0 * std::log10(std::max(1e-12, m2On)));
        check(m2On > m2Off * 2.0, "mode 2 sounds where it fits below Nyquist", d2);

        // Note 108 is ~4186 Hz: modes at 85 kHz and 166 kHz, neither of which
        // can exist at 48 kHz.  Their controls must be completely inert --
        // bit-identical output, because the resonators are never run.
        renderNoteAt(108, "tine_mode3_lvl", -100.0f, off);
        renderNoteAt(108, "tine_mode3_lvl",    0.0f, on);
        check(off == on, "mode 3 is inert where it would alias");

        renderNoteAt(108, "tine_mode2_lvl", -100.0f, off);
        renderNoteAt(108, "tine_mode2_lvl",    0.0f, on);
        check(off == on, "mode 2 is inert where it would alias");

        // Restore the defaults this block moved.
        if (auto* q = proc.getState().getParameter("tine_mode3_lvl"))
            q->setValueNotifyingHost(q->convertTo0to1(-6.0f));
        if (auto* q = proc.getState().getParameter("tine_mode2_lvl"))
            q->setValueNotifyingHost(q->convertTo0to1(0.0f));
        proc.prepareToPlay(sr, block);
    }

    // ---- Q varies across the keyboard ------------------------------------
    // The point of the resonator normalisation: Q sets decay and nothing else.
    // Under the old constant-skirt-gain form, sweeping Q across the keyboard
    // would have swung the level with it by ~9.5 dB as a pure artefact, which
    // is why this could not be done before.
    {
        auto decayAndPeak = [&](int midiNote, float tracking,
                                float& tail, float& peak) {
            if (auto* q = proc.getState().getParameter("q_tracking"))
                q->setValueNotifyingHost(q->convertTo0to1(tracking));
            proc.prepareToPlay(sr, block);

            juce::MidiBuffer panic;
            panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            renderPeak(proc, panic, 1, block);

            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, midiNote, (juce::uint8)110), 0);
            peak = renderPeak(proc, m, blocksPerSecond / 4, block);
            // How much is left after two seconds, with the key still held.
            juce::MidiBuffer none;
            renderPeak(proc, none, blocksPerSecond * 2, block);
            tail = renderPeak(proc, none, blocksPerSecond / 4, block);

            renderPeak(proc, panic, 1, block);
        };

        float flatLowTail = 0.0f, flatLowPeak = 0.0f;
        float trkLowTail  = 0.0f, trkLowPeak  = 0.0f;
        decayAndPeak(33, 0.0f,   flatLowTail, flatLowPeak);
        decayAndPeak(33, 0.217f, trkLowTail,  trkLowPeak);

        float flatHiTail = 0.0f, flatHiPeak = 0.0f;
        float trkHiTail  = 0.0f, trkHiPeak  = 0.0f;
        decayAndPeak(93, 0.0f,   flatHiTail, flatHiPeak);
        decayAndPeak(93, 0.217f, trkHiTail,  trkHiPeak);

        char d1[128];
        snprintf(d1, sizeof d1, "  (bass %.4f -> %.4f, treble %.5f -> %.5f)",
                 flatLowTail, trkLowTail, flatHiTail, trkHiTail);
        // Tracking lowers Q in the bass and raises it in the treble, so the
        // bass should decay further in the same time and the treble less.
        check(trkLowTail < flatLowTail * 0.9f && trkHiTail > flatHiTail * 1.1f,
              "decay tracking shortens the bass and lengthens the treble", d1);

        char d2[128];
        snprintf(d2, sizeof d2, "  (bass %.4f vs %.4f, treble %.4f vs %.4f)",
                 flatLowPeak, trkLowPeak, flatHiPeak, trkHiPeak);
        check(std::fabs(trkLowPeak - flatLowPeak) < flatLowPeak * 0.02f
              && std::fabs(trkHiPeak - flatHiPeak) < flatHiPeak * 0.02f,
              "changing Q does not change level", d2);

        if (auto* q = proc.getState().getParameter("q_tracking"))
            q->setValueNotifyingHost(q->convertTo0to1(0.217f));
        proc.prepareToPlay(sr, block);
    }

    // ---- factory presets -------------------------------------------------
    // A preset that loads but sounds like the one before it is not a preset,
    // so each is measured for level and brightness rather than merely checked
    // for not crashing.
    {
        const int numPresets = proc.getNumPrograms();
        char np[64]; snprintf(np, sizeof np, "  (%d presets)", numPresets);
        check(numPresets >= 4, "ships factory presets", np);

        // Render a whole note per preset and keep it, so presets can be
        // compared against Rhodes waveform-to-waveform.  Zero-crossing rate
        // is no use here: it comes out at 2*f0/SR for every preset because
        // the fundamental dominates, and says nothing about timbre.
        auto render = [&](int program, std::vector<float>& out) {
            proc.setCurrentProgram(program);
            out.clear();

            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, 57, (juce::uint8)100), 0);
            juce::AudioBuffer<float> b(2, block);
            for (int i = 0; i < blocksPerSecond; ++i) {
                b.clear();
                proc.processBlock(b, m);
                m.clear();
                for (int n = 0; n < block; ++n)
                    out.push_back(b.getSample(0, n));
            }

            juce::MidiBuffer panic;
            panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            renderPeak(proc, panic, 1, block);
        };

        auto rmsOf = [](const std::vector<float>& v) {
            double e = 0.0;
            for (float s : v) e += double(s) * double(s);
            return float(std::sqrt(e / std::max<size_t>(1, v.size())));
        };
        // Energy of the first difference over total energy: proportional to
        // mean squared frequency, so it rises with brightness.  Unlike ZCR it
        // responds to harmonics above the fundamental.
        auto brightnessOf = [](const std::vector<float>& v) {
            double hi = 0.0, all = 0.0;
            for (size_t n = 1; n < v.size(); ++n) {
                const double d = double(v[n]) - double(v[n-1]);
                hi  += d * d;
                all += double(v[n]) * double(v[n]);
            }
            return all > 0.0 ? float(std::sqrt(hi / all)) : 0.0f;
        };

        bool allNamed = true, allSound = true, allDistinct = true;
        std::vector<float> rhodes, current;

        for (int i = 0; i < numPresets; ++i) {
            const juce::String name = proc.getProgramName(i);
            if (name.isEmpty()) allNamed = false;

            render(i, current);
            const float rms = rmsOf(current);
            const float bright = brightnessOf(current);
            if (!(rms > 1.0e-4f)) allSound = false;

            float difference = 0.0f;
            if (i == 0) {
                rhodes = current;
            } else {
                double d = 0.0;
                const size_t n = std::min(rhodes.size(), current.size());
                for (size_t k = 0; k < n; ++k) {
                    const double delta = double(current[k]) - double(rhodes[k]);
                    d += delta * delta;
                }
                difference = float(std::sqrt(d / std::max<size_t>(1, n)))
                           / std::max(1.0e-9f, rmsOf(rhodes));
                if (difference < 0.05f) allDistinct = false;
            }

            printf("      %-16s rms %.4f  brightness %.4f  differs from Rhodes %5.1f%%\n",
                   name.toRawUTF8(), rms, bright, difference * 100.0f);
        }

        check(allNamed, "every preset is named");
        check(allSound, "every preset produces sound");
        check(allDistinct, "every preset differs from Rhodes");

        // Presets are overrides on the defaults, so switching away and back
        // must land where it started rather than accumulating.
        proc.setCurrentProgram(0);
        const float rhodesRatio1 = proc.getState().getParameter("tine_ratio1")->getValue();
        proc.setCurrentProgram(2);
        proc.setCurrentProgram(0);
        const float backAgain = proc.getState().getParameter("tine_ratio1")->getValue();
        check(std::fabs(backAgain - rhodesRatio1) < 1.0e-6f,
              "switching presets is not cumulative");

        // Tuning, level and polyphony belong to the player, not the preset.
        auto* master = proc.getState().getParameter("master");
        auto* poly   = proc.getState().getParameter("polyphony");
        master->setValueNotifyingHost(master->convertTo0to1(-12.0f));
        poly->setValueNotifyingHost(poly->convertTo0to1(16.0f));
        proc.setCurrentProgram(3);
        const float keptMaster = master->convertFrom0to1(master->getValue());
        const float keptPoly   = poly->convertFrom0to1(poly->getValue());
        char kept[80];
        snprintf(kept, sizeof kept, "  (master %.1f dB, poly %.0f)", keptMaster, keptPoly);
        check(std::fabs(keptMaster + 12.0f) < 0.5f && std::fabs(keptPoly - 16.0f) < 0.5f,
              "presets leave level and polyphony alone", kept);

        master->setValueNotifyingHost(master->convertTo0to1(0.0f));
        poly->setValueNotifyingHost(poly->convertTo0to1(32.0f));
        proc.setCurrentProgram(0);
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

            // Every control must sit inside the section that owns it.  The
            // tine section quietly grew from 6 controls to 10 as modes were
            // added, and an equal-height grid cut the bottom row off.
            int overflowing = 0;
            juce::String worstName;
            std::function<void(juce::Component*)> walk = [&](juce::Component* comp) {
                for (int k = 0; k < comp->getNumChildComponents(); ++k) {
                    auto* child = comp->getChildComponent(k);
                    if (auto* sec = dynamic_cast<ParamSection*>(child)) {
                        const int bad = sec->controlsNotPlaced();
                        if (bad > 0) {
                            overflowing += bad;
                            worstName = sec->getName();
                        }
                    }
                    walk(child);
                }
            };
            walk(ed.get());
            char ov[128];
            snprintf(ov, sizeof ov, "  (%d control%s without a place%s%s)",
                     overflowing, overflowing == 1 ? "" : "s",
                     overflowing ? ", worst: " : "",
                     overflowing ? worstName.toRawUTF8() : "");
            check(overflowing == 0, "every control fits in its section", ov);

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
