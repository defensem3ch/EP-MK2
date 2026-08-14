// Render the C++ voice to a WAV, so it can be listened to and compared with
// the hvcc reference using the same analysis tools.
//
//   g++ -O2 -std=c++14 -o render_voice tests/render_voice.cpp -lm
//   ./render_voice --note 45 --seconds 3 --out /tmp/cpp.wav
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <vector>
#include <ctime>

#include "../dsp/Engine.h"

static void writeWav(const char* path, const std::vector<float>& mono, int rate)
{
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    const uint32_t dataBytes = (uint32_t)(mono.size() * sizeof(float));
    const uint32_t byteRate = rate * sizeof(float);
    const uint16_t blockAlign = sizeof(float), fmtTag = 3, bits = 32, channels = 1;
    const uint32_t fmtSize = 16, riffSize = 36 + dataBytes, sr = (uint32_t)rate;
    fwrite("RIFF", 1, 4, f); fwrite(&riffSize, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); fwrite(&fmtSize, 4, 1, f);
    fwrite(&fmtTag, 2, 1, f); fwrite(&channels, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&byteRate, 4, 1, f); fwrite(&blockAlign, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    fwrite(mono.data(), 1, dataBytes, f);
    fclose(f);
}

int main(int argc, char** argv)
{
    int note = 45, velocity = 100, rate = 48000, seq = 0, poly = 32;
    double seconds = 3.0, holdSeconds = 1.0e9, seqHold = 0.4, seqGap = 0.1;
    const char* out = nullptr;

    for (int i = 1; i < argc - 1; i++) {
        if      (!strcmp(argv[i], "--note"))    note = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--vel"))     velocity = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seconds")) seconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--hold"))    holdSeconds = atof(argv[++i]);
        else if (!strcmp(argv[i], "--rate"))    rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out"))     out = argv[++i];
        else if (!strcmp(argv[i], "--seq"))     seq = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--poly"))    poly = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--seqhold")) seqHold = atof(argv[++i]);
        else if (!strcmp(argv[i], "--seqgap"))  seqGap = atof(argv[++i]);
    }

    epmk2::EngineParams ep;
    epmk2::VoiceParams& p = ep.voice;

    // --set <field>=<dB> for the level controls, so the port can be bisected
    // against the reference one contribution at a time.
    auto dB = [](double v) { return (float)std::pow(10.0, v / 20.0); };
    for (int i = 1; i < argc - 1; i++) {
        if (strcmp(argv[i], "--set")) continue;
        char* spec = argv[++i];
        char* eq = strchr(spec, '=');
        if (!eq) continue;
        *eq = '\0';
        const double val = atof(eq + 1);
        if      (!strcmp(spec, "tine_level"))   p.tineLevelLin   = dB(val);
        else if (!strcmp(spec, "tone_level"))   p.toneLevelLin   = dB(val);
        else if (!strcmp(spec, "pickup_level")) p.pickupLevelLin = dB(val);
        else if (!strcmp(spec, "hammer_level")) p.hammerLevelLin = dB(val);
        else if (!strcmp(spec, "buzz_level"))   p.buzzLevelLin   = dB(val);
        else if (!strcmp(spec, "tine_send"))    p.tineSendLin    = dB(val);
        else if (!strcmp(spec, "pickup_gain"))  p.pickupGainLin  = dB(val);
        else if (!strcmp(spec, "pickup_symmetry")) p.pickupSymmetryLin = dB(val);
        else if (!strcmp(spec, "pickup_attack"))   p.pickupAttackLin   = dB(val);
        else { fprintf(stderr, "unknown --set %s\n", spec); return 1; }
        printf("  set %s = %g dB\n", spec, val);
    }

    epmk2::Engine eng;
    eng.prepare(rate, poly);
    const float freq = eng.noteToFrequency((float)note, ep);
    if (!seq) eng.noteOn(note, velocity, ep);

    const int total = int(seconds * rate);
    const int releaseAt = int(holdSeconds * rate);
    std::vector<float> buf;
    buf.reserve(total);

    float peak = 0.0f;
    const int holdSamples = int(seqHold * rate), gapSamples = int(seqGap * rate);
    int nextEvent = 0, played = 0, heldNote = -1;

    const clock_t t0 = clock();
    for (int n = 0; n < total; ++n) {
        if (!seq && n == releaseAt) eng.noteOff(note, ep);
        if (seq && n == nextEvent) {
            if (heldNote >= 0) {
                eng.noteOff(heldNote, ep);
                heldNote = -1;
                nextEvent = n + gapSamples;
            } else if (played < seq) {
                heldNote = note + (played % 24);
                eng.noteOn(heldNote, velocity, ep);
                ++played;
                nextEvent = n + holdSamples;
            }
        }
        const float s = eng.process(ep);
        buf.push_back(s);
        if (std::fabs(s) > peak) peak = std::fabs(s);
    }
    const double cpu = double(clock() - t0) / CLOCKS_PER_SEC;
    printf("cpu %.3fs for %.2fs audio -> %.1f%% of one core (poly %d)\n",
           cpu, seconds, 100.0 * cpu / seconds, poly);

    // Look for discontinuities: a sample-to-sample jump far larger than the
    // local slope is what a click sounds like.
    {
        int clicks = 0; double worst = 0.0; int worstAt = 0;
        for (size_t n = 2; n < buf.size(); ++n) {
            const double d1 = std::fabs(buf[n] - buf[n-1]);
            const double d2 = std::fabs(buf[n-1] - buf[n-2]);
            if (d1 > 0.02 && d1 > 8.0 * d2 + 1e-4) {
                ++clicks;
                if (d1 > worst) { worst = d1; worstAt = (int)n; }
            }
        }
        printf("discontinuities: %d (worst %.4f at %.3fs)\n",
               clicks, worst, worstAt / double(rate));
    }

    printf("note %d (%.2f Hz) vel %d | %.2fs | peak %.4f | %s at end\n",
           note, freq, velocity, seconds, peak,
           eng.activeVoices() ? "still active" : "finished");

    if (out) writeWav(out, buf, rate);
    return 0;
}
