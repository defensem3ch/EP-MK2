// Play a MIDI file through the plugin offline: renders to a WAV, reports the
// realtime cost, the peak voice count, and any discontinuities.
//
// Synthetic note patterns only go so far -- how long voices live, and so what
// the instrument actually costs, depends entirely on how someone plays and how
// they use the pedal.  This runs the real thing.
//
//   ./epmk2-playmidi file.mid [--out out.wav] [--poly 32] [--block 512]
#include <cmath>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "../plugin/PluginProcessor.h"

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: %s <file.mid> [--out out.wav] [--poly N] [--block N]\n", argv[0]);
        return 1;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File midiFile(juce::File::getCurrentWorkingDirectory()
                                  .getChildFile(juce::String(argv[1])));
    const char* outPath = nullptr;
    int poly = 32, block = 512;
    for (int i = 2; i < argc - 1; ++i) {
        if      (!strcmp(argv[i], "--out"))   outPath = argv[++i];
        else if (!strcmp(argv[i], "--poly"))  poly = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--block")) block = atoi(argv[++i]);
    }

    juce::FileInputStream in(midiFile);
    if (!in.openedOk()) { printf("cannot open %s\n", midiFile.getFullPathName().toRawUTF8()); return 1; }

    juce::MidiFile mf;
    if (!mf.readFrom(in)) { printf("not a MIDI file\n"); return 1; }
    mf.convertTimestampTicksToSeconds();

    juce::MidiMessageSequence all;
    for (int t = 0; t < mf.getNumTracks(); ++t)
        all.addSequence(*mf.getTrack(t), 0.0);
    all.updateMatchedPairs();

    const double sr = 48000.0;
    EpMk2Processor proc;
    proc.setPlayConfigDetails(0, 2, sr, block);
    if (auto* p = proc.getState().getParameter("polyphony"))
        p->setValueNotifyingHost(p->convertTo0to1((float)poly));
    proc.prepareToPlay(sr, block);

    const double tail = 6.0;
    const double length = all.getEndTime() + tail;
    const int totalSamples = int(length * sr);

    juce::AudioBuffer<float> buf(2, block);
    juce::AudioBuffer<float> rendered(1, totalSamples);
    rendered.clear();

    struct Ev { double t; juce::String what; };
    std::vector<Ev> log;
    int event = 0, peakVoices = 0;
    double cpu = 0.0;
    int notes = 0, pedalEvents = 0;

    for (int pos = 0; pos < totalSamples; pos += block) {
        const int n = juce::jmin(block, totalSamples - pos);
        buf.setSize(2, n, false, false, true);
        buf.clear();

        juce::MidiBuffer midi;
        const double blockEnd = double(pos + n) / sr;
        while (event < all.getNumEvents()
               && all.getEventTime(event) < blockEnd) {
            const auto& m = all.getEventPointer(event)->message;
            const int offset = juce::jlimit(0, n - 1,
                int((all.getEventTime(event) - double(pos) / sr) * sr));
            midi.addEvent(m, offset);
            if (m.isNoteOn()) {
                ++notes;
                log.push_back({ all.getEventTime(event),
                                "note on  " + juce::MidiMessage::getMidiNoteName(m.getNoteNumber(), true, true, 4)
                                    + " vel " + juce::String(m.getVelocity()) });
            }
            if (m.isNoteOff())
                log.push_back({ all.getEventTime(event),
                                "note off " + juce::MidiMessage::getMidiNoteName(m.getNoteNumber(), true, true, 4) });
            if (m.isController() && m.getControllerNumber() == 64) ++pedalEvents;
            ++event;
        }

        const auto t0 = std::chrono::steady_clock::now();
        proc.processBlock(buf, midi);
        cpu += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

        peakVoices = juce::jmax(peakVoices, proc.getActiveVoiceCount());
        rendered.copyFrom(0, pos, buf, 0, 0, n);
    }

    // Note onsets are supposed to be abrupt -- the hammer contact is under a
    // millisecond, so the attack moves fast enough to trip any slope detector.
    // What matters is a discontinuity that is *not* explained by a note
    // starting, which is what a retrigger click or a voice-steal step looks
    // like.  Count those separately.
    std::vector<double> onsets;
    for (const auto& e : log)
        if (e.what.startsWith("note on"))
            onsets.push_back(e.t);

    auto nearOnset = [&](double t) {
        for (double o : onsets)
            if (t >= o - 0.001 && t <= o + 0.030)
                return true;
        return false;
    };

    // A fixed slope threshold does not survive a change of timbre.  The
    // geometric pickup produces a genuinely spiky waveform -- flux goes as
    // 1/r^3 and peaks sharply as the tine swings close -- so a single clean
    // note trips a "d1 > 0.02" rule 8-25 times with nothing wrong.  What marks
    // a real step is a jump far outside the slopes the signal has been making
    // either side of it, so compare against a running mean of |derivative|.
    const float* d = rendered.getReadPointer(0);
    const int window = int(0.005 * sr);
    int clicks = 0, unexplained = 0;
    double worst = 0.0, worstAt = 0.0;
    double slopeSum = 0.0;
    for (int n = 1; n < juce::jmin(window, totalSamples); ++n)
        slopeSum += std::fabs(d[n] - d[n-1]);

    for (int n = 2; n < totalSamples; ++n) {
        if (n + window < totalSamples)
            slopeSum += std::fabs(d[n + window] - d[n + window - 1])
                      - std::fabs(d[n] - d[n-1]);
        const double meanSlope = slopeSum / window;

        const double d1 = std::fabs(d[n] - d[n-1]), d2 = std::fabs(d[n-1] - d[n-2]);
        if (d1 > 0.02 && d1 > 8.0 * d2 + 1e-4 && d1 > 12.0 * meanSlope) {
            ++clicks;
            const double t = n / sr;
            if (!nearOnset(t)) {
                ++unexplained;
                if (d1 > worst) { worst = d1; worstAt = t; }
            }
        }
    }

    printf("%s\n", midiFile.getFileName().toRawUTF8());
    printf("  %.1f s, %d note-ons, %d pedal events\n", length, notes, pedalEvents);
    printf("  polyphony %d, block %d\n", poly, block);
    printf("  peak voices sounding: %d\n", peakVoices);
    printf("  cpu %.3f s for %.1f s audio -> %.1f%% of one core\n",
           cpu, length, 100.0 * cpu / length);
    printf("  peak level %.4f\n", rendered.getMagnitude(0, totalSamples));
    printf("  slope events: %d, of which within a note attack: %d\n",
           clicks, clicks - unexplained);
    printf("  unexplained discontinuities: %d%s\n", unexplained,
           unexplained ? juce::String(" (worst " + juce::String(worst, 4)
                                 + " at " + juce::String(worstAt, 2) + " s)").toRawUTF8() : "");

    // Show what was happening around each glitch.
    if (unexplained > 0) {
        printf("\n  glitches, with the MIDI around them:\n");
        int shown = 0;
        for (int n = 2; n < totalSamples && shown < 6; ++n) {
            const double d1 = std::fabs(d[n] - d[n-1]), d2 = std::fabs(d[n-1] - d[n-2]);
            if (!(d1 > 0.02 && d1 > 8.0 * d2 + 1e-4))
                continue;
            const double t = n / sr;
            if (nearOnset(t))
                continue;
            printf("    %8.3f s  jump %.4f  (level %.4f -> %.4f)\n",
                   t, d1, d[n-1], d[n]);
            for (const auto& e : log)
                if (std::fabs(e.t - t) < 0.25)
                    printf("               %+7.3f s  %s\n", e.t - t, e.what.toRawUTF8());
            ++shown;
            n += int(0.01 * sr);
        }
    }

    if (outPath != nullptr) {
        juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(outPath));
        out.deleteFile();
        juce::WavAudioFormat wav;
        if (auto* fs = out.createOutputStream().release()) {
            std::unique_ptr<juce::AudioFormatWriter> w(
                wav.createWriterFor(fs, sr, 1, 24, {}, 0));
            if (w != nullptr) w->writeFromAudioSampleBuffer(rendered, 0, totalSamples);
        }
    }
    return 0;
}
