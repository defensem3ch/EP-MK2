// Drives the plugin's AudioProcessor directly with MIDI, so the wrapper is
// exercised the way a host exercises it -- buffer handling, sample-accurate
// event scheduling, note-off, sustain pedal, all-notes-off -- without needing
// to load it into a DAW.
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
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
        // Exercised at a strong setting, not the shipped default: the default
        // is deliberately near flat, because the reference library shows no
        // reliable trend of Q against pitch (r2 0.02).  What is being tested
        // is that the control works, not what it is set to.
        decayAndPeak(33, 0.0f, flatLowTail, flatLowPeak);
        decayAndPeak(33, 0.5f, trkLowTail,  trkLowPeak);

        float flatHiTail = 0.0f, flatHiPeak = 0.0f;
        float trkHiTail  = 0.0f, trkHiPeak  = 0.0f;
        decayAndPeak(93, 0.0f, flatHiTail, flatHiPeak);
        decayAndPeak(93, 0.5f, trkHiTail,  trkHiPeak);

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
            q->setValueNotifyingHost(q->convertTo0to1(0.056f));
        proc.prepareToPlay(sr, block);
    }

    // ---- moving a control must not click ---------------------------------
    // Hosts deliver parameter changes once per block, so a knob drag is a
    // series of steps.  The pickup geometry rebuilds a lookup table, and the
    // pickup differentiates its output -- so differencing this sample's flux
    // against last sample's *stored* flux also differentiated the table change
    // itself.  Stepping pickup_offset from 0.8 to 0 jumped the output by 0.33,
    // as loud as the note.
    {
        auto stepMidNote = [&](const char* id, float from, float to) {
            juce::MidiBuffer panic;
            panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            renderPeak(proc, panic, 1, block);

            auto* q = proc.getState().getParameter(id);
            if (q == nullptr)
                return 0.0f;
            q->setValueNotifyingHost(q->convertTo0to1(from));
            proc.prepareToPlay(sr, block);

            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, 45, (juce::uint8)110), 0);
            juce::AudioBuffer<float> b(2, block);
            std::vector<float> out;
            const int blocks = blocksPerSecond;
            for (int i = 0; i < blocks; ++i) {
                if (i == blocks / 2)
                    q->setValueNotifyingHost(q->convertTo0to1(to));
                b.clear();
                proc.processBlock(b, m);
                m.clear();
                for (int n = 0; n < block; ++n)
                    out.push_back(b.getSample(0, n));
            }
            renderPeak(proc, panic, 1, block);

            // The largest jump in the few samples either side of the change.
            const int at = (blocks / 2) * block;
            float jump = 0.0f;
            for (int n = at - 4; n < at + 12 && n < (int)out.size(); ++n)
                jump = std::max(jump, std::fabs(out[(size_t)n] - out[(size_t)n - 1]));
            return jump;
        };

        struct { const char* id; float from, to; } moves[] = {
            { "pickup_offset",   0.80f, 0.00f },
            { "pickup_offset",   0.00f, 1.50f },
            { "pickup_distance", 0.80f, 0.10f },
            { "pickup_distance", 3.00f, 0.10f },
            { "tone_decay",   1334.0f, 200.0f },
        };
        float worst = 0.0f;
        const char* worstId = "";
        for (const auto& mv : moves) {
            const float j = stepMidNote(mv.id, mv.from, mv.to);
            if (j > worst) { worst = j; worstId = mv.id; }
        }
        char sj[96];
        snprintf(sj, sizeof sj, "  (worst %.4f, %s)", worst, worstId);
        check(worst < 0.02f, "stepping a control mid-note does not click", sj);

        // Put the defaults back.
        for (const auto& spec : epmk2::params::table())
            if (auto* q = proc.getState().getParameter(spec.id))
                q->setValueNotifyingHost(q->convertTo0to1(spec.def));
        proc.prepareToPlay(sr, block);
    }

    // ---- sub-fundamental -------------------------------------------------
    // A partial below f0, produced by the tone bar.  Measured in the reference
    // library at 0.42-0.60 x f0 from note 28 to 70; the model places it at
    // 0.55.  It has to survive the pickup's body highpass, which sits at f0
    // and so attenuates everything below it -- the kind of interaction that
    // silently makes a feature do nothing.
    {
        auto goertzel = [](const std::vector<float>& v, double freq, double rate) {
            const double w = 2.0 * M_PI * freq / rate;
            const double c = 2.0 * std::cos(w);
            double s0 = 0.0, s1 = 0.0, s2 = 0.0;
            for (float x : v) { s0 = x + c * s1 - s2; s2 = s1; s1 = s0; }
            return std::sqrt(std::max(0.0, s1*s1 + s2*s2 - c*s1*s2))
                   / std::max<size_t>(1, v.size());
        };

        auto renderAt = [&](int midiNote, float subDb, std::vector<float>& out) {
            if (auto* q = proc.getState().getParameter("sub_level"))
                q->setValueNotifyingHost(q->convertTo0to1(subDb));
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

        std::vector<float> off, on;
        const double f0 = 440.0 * std::pow(2.0, (52 - 69) / 12.0);   // note 52
        renderAt(52, -100.0f, off);
        renderAt(52,    0.0f, on);
        const double subOff = goertzel(off, f0 * 0.55, sr);
        const double subOn  = goertzel(on,  f0 * 0.55, sr);
        char sd[96];
        snprintf(sd, sizeof sd, "  (%.1f -> %.1f dB at %.0f Hz)",
                 20.0 * std::log10(std::max(1e-12, subOff)),
                 20.0 * std::log10(std::max(1e-12, subOn)), f0 * 0.55);
        check(subOn > subOff * 4.0, "the sub-fundamental reaches the output", sd);

        // Below 20 Hz there is nothing to hear, and designBandpass would clamp
        // it up to 20 Hz -- putting a resonator at a frequency the note does
        // not have.  Note 21 puts the sub at 15 Hz, so it must be skipped.
        renderAt(21, -100.0f, off);
        renderAt(21,    0.0f, on);
        check(off == on, "the sub is skipped when it falls below hearing");

        if (auto* q = proc.getState().getParameter("sub_level"))
            q->setValueNotifyingHost(q->convertTo0to1(-30.0f));
        proc.prepareToPlay(sr, block);
    }

    // ---- variation: no two notes quite alike -----------------------------
    // Two different things, and they must not be confused.  Key variation is
    // fixed per key -- tines are individually cut, and the reference library
    // shows Q scattering 900 to 3600 with no pattern in pitch -- so a note
    // sounds like itself every time.  Strike variation is random, and is the
    // thing a sample cannot do.
    {
        auto renderNotes = [&](float keyVar, float strikeVar,
                               const std::vector<int>& notes, std::vector<float>& out) {
            if (auto* q = proc.getState().getParameter("key_var"))
                q->setValueNotifyingHost(q->convertTo0to1(keyVar));
            if (auto* q = proc.getState().getParameter("strike_var"))
                q->setValueNotifyingHost(q->convertTo0to1(strikeVar));
            proc.prepareToPlay(sr, block);   // reseeds, so renders repeat

            juce::AudioBuffer<float> b(2, block);
            out.clear();
            for (int n : notes) {
                juce::MidiBuffer panic;
                panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
                renderPeak(proc, panic, 1, block);

                juce::MidiBuffer m;
                m.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8)100), 0);
                for (int i = 0; i < blocksPerSecond / 3; ++i) {
                    b.clear();
                    proc.processBlock(b, m);
                    m.clear();
                    for (int k = 0; k < block; ++k)
                        out.push_back(b.getSample(0, k));
                }
            }
        };

        auto rms = [](const std::vector<float>& v) {
            double e = 0.0;
            for (float x : v) e += double(x) * double(x);
            return std::sqrt(e / std::max<size_t>(1, v.size()));
        };
        auto difference = [&](const std::vector<float>& a, const std::vector<float>& b) {
            double d = 0.0;
            const size_t n = std::min(a.size(), b.size());
            for (size_t i = 0; i < n; ++i) {
                const double x = double(a[i]) - double(b[i]);
                d += x * x;
            }
            return std::sqrt(d / std::max<size_t>(1, n)) / std::max(1.0e-12, rms(a));
        };

        const std::vector<int> one { 45 }, scale { 45, 46, 47, 48 };
        std::vector<float> a, b;

        // Off means exactly off: the old behaviour, sample for sample.
        renderNotes(0.0f, 0.0f, one, a);
        renderNotes(0.0f, 0.0f, one, b);
        check(a == b, "with variation off, output is bit-identical");

        // On, it still has to be reproducible, or nothing downstream --
        // rendering, testing, bouncing a track twice -- can be trusted.
        renderNotes(0.35f, 0.30f, one, a);
        renderNotes(0.35f, 0.30f, one, b);
        check(a == b, "with variation on, a render still repeats exactly");

        // Neighbouring keys differ from each other even with strike variation
        // off, because the difference is in the tines, not the playing.
        std::vector<float> flat, varied;
        renderNotes(0.0f,  0.0f, scale, flat);
        renderNotes(0.60f, 0.0f, scale, varied);
        char kv[80];
        snprintf(kv, sizeof kv, "  (%.1f%% apart)", difference(flat, varied) * 100.0);
        check(difference(flat, varied) > 0.02, "each key has its own character", kv);

        if (auto* q = proc.getState().getParameter("key_var"))
            q->setValueNotifyingHost(q->convertTo0to1(0.35f));
        if (auto* q = proc.getState().getParameter("strike_var"))
            q->setValueNotifyingHost(q->convertTo0to1(0.30f));
        proc.prepareToPlay(sr, block);
    }

    // ---- the same instrument at any sample rate --------------------------
    // The resonators sum their input sample by sample while the strike is
    // defined in seconds, so a higher rate used to put more samples under the
    // same pulse and drive them harder: 4.6 dB louder at 96 kHz than at 48,
    // and 9 dB at 192.  A session's sample rate is not a tone control.
    {
        auto levelAt = [](double rate) {
            EpMk2Processor p;
            p.setPlayConfigDetails(0, 2, rate, 512);
            p.prepareToPlay(rate, 512);
            if (auto* q = p.getState().getParameter("key_var"))
                q->setValueNotifyingHost(0.0f);
            if (auto* q = p.getState().getParameter("strike_var"))
                q->setValueNotifyingHost(0.0f);

            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, 45, (juce::uint8)110), 0);
            juce::AudioBuffer<float> b(2, 512);
            float peak = 0.0f;
            for (int i = 0; i < int(rate * 0.5) / 512; ++i) {
                b.clear();
                p.processBlock(b, m);
                m.clear();
                peak = std::max(peak, b.getMagnitude(0, b.getNumSamples()));
            }
            return peak;
        };

        const float at44 = levelAt(44100.0), at48 = levelAt(48000.0);
        const float at96 = levelAt(96000.0), at192 = levelAt(192000.0);
        const float lo = std::min(std::min(at44, at48), std::min(at96, at192));
        const float hi = std::max(std::max(at44, at48), std::max(at96, at192));
        const float spread = 20.0f * std::log10(hi / std::max(1.0e-9f, lo));
        char sd[112];
        snprintf(sd, sizeof sd, "  (%.4f/%.4f/%.4f/%.4f, %.2f dB apart)",
                 at44, at48, at96, at192, spread);
        check(spread < 0.5f, "level does not depend on the sample rate", sd);
    }

    // ---- stereo tremolo ---------------------------------------------------
    // The voices are mono -- one tine, one pickup -- so the channels separate
    // only at the tremolo.  Which means this is the first thing in the
    // instrument that can produce a stereo image at all, and worth checking it
    // really does rather than assuming.
    {
        auto renderPair = [&](bool tremOn, bool stereo,
                              std::vector<float>& l, std::vector<float>& r) {
            auto set = [&](const char* id, float v) {
                if (auto* q = proc.getState().getParameter(id))
                    q->setValueNotifyingHost(q->convertTo0to1(v));
            };
            set("trem_on", tremOn ? 1.0f : 0.0f);
            set("trem_stereo", stereo ? 1.0f : 0.0f);
            set("trem_rate", 6.0f);
            set("trem_depth", -3.0f);
            proc.prepareToPlay(sr, block);

            juce::MidiBuffer panic;
            panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            renderPeak(proc, panic, 1, block);

            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, 45, (juce::uint8)110), 0);
            juce::AudioBuffer<float> b(2, block);
            l.clear(); r.clear();
            for (int i = 0; i < blocksPerSecond; ++i) {
                b.clear();
                proc.processBlock(b, m);
                m.clear();
                for (int n = 0; n < block; ++n) {
                    l.push_back(b.getSample(0, n));
                    r.push_back(b.getSample(1, n));
                }
            }
            renderPeak(proc, panic, 1, block);
        };

        auto channelDifference = [](const std::vector<float>& l,
                                    const std::vector<float>& r) {
            double d = 0.0, e = 0.0;
            for (size_t i = 0; i < l.size(); ++i) {
                const double x = double(l[i]) - double(r[i]);
                d += x * x;
                e += double(l[i]) * double(l[i]);
            }
            return e > 0.0 ? std::sqrt(d / e) : 0.0;
        };

        std::vector<float> l, r;

        renderPair(false, true, l, r);
        check(l == r, "with the tremolo off, both channels are identical");

        renderPair(true, false, l, r);
        check(l == r, "mono tremolo moves both channels together");

        renderPair(true, true, l, r);
        const double spread = channelDifference(l, r);
        char sd[80];
        snprintf(sd, sizeof sd, "  (%.1f%% apart)", spread * 100.0);
        check(spread > 0.05, "stereo tremolo swings the channels apart", sd);

        // Antiphase, not just different: the two should move oppositely, so
        // their sum stays far steadier than either channel alone.
        std::vector<float> sum(l.size()), one(l.size());
        for (size_t i = 0; i < l.size(); ++i) {
            sum[i] = 0.5f * (l[i] + r[i]);
            one[i] = l[i];
        }
        auto swing = [&](const std::vector<float>& v) {
            // peak level per 50 ms block, max against min
            double lo = 1e9, hi = 0.0;
            const size_t w = size_t(sr * 0.05);
            for (size_t i = 0; i + w < v.size(); i += w) {
                double pk = 0.0;
                for (size_t k = i; k < i + w; ++k) pk = std::max(pk, (double)std::fabs(v[k]));
                if (pk > 0) { lo = std::min(lo, pk); hi = std::max(hi, pk); }
            }
            return hi > 0 ? 20.0 * std::log10(hi / lo) : 0.0;
        };
        const double swingOne = swing(one), swingSum = swing(sum);
        char an[96];
        snprintf(an, sizeof an, "  (one channel %.1f dB, the sum %.1f dB)",
                 swingOne, swingSum);
        check(swingSum < swingOne, "the channels move in antiphase", an);

        if (auto* q = proc.getState().getParameter("trem_on"))
            q->setValueNotifyingHost(0.0f);
        proc.prepareToPlay(sr, block);
    }

    // ---- polyphony reaches 128 -------------------------------------------
    // kMaxVoices was always 128; only the parameter's range capped it at 64.
    // Cost is linear in *sounding* voices -- idle ones are skipped outright
    // and retire at -80 dBFS -- so the only question is whether they all
    // actually sound.
    {
        if (auto* q = proc.getState().getParameter("polyphony"))
            q->setValueNotifyingHost(q->convertTo0to1(128.0f));
        proc.prepareToPlay(sr, block);

        juce::MidiBuffer panic;
        panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
        renderPeak(proc, panic, 1, block);

        // A full 88-key keyboard, held: the realistic worst case.
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
        for (int n = 21; n < 109; ++n)
            midi.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8)100), 1);
        renderPeak(proc, midi, blocksPerSecond / 4, block);

        const int sounding = proc.getActiveVoiceCount();
        char pd[80];
        snprintf(pd, sizeof pd, "  (%d voices)", sounding);
        check(sounding >= 80, "a full keyboard sounds at once", pd);

        juce::MidiBuffer up;
        up.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
        renderPeak(proc, up, 1, block);
        renderPeak(proc, panic, 1, block);
        if (auto* q = proc.getState().getParameter("polyphony"))
            q->setValueNotifyingHost(q->convertTo0to1(32.0f));
        proc.prepareToPlay(sr, block);
    }

    // ---- sympathetic resonance -------------------------------------------
    // Tines share a frame, so an undamped one answers whatever else is
    // sounding.  This is a feedback loop around a bank of resonators whose
    // gain at their own frequency is their Q -- into the thousands here -- so
    // stability is the thing to check, not the effect.
    {
        auto set = [&](const char* id, float v) {
            if (auto* q = proc.getState().getParameter(id))
                q->setValueNotifyingHost(q->convertTo0to1(v));
        };
        auto silence = [&] {
            juce::MidiBuffer panic;
            panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            renderPeak(proc, panic, 1, block);
        };

        // The classic demonstration: hold a chord silently, strike a low note,
        // then *damp the struck note* and listen to what is left.  Measuring
        // while it still rings measures the struck note -- it masks the very
        // thing being looked for.
        auto heldChordLevel = [&](float amount) {
            set("sympathetic", amount);
            set("key_var", 0.0f);
            set("strike_var", 0.0f);
            proc.prepareToPlay(sr, block);
            silence();

            juce::MidiBuffer hold;
            for (int n : { 64, 68, 71 })
                hold.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8)1), 0);
            renderPeak(proc, hold, blocksPerSecond / 2, block);

            juce::MidiBuffer strike;
            strike.addEvent(juce::MidiMessage::noteOn(1, 40, (juce::uint8)120), 0);
            renderPeak(proc, strike, blocksPerSecond / 2, block);

            juce::MidiBuffer damp;
            damp.addEvent(juce::MidiMessage::noteOff(1, 40), 0);
            juce::AudioBuffer<float> b(2, block);
            std::vector<float> out;
            for (int i = 0; i < blocksPerSecond * 2; ++i) {
                b.clear();
                proc.processBlock(b, damp);
                damp.clear();
                for (int k = 0; k < block; ++k) out.push_back(b.getSample(0, k));
            }
            silence();

            double total = 0.0;
            for (int n : { 64, 68, 71 }) {
                const double f = 440.0 * std::pow(2.0, (n - 69) / 12.0);
                const double w = 2.0 * M_PI * f / sr, c = 2.0 * std::cos(w);
                double s0 = 0, s1 = 0, s2 = 0;
                for (float v : out) { s0 = v + c * s1 - s2; s2 = s1; s1 = s0; }
                total += std::sqrt(std::max(0.0, s1*s1 + s2*s2 - c*s1*s2)) / out.size();
            }
            return total;
        };

        const double quiet = heldChordLevel(0.0f);
        const double rung  = heldChordLevel(1.0f);
        char sy[96];
        snprintf(sy, sizeof sy, "  (%.1f dB more at the held pitches)",
                 20.0 * std::log10(rung / std::max(1e-15, quiet)));
        check(rung > quiet * 1.6, "a silently held chord answers a struck note", sy);

        // Stability with only a *few* notes held, which is the worst case and
        // the one that matters.  Each voice is driven by the average of the
        // others, so with two coupled voices the divisor is one and each hears
        // the other at full scale; with sixty-eight it is divided by
        // sixty-seven.  The loop is tightest when the fewest notes are down.
        //
        // Checking sixty-eight alone -- which is what this did -- passes at a
        // setting where two held notes grow to 187% of their own peak and a
        // held note never decays at all.
        {
            set("key_var", 0.0f);
            set("strike_var", 0.0f);
            for (int voices : { 2, 3, 4 }) {
                double coupledTail = 0.0, bareTail = 0.0;
                for (int on = 0; on < 2; ++on) {
                    set("sympathetic", on ? 1.0f : 0.0f);
                    proc.prepareToPlay(sr, block);
                    silence();

                    juce::MidiBuffer midi;
                    for (int i = 0; i < voices; ++i)
                        midi.addEvent(juce::MidiMessage::noteOn(
                            1, 60 + i * 3, (juce::uint8)100), 0);
                    const float early = renderPeak(proc, midi, blocksPerSecond / 3, block);
                    juce::MidiBuffer none;
                    renderPeak(proc, none, blocksPerSecond * 2, block);
                    const float late = renderPeak(proc, none, blocksPerSecond / 2, block);
                    (on ? coupledTail : bareTail) = late / std::max(1.0e-9f, early);
                    silence();
                }
                char st[112];
                snprintf(st, sizeof st,
                         "  (%d notes: %.0f%% left coupled, %.0f%% uncoupled)",
                         voices, coupledTail * 100.0, bareTail * 100.0);
                check(coupledTail < bareTail * 1.6,
                      "a few held notes still decay with coupling", st);
            }
        }

        set("sympathetic", 0.35f);
        set("key_var", 0.35f);
        set("strike_var", 0.30f);
        set("polyphony", 32.0f);
        proc.prepareToPlay(sr, block);
    }

    // A NaN reaching the pickup's lookup table used to index far outside it --
    // undefined, and in practice a segfault in the host rather than a wrong
    // sample.  It cannot arrive through the audio path, but it must not be
    // able to take the session down if it ever does.
    {
        epmk2::PickupShaper sh;
        sh.setGeometry(0.8f, 0.8f);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float inf = std::numeric_limits<float>::infinity();
        const bool ok = std::isfinite(sh.process(nan))
                     && std::isfinite(sh.process(inf))
                     && std::isfinite(sh.process(-inf))
                     && std::isfinite(sh.process(1.0e30f));
        check(ok, "the pickup table survives a NaN without leaving its bounds");
    }

    // ---- dynamic range ----------------------------------------------------
    // Widening the range must open up the space below full velocity without
    // moving full velocity itself -- otherwise every other measurement that
    // was matched to the reference at 0x7F would drift with this control.
    {
        auto peakAt = [&](int velocity, float range) {
            if (auto* q = proc.getState().getParameter("vel_range"))
                q->setValueNotifyingHost(q->convertTo0to1(range));
            proc.prepareToPlay(sr, block);
            juce::MidiBuffer panic;
            panic.addEvent(juce::MidiMessage::allSoundOff(1), 0);
            renderPeak(proc, panic, 1, block);
            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::noteOn(1, 45, (juce::uint8) velocity), 0);
            const float pk = renderPeak(proc, m, blocksPerSecond / 2, block);
            renderPeak(proc, panic, 1, block);
            return pk;
        };

        const float loudNarrow = peakAt(127, 5.0f), loudWide = peakAt(127, 8.0f);
        const float softNarrow = peakAt(30, 5.0f),  softWide = peakAt(30, 8.0f);

        char d[112];
        snprintf(d, sizeof d, "  (loud %.4f vs %.4f, soft %.4f vs %.4f)",
                 loudNarrow, loudWide, softNarrow, softWide);
        check(std::fabs(loudWide - loudNarrow) < loudNarrow * 0.02f
              && softWide < softNarrow * 0.7f,
              "dynamic range opens up below without moving full velocity", d);

        if (auto* q = proc.getState().getParameter("vel_range"))
            q->setValueNotifyingHost(q->convertTo0to1(5.0f));
        proc.prepareToPlay(sr, block);
    }

    // ---- does every control do anything? ---------------------------------
    // A knob that moves and changes nothing is worse than a missing one: it
    // invites a listener to imagine a difference.  This renders the same
    // performance at each parameter's two extremes and asks whether the audio
    // came out different at all -- not whether it changed by the right amount,
    // just whether the control is connected to anything.
    {
        auto renderWith = [](const char* id, float value, std::vector<float>& out) {
            const double sr = 48000.0;
            const int block = 512;
            const int held = (int) (0.35 * sr), total = held + (int) (0.25 * sr);

            EpMk2Processor proc;
            proc.setPlayConfigDetails(0, 2, sr, block);
            proc.prepareToPlay(sr, block);
            // The performance has to reach every control.  Tremolo and
            // vibrato are both switched off by a depth of zero, and a bend
            // range does nothing until the wheel moves -- so all three are
            // set up first, and then the parameter under test overwrites its
            // own baseline.  Without this the audit reported Bend Range and
            // Vibrato Rate as dead controls, which is what it would report
            // for a genuinely dead one.
            auto set = [&proc](const char* which, float v) {
                if (auto* p = proc.getState().getParameter(which))
                    p->setValueNotifyingHost(p->convertTo0to1(v));
            };
            set("trem_on", 1.0f);
            set("vib_depth", 30.0f);
            set("pitch_bend", 0.5f);
            if (auto* p = proc.getState().getParameter(id))
                p->setValueNotifyingHost(p->convertTo0to1(value));

            // A chord rather than a note: polyphony, sympathetic coupling and
            // the stereo tremolo all need more than one voice to show.  Low
            // enough that none of the tine modes are past Nyquist.
            juce::MidiBuffer midi;
            for (int n : { 40, 47, 52, 56, 59, 64 })
                midi.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8) 100), 0);

            // Both channels, laid end to end.  Stereo is the reason: it
            // swings the two in antiphase against a mono tremolo that moves
            // them together, and the left channel alone comes out the same
            // either way -- so reading one channel called it a dead control.
            out.assign((size_t) total * 2, 0.0f);
            juce::AudioBuffer<float> buf(2, block);
            for (int pos = 0; pos < total; pos += block) {
                const int n = juce::jmin(block, total - pos);
                buf.setSize(2, n, false, false, true);
                buf.clear();
                // Released partway, so note-off, the damper and the pedal are
                // all exercised.
                if (pos <= held && pos + n > held)
                    for (int k : { 40, 47, 52, 56, 59, 64 })
                        midi.addEvent(juce::MidiMessage::noteOff(1, k),
                                      juce::jlimit(0, n - 1, held - pos));
                proc.processBlock(buf, midi);
                midi.clear();
                for (int i = 0; i < n; ++i) {
                    out[(size_t) (pos + i)] = buf.getSample(0, i);
                    out[(size_t) (total + pos + i)] = buf.getSample(1, i);
                }
            }
        };

        int dead = 0;
        juce::String deadNames;
        std::vector<std::pair<double, juce::String>> effect;
        for (const auto& sp : epmk2::params::table()) {
            std::vector<float> lo, hi;
            renderWith(sp.id, sp.min, lo);
            renderWith(sp.id, sp.max, hi);

            double diff = 0.0, level = 0.0;
            for (size_t i = 0; i < lo.size(); ++i) {
                diff = juce::jmax(diff, (double) std::abs(lo[i] - hi[i]));
                level = juce::jmax(level, (double) std::abs(lo[i]));
            }
            // A thousandth of the signal: below that it is not a control, it
            // is a rounding difference.
            if (! (diff > level * 1.0e-3)) {
                ++dead;
                deadNames += (deadNames.isEmpty() ? "" : ", ") + juce::String(sp.name);
            }
            effect.push_back({ 20.0 * std::log10(juce::jmax(1e-12, diff / juce::jmax(1e-12, level))),
                               juce::String(sp.name) });
        }
        // The weakest few, always: a control can be connected and still be
        // doing almost nothing, and that is worth seeing before it is
        // reported as broken by someone playing it.
        std::sort(effect.begin(), effect.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        juce::String weakest;
        for (size_t i = 0; i < 3 && i < effect.size(); ++i)
            weakest += (i ? ", " : "") + effect[i].second
                     + juce::String::formatted(" %.0f dB", effect[i].first);

        char cb[320];
        snprintf(cb, sizeof cb, "  (%d of %d do nothing%s%s; weakest %s)", dead,
                 (int) epmk2::params::table().size(),
                 dead ? ": " : "", dead ? deadNames.toRawUTF8() : "",
                 weakest.toRawUTF8());
        check(dead == 0, "every control changes the sound", cb);
    }

    // ---- what carries the sound -------------------------------------------
    // Tone Level and Tine Level are the *direct* paths -- the tone bar and the
    // tine mixed straight to the output, bypassing the pickup.  A real Rhodes
    // has no such thing; they are inherited from the Pd model and kept because
    // the keyboard's evenness measurably depends on them (roadmap 4.6).
    //
    // The consequence is not obvious from the names: turning both down does
    // not silence the instrument, because the pickup is still carrying the
    // same tone bar and tine.  This pins that down as a fact rather than a
    // surprise, and reports what each path is worth.
    {
        auto levelWith = [](std::initializer_list<std::pair<const char*, float>> set) {
            const double sr = 48000.0;
            const int block = 512, total = (int) (0.5 * sr);
            EpMk2Processor proc;
            proc.setPlayConfigDetails(0, 2, sr, block);
            proc.prepareToPlay(sr, block);
            for (auto& kv : set)
                if (auto* p = proc.getState().getParameter(kv.first))
                    p->setValueNotifyingHost(p->convertTo0to1(kv.second));

            juce::MidiBuffer midi;
            for (int n : { 40, 52, 59 })
                midi.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8) 100), 0);

            juce::AudioBuffer<float> buf(2, block);
            double peak = 0.0;
            for (int pos = 0; pos < total; pos += block) {
                buf.clear();
                proc.processBlock(buf, midi);
                midi.clear();
                peak = juce::jmax(peak, (double) buf.getMagnitude(0, buf.getNumSamples()));
            }
            return 20.0 * std::log10(juce::jmax(1e-9, peak));
        };

        const double all      = levelWith({});
        const double noTone   = levelWith({ { "tone_level", -100.0f } });
        const double noTine   = levelWith({ { "tine_level", -100.0f } });
        const double neither  = levelWith({ { "tone_level", -100.0f }, { "tine_level", -100.0f } });
        const double noPickup = levelWith({ { "pickup_level", -100.0f } });
        // Both of the tone bar's paths shut: the one thing Tone Direct alone
        // could never do, and the reason Tone to Pickup exists.
        const double noBar = levelWith({ { "tone_level", -100.0f },
                                         { "tone_send", -100.0f } });

        char rb[280];
        snprintf(rb, sizeof rb,
                 "  (full %.1f dB; without tone %+.1f, tine %+.1f, both %+.1f,"
                 " pickup %+.1f, tone bar entirely %+.1f)",
                 all, noTone - all, noTine - all, neither - all,
                 noPickup - all, noBar - all);
        // Both direct paths off must still sound, and the pickup off must
        // still sound: neither is the whole instrument on its own.  Closing
        // both of the tone bar's paths is different in kind -- 10 dB below
        // what Tone Direct alone can manage, and what is left is the tine,
        // not a quieter note.
        check(neither > all - 24.0 && noPickup > all - 24.0
                  && noTone < all - 0.5 && noTine < all - 0.1
                  && noBar < neither - 8.0,
              "the pickup and the direct paths each carry part of it", rb);
    }

    // ---- the mod wheel ----------------------------------------------------
    // CC1 is vibrato, and it drives the *parameter* rather than sitting beside
    // it -- so the panel shows what the wheel is asking for.  Same shape as
    // CC64, which had the opposite bug: the parameter overwrote the pedal
    // every block and a held pedal survived about 10 ms.
    {
        EpMk2Processor proc;
        proc.setPlayConfigDetails(0, 2, 48000.0, 256);
        proc.prepareToPlay(48000.0, 256);

        auto depth = [&proc] {
            auto* p = proc.getState().getParameter("vib_depth");
            return p != nullptr ? p->convertFrom0to1(p->getValue()) : -1.0f;
        };

        juce::AudioBuffer<float> buf(2, 256);
        auto send = [&](int value) {
            juce::MidiBuffer m;
            m.addEvent(juce::MidiMessage::controllerEvent(1, 1, value), 0);
            buf.clear();
            proc.processBlock(buf, m);
        };

        const float atRest = depth();
        send(127);
        const float wideOpen = depth();
        // Several blocks, because the failure this guards against is a value
        // that is written and then overwritten by the parameter table.
        for (int i = 0; i < 8; ++i) { juce::MidiBuffer none; buf.clear(); proc.processBlock(buf, none); }
        const float stillOpen = depth();
        send(0);
        const float shut = depth();

        char mb[128];
        snprintf(mb, sizeof mb, "  (rest %.0f, wheel up %.0f, 8 blocks later %.0f, down %.0f)",
                 atRest, wideOpen, stillOpen, shut);
        check(atRest == 0.0f && wideOpen > 90.0f && stillOpen == wideOpen && shut == 0.0f,
              "the mod wheel drives vibrato depth, and it stays", mb);
    }

    // ---- the pitch wheel, on the panel ------------------------------------
    // The knob follows an incoming wheel and can drive the bend by itself.
    // The failure worth guarding against is the second half: if the wheel's
    // last position were pushed into the parameter every block, the knob
    // would be overwritten the moment it was let go of and could never be
    // used by hand.
    {
        EpMk2Processor proc;
        proc.setPlayConfigDetails(0, 2, 48000.0, 256);
        proc.prepareToPlay(48000.0, 256);

        auto* bend = proc.getState().getParameter("pitch_bend");
        auto value = [&] { return bend->convertFrom0to1(bend->getValue()); };
        juce::AudioBuffer<float> buf(2, 256);
        auto run = [&](juce::MidiBuffer m) { buf.clear(); proc.processBlock(buf, m); };

        run({});
        const float atRest = value();

        juce::MidiBuffer up;
        up.addEvent(juce::MidiMessage::pitchWheel(1, 16383), 0);
        run(std::move(up));
        const float wheelUp = value();

        juce::MidiBuffer down;
        down.addEvent(juce::MidiMessage::pitchWheel(1, 0), 0);
        run(std::move(down));
        const float wheelDown = value();

        // Now by hand, with no wheel message anywhere near it.
        bend->setValueNotifyingHost(bend->convertTo0to1(0.5f));
        for (int i = 0; i < 8; ++i) run({});
        const float byHand = value();

        char pb[160];
        snprintf(pb, sizeof pb, "  (rest %.2f, wheel up %.2f, down %.2f, by hand %.2f)",
                 atRest, wheelUp, wheelDown, byHand);
        check(atRest == 0.0f && wheelUp > 0.99f && wheelDown < -0.99f
                  && std::abs(byHand - 0.5f) < 0.01f,
              "the bend knob follows the wheel, and survives without it", pb);
    }

    // ---- bend and vibrato -------------------------------------------------
    // Both move a note that is already sounding, which means re-deriving every
    // resonator on every voice while it rings.  The roadmap flagged two things
    // to check before believing it: that it does not click, and what it costs.
    {
        const double sr = 48000.0;
        const int block = 128;

        // Sweep the wheel across a held chord and look for a step.  A high-Q
        // resonator whose frequency is edited mid-ring is exactly the kind of
        // change that puts a discontinuity in the output, and at the control
        // rate this runs at, a bad one would show as a sample-to-sample jump
        // far larger than the waveform's own slope.
        auto sweep = [&](bool bend, double& worstJump, double& peak) {
            EpMk2Processor proc;
            proc.setPlayConfigDetails(0, 2, sr, block);
            proc.prepareToPlay(sr, block);
            if (auto* p = proc.getState().getParameter("vib_depth"))
                p->setValueNotifyingHost(p->convertTo0to1(bend ? 0.0f : 80.0f));

            juce::MidiBuffer midi;
            for (int n : { 40, 52, 59, 64 })
                midi.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8) 110), 0);

            const int total = (int) (2.0 * sr);
            juce::AudioBuffer<float> buf(2, block);
            float previous = 0.0f;
            worstJump = 0.0; peak = 0.0;
            for (int pos = 0; pos < total; pos += block) {
                if (bend) {
                    // A full sweep down and back over the two seconds.
                    const double t = (double) pos / total;
                    const int wheel = (int) (8192 + 8191 * std::sin(2.0 * M_PI * t));
                    midi.addEvent(juce::MidiMessage::pitchWheel(1, wheel), 0);
                }
                buf.clear();
                proc.processBlock(buf, midi);
                midi.clear();
                // Skip the attack, which is a step by design.
                if (pos < 0.2 * sr) {
                    previous = buf.getSample(0, buf.getNumSamples() - 1);
                    continue;
                }
                for (int i = 0; i < buf.getNumSamples(); ++i) {
                    const float x = buf.getSample(0, i);
                    worstJump = juce::jmax(worstJump, (double) std::abs(x - previous));
                    peak = juce::jmax(peak, (double) std::abs(x));
                    previous = x;
                }
            }
        };

        double bendJump = 0.0, bendPeak = 0.0, vibJump = 0.0, vibPeak = 0.0;
        sweep(true, bendJump, bendPeak);
        sweep(false, vibJump, vibPeak);

        // What a clean waveform's largest step is: at the top of the model's
        // range one sample of a 2 kHz sine at full scale moves about 0.26.
        // Anything near that is the signal; a retune click is not subtle.
        char jb[160];
        snprintf(jb, sizeof jb, "  (bend %.3f of %.2f peak, vibrato %.3f of %.2f)",
                 bendJump, bendPeak, vibJump, vibPeak);
        check(bendJump < bendPeak * 0.35 && vibJump < vibPeak * 0.35,
              "sweeping the pitch does not step the output", jb);

        // And the cost, since retuning is not free and vibrato leaves it
        // running for as long as a note sounds.
        auto cost = [&](float depth) {
            EpMk2Processor proc;
            proc.setPlayConfigDetails(0, 2, sr, block);
            proc.prepareToPlay(sr, block);
            if (auto* p = proc.getState().getParameter("vib_depth"))
                p->setValueNotifyingHost(p->convertTo0to1(depth));
            juce::MidiBuffer midi;
            for (int n = 36; n < 68; ++n)
                midi.addEvent(juce::MidiMessage::noteOn(1, n, (juce::uint8) 100), 0);
            juce::AudioBuffer<float> buf(2, block);
            const int total = (int) (2.0 * sr);
            const auto start = juce::Time::getHighResolutionTicks();
            for (int pos = 0; pos < total; pos += block) {
                buf.clear();
                proc.processBlock(buf, midi);
                midi.clear();
            }
            return juce::Time::highResolutionTicksToSeconds(
                       juce::Time::getHighResolutionTicks() - start) / 2.0 * 100.0;
        };
        const double idle = cost(0.0f), moving = cost(80.0f);
        char cb[160];
        snprintf(cb, sizeof cb, "  (32 voices: %.1f%% of a core, %.1f%% with vibrato,"
                                " +%.1f)", idle, moving, moving - idle);
        check(moving < 60.0, "retuning while it sounds is affordable", cb);
    }

    // ---- tunings ---------------------------------------------------------
    // The scale reaches the audio thread, survives a session, and is a
    // different instrument from the equal divisions it replaces.
    {
        // What the engine actually sounds, measured rather than asserted from
        // the table: render one note and find its fundamental.
        auto pitchOf = [](EpMk2Processor& p, int note) {
            const double sr = 48000.0;
            const int n = 1 << 15;
            // Clear whatever the last call left ringing.  A tone bar at
            // Q 1750 sounds for seconds, so without this the note measured
            // here competes with the note measured before it -- which is
            // exactly how a passing test can measure the wrong note.
            p.prepareToPlay(sr, 512);
            juce::AudioBuffer<float> out(2, n);
            out.clear();
            juce::AudioBuffer<float> buf(2, 512);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) 100), 0);
            for (int pos = 0; pos < n; pos += 512) {
                buf.clear();
                p.processBlock(buf, midi);
                midi.clear();
                out.copyFrom(0, pos, buf, 0, 0, 512);
            }
            // Goertzel over a range of candidate pitches, taking the strongest.
            const float* x = out.getReadPointer(0);
            double best = 0.0, bestPower = 0.0;
            for (double f = 20.0; f < 2000.0; f *= 1.0005) {
                const double w = 2.0 * M_PI * f / sr;
                const double c = 2.0 * std::cos(w);
                double s0 = 0.0, s1 = 0.0, s2 = 0.0;
                for (int i = n / 8; i < n; ++i) { s0 = x[i] + c * s1 - s2; s2 = s1; s1 = s0; }
                const double power = s1 * s1 + s2 * s2 - c * s1 * s2;
                if (power > bestPower) { bestPower = power; best = f; }
            }
            return best;
        };

        EpMk2Processor proc;
        proc.setPlayConfigDetails(0, 2, 48000.0, 512);
        proc.prepareToPlay(48000.0, 512);

        const double equal = pitchOf(proc, 73);      // C#5, four semitones up

        epmk2::Scale meantone;
        std::string err;
        const auto& built = epmk2::builtInScales();
        auto found = std::find_if(built.begin(), built.end(), [](const epmk2::BuiltInScale& b) {
            return juce::String(b.name) == "Quarter-comma Meantone"; });
        check(found != built.end() && epmk2::parseScl(found->scl, meantone, err),
              "a built-in scale parses", err.empty() ? "" : ("  (" + err + ")").c_str());
        meantone.name = "Quarter-comma Meantone";
        proc.setScale(meantone);

        // Quarter-comma meantone's C# is 76.049 cents above C, where 12-equal
        // puts it at 100.  Base note 69 is A440, so note 73 is four steps up:
        // meantone 386.314 cents against 400.
        const double tempered = pitchOf(proc, 73);
        const double moved = 1200.0 * std::log2(tempered / equal);
        char tb[96];
        snprintf(tb, sizeof tb, "  (moved %.1f cents, expected -13.7)", moved);
        check(std::abs(moved - (386.314 - 400.0)) < 2.0,
              "a scale retunes what the engine sounds", tb);

        // A period is not always an octave, and the engine has to follow it.
        epmk2::Scale bp;
        auto bpIt = std::find_if(built.begin(), built.end(), [](const epmk2::BuiltInScale& b) {
            return juce::String(b.name) == "Bohlen-Pierce"; });
        epmk2::parseScl(bpIt->scl, bp, err);
        bp.name = "Bohlen-Pierce";
        proc.setScale(bp);
        const double base = pitchOf(proc, 69), up13 = pitchOf(proc, 69 + 13);
        char pb[96];
        snprintf(pb, sizeof pb, "  (%.0f Hz -> %.0f Hz, ratio %.3f)", base, up13, up13 / base);
        check(std::abs(up13 / base - 3.0) < 0.02,
              "a scale with no octave repeats at its own period", pb);

        // The session has to carry the table.  A path would not survive the
        // project moving, and the file being edited would change the tuning.
        juce::MemoryBlock saved;
        proc.getStateInformation(saved);
        EpMk2Processor restored;
        restored.setPlayConfigDetails(0, 2, 48000.0, 512);
        restored.prepareToPlay(48000.0, 512);
        restored.setStateInformation(saved.getData(), (int) saved.getSize());
        char sb[128];
        snprintf(sb, sizeof sb, "  (%s, %d degrees)",
                 restored.getScale().name.c_str(), restored.getScale().degrees());
        check(restored.getScale().name == bp.name
                  && restored.getScale().cents == bp.cents,
              "the scale travels in the session", sb);
        // And it must be *live* in the restored instance, not merely stored.
        check(std::abs(pitchOf(restored, 69 + 13) / pitchOf(restored, 69) - 3.0) < 0.02,
              "a restored scale is published, not just remembered");

        // Back to no scale, and the equal divisions have to return.
        proc.setScale({});
        check(std::abs(pitchOf(proc, 73) - equal) < 0.5,
              "clearing the scale restores the equal divisions");
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

    // ---- the window remembers its size -----------------------------------
    // Across instances and across installs, which means the settings file is
    // the only source.  Storing it in the session state as well seemed
    // harmless and was not: session state took priority, so any state a host
    // applied to a freshly added instance -- a default preset, an empty tree,
    // anything -- overrode the size the user had actually chosen, and a new
    // instance opened at the factory default however many times it was set.
    {
        juce::ScopedJuceInitialiser_GUI juceInit;
        int w = 0, h = 0;
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
            auto* e = dynamic_cast<EpMk2Editor*>(ed.get());
            // Wait out the settle window, so this counts as the user resizing
            // rather than the host opening the window.
            juce::Thread::sleep(800);
            w = EpMk2Editor::kDesignWidth * 5 / 4;
            h = e != nullptr ? e->designHeightForTest() * 5 / 4 : ed->getHeight();
            ed->setSize(w, h);
            w = ed->getWidth();
            h = ed->getHeight();
        }

        // A brand new instance, sharing nothing with this one but the settings.
        {
            EpMk2Processor fresh;
            fresh.setPlayConfigDetails(0, 2, sr, block);
            fresh.prepareToPlay(sr, block);
            std::unique_ptr<juce::AudioProcessorEditor> ed(fresh.createEditor());
            char d[96];
            snprintf(d, sizeof d, "  (%d x %d -> %d x %d)",
                     w, h, ed->getWidth(), ed->getHeight());
            check(std::abs(ed->getWidth() - w) <= 2,
                  "a new instance opens at the last size used", d);
        }

        // A host handing the plugin a state must not undo that.  This is the
        // case that was broken: adding an instance to a project reset it.
        {
            EpMk2Processor fresh;
            fresh.setPlayConfigDetails(0, 2, sr, block);
            fresh.prepareToPlay(sr, block);
            juce::MemoryBlock blob;
            fresh.getStateInformation(blob);
            fresh.setStateInformation(blob.getData(), (int) blob.getSize());
            std::unique_ptr<juce::AudioProcessorEditor> ed(fresh.createEditor());
            char d[96];
            snprintf(d, sizeof d, "  (%d x %d -> %d x %d)",
                     w, h, ed->getWidth(), ed->getHeight());
            check(std::abs(ed->getWidth() - w) <= 2,
                  "restoring a session does not reset the window", d);
        }

        // A host sizing the editor must not be mistaken for the user doing
        // it.  Hosts set their own size immediately after constructing the
        // editor, and the first time one sees a new build it has nothing
        // remembered and uses the default -- which used to be written down as
        // the preference, permanently, so shipping a build wiped it.
        {
            EpMk2Processor fresh;
            fresh.setPlayConfigDetails(0, 2, sr, block);
            fresh.prepareToPlay(sr, block);
            {
                std::unique_ptr<juce::AudioProcessorEditor> ed(fresh.createEditor());
                auto* e = dynamic_cast<EpMk2Editor*>(ed.get());
                // Straight away, as a host does.
                ed->setSize(EpMk2Editor::kDesignWidth,
                            e != nullptr ? e->designHeightForTest() : ed->getHeight());
            }
            std::unique_ptr<juce::AudioProcessorEditor> ed(fresh.createEditor());
            char d[112];
            snprintf(d, sizeof d, "  (host asked for %d, reopened at %d, wanted %d)",
                     EpMk2Editor::kDesignWidth, ed->getWidth(), w);
            check(std::abs(ed->getWidth() - w) <= 2,
                  "a host opening the window does not overwrite the preference", d);
        }

        // A size stored by an older layout still restores: the design size
        // changes whenever the panel does, so the stored height will not match
        // the current aspect ratio.  Width is what carries over.
        {
            proc.saveEditorSize(w, h * 3 / 2);
            std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
            char d[96];
            snprintf(d, sizeof d, "  (asked %d x %d, got %d x %d)",
                     w, h * 3 / 2, ed->getWidth(), ed->getHeight());
            check(std::abs(ed->getWidth() - w) <= 2,
                  "a size stored by an older layout still restores", d);
        }

        // Leave the preference at the design size.
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
            if (auto* e = dynamic_cast<EpMk2Editor*>(ed.get()))
                e->setSize(EpMk2Editor::kDesignWidth, e->designHeightForTest());
        }
    }

    // ---- CC64 shows on the panel -----------------------------------------
    {
        auto* pedal = proc.getState().getParameter("sustain");
        pedal->setValueNotifyingHost(0.0f);

        juce::MidiBuffer down;
        down.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
        renderPeak(proc, down, 1, block);
        const bool litByCC = pedal->getValue() > 0.5f;

        juce::MidiBuffer up;
        up.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
        renderPeak(proc, up, 1, block);
        const bool darkAfter = pedal->getValue() < 0.5f;

        check(litByCC && darkAfter, "CC64 moves the panel's sustain control");
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
            // The width a control actually gets, read from the panel.
            int widestControl = 0;
            std::function<void(juce::Component*)> widths = [&](juce::Component* comp) {
                for (int k = 0; k < comp->getNumChildComponents(); ++k) {
                    auto* child = comp->getChildComponent(k);
                    if (dynamic_cast<ParamControl*>(child) != nullptr)
                        widestControl = juce::jmax(widestControl, child->getWidth());
                    widths(child);
                }
            };
            widths(ed.get());

            // No label may be squashed or shrunk to fit.  JUCE's fitted text
            // will compress glyphs to ~0.7 of their width and reduce the font
            // height rather than overflow, which quietly leaves some names
            // narrower than others -- and the panel should be one size of type
            // throughout.  So: every label must fit at full size, in at most
            // two lines, in the width it has.
            {
                // The panel's own font, not an approximation of it: the
                // width being checked here is the width that font produces.
                const juce::Font labelFont(panelLabelFont());
                auto widthOf = [&](const juce::String& t) {
                    return juce::GlyphArrangement::getStringWidthInt(labelFont, t);
                };
                // The whole name on one line.  The panel reserves a single
                // line for it, so a name that does not fit is clipped rather
                // than wrapped -- this has to measure what is actually drawn,
                // not the best two-line split it could fall back to.
                auto required = [&](const juce::String& name) {
                    return widthOf(name);
                };

                int worstOver = 0;
                juce::String worstName;
                for (const auto& sp : epmk2::params::table()) {
                    // The panel's name for it, which is what gets drawn and
                    // what the panel's width is derived from.
                    const int need = required(sp.panelName());
                    if (need > widestControl - 4) {
                        const int over = need - (widestControl - 4);
                        if (over > worstOver) { worstOver = over; worstName = sp.panelName(); }
                    }
                }
                char lb[128];
                snprintf(lb, sizeof lb, "  (%d px of room; worst is %s, over by %d)",
                         widestControl - 4, worstName.isEmpty() ? "none"
                                                                : worstName.toRawUTF8(),
                         worstOver);
                check(worstOver == 0, "every name fits on one line, unsquashed", lb);
            }

            // A control a loaded scale takes over must say so rather than
            // simply not responding.
            {
                PanelContent* panel = nullptr;
                std::function<void(juce::Component*)> findPanel = [&](juce::Component* c) {
                    for (int k = 0; k < c->getNumChildComponents(); ++k) {
                        if (auto* p = dynamic_cast<PanelContent*>(c->getChildComponent(k)))
                            panel = p;
                        findPanel(c->getChildComponent(k));
                    }
                };
                findPanel(ed.get());

                auto dimmedState = [&](const char* name) {
                    auto* cell = panel != nullptr ? panel->cellNamed(name) : nullptr;
                    return cell != nullptr && cell->isDimmed();
                };

                epmk2::Scale s12;
                std::string err;
                epmk2::parseScl(epmk2::builtInScales().front().scl, s12, err);
                s12.name = epmk2::builtInScales().front().name;

                proc.setScale({});
                if (panel != nullptr) panel->refreshDependents();
                const bool liveWhenEqual = ! dimmedState("Divisions")
                                        && ! dimmedState("Interval");

                proc.setScale(s12);
                if (panel != nullptr) panel->refreshDependents();
                const bool dimWhenScaled = dimmedState("Divisions")
                                        && dimmedState("Interval");
                // Base Frequency and Base MIDI Note still mean something under
                // a scale -- what it is measured from, and where its root sits.
                const bool baseStaysLive = ! dimmedState("Base Frequency")
                                        && ! dimmedState("Base MIDI Note");

                proc.setScale({});
                if (panel != nullptr) panel->refreshDependents();
                const bool liveAgain = ! dimmedState("Divisions");

                // The tremolo switch takes over its whole section, stereo
                // included: with no swing there is nothing for the two
                // channels to take opposite sides of.
                auto* tremOn = proc.getState().getParameter("trem_on");
                tremOn->setValueNotifyingHost(0.0f);
                if (panel != nullptr) panel->refreshDependents();
                const bool dimWhenOff = dimmedState("Stereo")
                                     && dimmedState("Tremolo Depth")
                                     && dimmedState("Tremolo Rate")
                                     && dimmedState("Tremolo Shape");
                tremOn->setValueNotifyingHost(1.0f);
                if (panel != nullptr) panel->refreshDependents();
                const bool liveWhenOn = ! dimmedState("Stereo")
                                     && ! dimmedState("Tremolo Depth")
                                     && ! dimmedState("Tremolo Rate")
                                     && ! dimmedState("Tremolo Shape");
                char tb2[96];
                snprintf(tb2, sizeof tb2, "  (off %s, on %s)",
                         dimWhenOff ? "dim" : "LIVE", liveWhenOn ? "live" : "DIM");
                check(dimWhenOff && liveWhenOn,
                      "the tremolo switch greys out its own section", tb2);

                char db[128];
                snprintf(db, sizeof db, "  (equal %s, scaled %s, base %s, back %s)",
                         liveWhenEqual ? "live" : "DIM", dimWhenScaled ? "dim" : "LIVE",
                         baseStaysLive ? "live" : "DIM", liveAgain ? "live" : "DIM");
                check(panel != nullptr && liveWhenEqual && dimWhenScaled
                          && baseStaysLive && liveAgain,
                      "a scale greys out the controls it takes over", db);
            }

            // A shortened name has to be recoverable.  The panel says
            // "Distance" because it is only as wide as its longest name; the
            // info bar has a whole strip and says "Pickup Distance", so the
            // abbreviation never has to be guessed at.
            {
                int wrongName = 0, shortened = 0;
                juce::String offender;
                std::function<void(juce::Component*)> names = [&](juce::Component* comp) {
                    for (int k = 0; k < comp->getNumChildComponents(); ++k) {
                        auto* child = comp->getChildComponent(k);
                        if (auto* pc = dynamic_cast<ParamControl*>(child)) {
                            const auto& all = epmk2::params::table();
                            auto sp = std::find_if(all.begin(), all.end(),
                                [&](const epmk2::params::Spec& s) {
                                    return pc->infoName() == s.name; });
                            if (sp == all.end()) {
                                ++wrongName;
                                offender = pc->infoName();
                            } else {
                                if (pc->drawnName() != juce::String(sp->panelName())) {
                                    ++wrongName;
                                    offender = pc->infoName();
                                }
                                if (pc->drawnName() != pc->infoName())
                                    ++shortened;
                            }
                        }
                        names(child);
                    }
                };
                names(ed.get());
                char nb[128];
                snprintf(nb, sizeof nb, "  (%d shortened, %d wrong%s%s)",
                         shortened, wrongName, wrongName ? ": " : "",
                         wrongName ? offender.toRawUTF8() : "");
                // The second half keeps this honest: if nothing were ever
                // shortened the first half would pass while proving nothing.
                check(wrongName == 0 && shortened > 0,
                      "the info bar gives a shortened name in full", nb);
            }

            // Rows of knobs must line up across the panel's three columns.
            // They are laid out per section, so nothing makes them agree by
            // construction: a section that starts at a different height, or a
            // row that reserves a different amount of space for its names,
            // puts its knobs a few pixels off its neighbours' -- close enough
            // to read as a mistake rather than as a decision.
            //
            // Near-misses are the whole point, so this does not bucket: any
            // two knobs within half a row of each other must be at *exactly*
            // the same height.
            {
                std::vector<std::pair<int, juce::String>> knobY;
                std::function<void(juce::Component*)> collect = [&](juce::Component* comp) {
                    for (int k = 0; k < comp->getNumChildComponents(); ++k) {
                        auto* child = comp->getChildComponent(k);
                        if (auto* pc = dynamic_cast<ParamControl*>(child))
                            for (int j = 0; j < pc->getNumChildComponents(); ++j)
                                if (dynamic_cast<juce::Slider*>(pc->getChildComponent(j)))
                                    knobY.push_back({ pc->getChildComponent(j)->getScreenPosition().y,
                                                      pc->getName() });
                        collect(child);
                    }
                };
                collect(ed.get());

                int worst = 0;
                juce::String worstPair;
                for (size_t a = 0; a < knobY.size(); ++a)
                    for (size_t b = a + 1; b < knobY.size(); ++b) {
                        const int d = std::abs(knobY[a].first - knobY[b].first);
                        if (d > 0 && d < 62 && d > worst) {
                            worst = d;
                            worstPair = knobY[a].second + " / " + knobY[b].second;
                        }
                    }
                char rb[160];
                snprintf(rb, sizeof rb, "  (%d knobs, worst near-miss %d px%s%s)",
                         (int) knobY.size(), worst,
                         worst ? ": " : "", worstPair.toRawUTF8());
                check(worst == 0 && ! knobY.empty(),
                      "knob rows line up across the columns", rb);
            }

            // A section's padding must be the same top and bottom.  Checking
            // the bottom against a floor instead let them drift apart: the
            // gap under the last row grew to three times the gap under the
            // header and the test had nothing to say about it.
            int worstAsymmetry = 0, tightest = 10000;
            juce::String tightestName, asymmetricName;
            std::function<void(juce::Component*)> margins = [&](juce::Component* comp) {
                for (int k = 0; k < comp->getNumChildComponents(); ++k) {
                    auto* child = comp->getChildComponent(k);
                    if (auto* sec = dynamic_cast<ParamSection*>(child)) {
                        const int top = sec->topMargin(), bottom = sec->bottomMargin();
                        if (std::abs(top - bottom) > worstAsymmetry) {
                            worstAsymmetry = std::abs(top - bottom);
                            asymmetricName = sec->getName();
                        }
                        if (bottom < tightest) { tightest = bottom; tightestName = sec->getName(); }
                    }
                    margins(child);
                }
            };
            margins(ed.get());
            // The info bar has to actually receive help text.  Hovering
            // cannot be simulated in a snapshot, so drive it directly and
            // check the bar changes what it draws.
            {
                auto barOf = [&](juce::Image& src) {
                    return src.getClippedImage(
                        juce::Rectangle<int>(0, src.getHeight() - 40,
                                             src.getWidth(), 38));
                };
                auto distinct = [](const juce::Image& im) {
                    std::set<juce::uint32> seen;
                    for (int y = 0; y < im.getHeight(); y += 2)
                        for (int x = 0; x < im.getWidth(); x += 2)
                            seen.insert(im.getPixelAt(x, y).getARGB());
                    return (int) seen.size();
                };
                juce::Image idle = ed->createComponentSnapshot(ed->getLocalBounds());
                auto idleBar = barOf(idle);

                // Reach into the panel and show a control's help.
                std::function<PanelContent*(juce::Component*)> findPanel =
                    [&](juce::Component* c) -> PanelContent* {
                        for (int k = 0; k < c->getNumChildComponents(); ++k) {
                            auto* ch = c->getChildComponent(k);
                            if (auto* pc = dynamic_cast<PanelContent*>(ch)) return pc;
                            if (auto* deep = findPanel(ch)) return deep;
                        }
                        return nullptr;
                    };
                bool ok = false;
                if (auto* panel = findPanel(ed.get())) {
                    panel->showHelp("Bass Tilt",
                                    "How much further a bass tine swings than a "
                                    "treble one for the same blow.");
                    juce::Image shown = ed->createComponentSnapshot(ed->getLocalBounds());
                    auto shownBar = barOf(shown);
                    // Count how much of the strip actually changed, rather
                    // than sampling one pixel that may be background in both.
                    int changed = 0;
                    for (int y = 0; y < shownBar.getHeight(); ++y)
                        for (int x = 0; x < shownBar.getWidth(); ++x)
                            if (shownBar.getPixelAt(x, y) != idleBar.getPixelAt(x, y))
                                ++changed;
                    ok = changed > 200;
                    panel->showHelp({}, {});
                }
                check(ok, "the info bar shows a control's help");
            }

            // Every control must carry help text, or the bar is useless for it.
            int missingHelp = 0;
            for (const auto& sp : epmk2::params::table())
                if (sp.help == nullptr || juce::String(sp.help).length() < 20)
                    ++missingHelp;
            char mh[64];
            snprintf(mh, sizeof mh, "  (%d without)", missingHelp);
            check(missingHelp == 0, "every control explains itself", mh);

            char bm[160];
            snprintf(bm, sizeof bm, "  (%d px of padding, tightest is %s;"
                                    " worst top-to-bottom gap %d px%s%s)",
                     tightest, tightestName.toRawUTF8(), worstAsymmetry,
                     worstAsymmetry ? " at " : "",
                     worstAsymmetry ? asymmetricName.toRawUTF8() : "");
            check(tightest >= 6 && worstAsymmetry == 0,
                  "sections are padded the same top and bottom", bm);

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
            // Distance from the top of the window down to the first section
            // header, which is a bright pastel bar.  Matching an exact colour
            // does not work: the title bar is a gradient, so "same as the top
            // pixel" stops a few rows in and reports 4 px whatever the scale.
            auto headerDepth = [](const juce::Image& im) {
                const int x = im.getWidth() / 2;
                for (int y = 0; y < im.getHeight(); ++y)
                    if (im.getPixelAt(x, y).getBrightness() > 0.45f)
                        return y;
                return im.getHeight();
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
