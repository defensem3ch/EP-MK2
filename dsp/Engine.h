// The whole instrument: voice allocation plus the top-level chain the Pd
// patch applies after the voice bank.
//
//   voices -> *0.5 -> tremolo -> master -> hip~ 5 -> tanh -> out
//
// Framework-free, like the rest of dsp/.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Scale.h"
#include "Voice.h"

namespace epmk2 {

// dsp/ stays free of framework headers, so this is spelled out rather than
// reaching for juce::jmax.
inline int juce_max(int a, int b) noexcept { return a > b ? a : b; }

struct EngineParams {
    VoiceParams voice;

    // tuning: "N divisions of interval I", as in the Pd patch
    float baseFreq = 440.0f;
    float baseNote = 69.0f;
    float divisions = 12.0f;
    float interval = 2.0f;

    // How far the wheel bends at its extreme, and where the wheel is.  Kept
    // apart because the range is a parameter the host owns and the wheel is a
    // live MIDI value -- combining them in one field meant whichever was
    // written last won, which is the bug CC64 had.
    float bendRangeSemis = 2.0f;
    float bendWheel = 0.0f;          // -1 .. 1

    // Pitch bend, in semitones: the wheel scaled by its range.  Vibrato is in
    // cents, which is the unit that stays meaningful across the keyboard.
    float vibratoDepthCents = 0.0f;
    float vibratoRateHz = 5.0f;

    // An unequal scale, when one is loaded, which overrides the two above.
    // A bare pointer and a count rather than a container: this is read on the
    // audio thread, and the processor publishes a fixed array it will not
    // reallocate.  Null means the equal divisions still apply.
    const double* scaleCents = nullptr;
    int scaleDegrees = 0;

    // tremolo
    bool  tremoloOn = false;
    float tremoloRateHz = 3.0f;
    float tremoloShape = 0.0f;       // 0..127, sine -> triangle
    float tremoloDepthLin = 0.355f;  // -9 dB
    // Stereo swings the channels in antiphase, which is what a suitcase does
    // -- it pans between two amplifiers rather than simply ducking.  Mono
    // moves both together.
    bool  tremoloStereo = false;

    float masterLin = 1.0f;
    int   polyphony = 32;
};

class Engine {
public:
    static constexpr int kMaxVoices = 128;

    // Set by the *two note* case, which is the worst one and was very nearly
    // missed: the drive each voice receives is the average of the others, so
    // with two coupled voices the divisor is one and each hears the other at
    // full scale.  With sixty-eight it is divided by sixty-seven.  The loop is
    // therefore tightest when the fewest notes are held -- the opposite of
    // what the first stability check assumed, which is why it passed at a
    // setting where two held notes grew to 187% of their own peak.
    static constexpr float kCouplingScale = 0.05f;

    void setVoiceCount(int n) noexcept
    {
        const int wanted = std::max(1, std::min(n, kMaxVoices));
        if (wanted == voiceCount)
            return;
        // Silence anything above the new limit so it cannot be left ringing
        // with no way to reach it.
        for (int i = wanted; i < voiceCount; ++i)
            voices[i].reset();
        voiceCount = wanted;
    }

    int getVoiceCount() const noexcept { return voiceCount; }

    // Only for calibrating the coupling against a measurement; the shipped
    // value is kCouplingScale.
    void setCouplingScaleForTest(float s) noexcept { couplingScale = s; }

    void prepare(double sr, int numVoices) noexcept
    {
        sampleRate = sr;
        voiceCount = std::max(1, std::min(numVoices, kMaxVoices));
        for (int i = 0; i < kMaxVoices; ++i)
            voices[i].prepare(sr);
        tremoloPhase = 0.0f;
        vibratoPhase = 0.0f;
        pitchCountdown = 0;
        pitchMoving = false;
        couplingBus = 0.0f;
        coupledLast = 0;
        voiceLast.fill(0.0f);
        rngState = 0x9E3779B9u;
        dcBlockX1 = dcBlockY1 = dcBlockX1R = dcBlockY1R = 0.0f;
        dcBlockCoeff = float(1.0 - 2.0 * kPi * 5.0 / sr);   // hip~ 5
    }

    // A loaded scale if there is one, otherwise the patch's own tuning:
    // interval^((note - baseNote) / divisions) * baseFreq.
    float noteToFrequency(float midiNote, const EngineParams& p) const noexcept
    {
        // A scale is a table of degrees, so it is indexed by whole steps from
        // the base note.  Rounding is what a keyboard gives it either way.
        if (p.scaleDegrees > 0)
            return float(p.baseFreq
                         * scaleRatio(p.scaleCents, p.scaleDegrees,
                                      (int) std::lround(midiNote - p.baseNote)));

        if (p.divisions == 0.0f)
            return p.baseFreq;
        return float(std::pow((double)p.interval,
                              (midiNote - p.baseNote) / (double)p.divisions)
                     * p.baseFreq);
    }

    void noteOn(int midiNote, int velocity, const EngineParams& p) noexcept
    {
        if (velocity <= 0) { noteOff(midiNote, p); return; }
        Voice& v = voices[allocate(midiNote)];
        // Same pitch, still sounding: the same tine struck again.
        const bool restrike = v.isActive() && (int)v.getNote() == midiNote;
        v.noteOn((float)midiNote, (float)velocity,
                 noteToFrequency((float)midiNote, p), p.voice, restrike,
                 drawStrike(p.voice.strikeVariation));
    }

    void noteOff(int midiNote, const EngineParams& p) noexcept
    {
        for (int i = 0; i < voiceCount; ++i)
            if (voices[i].isActive() && voices[i].isHeld()
                && (int)voices[i].getNote() == midiNote)
                voices[i].noteOff(p.voice);
    }

    void sustainPedal(bool down, const EngineParams& p) noexcept
    {
        if (!down)
            for (int i = 0; i < voiceCount; ++i)
                voices[i].pedalReleased(p.voice);
    }

    // MIDI CC123: behave as though every key were released, so notes ring out
    // with their normal release rather than stopping dead.
    void allNotesOff(const EngineParams& p) noexcept
    {
        for (int i = 0; i < voiceCount; ++i)
            if (voices[i].isActive() && voices[i].isHeld())
                voices[i].noteOff(p.voice);
    }

    // MIDI CC120: immediate silence, including the output filter state, so
    // there is no tail left ringing.
    void allSoundOff() noexcept
    {
        for (int i = 0; i < voiceCount; ++i)
            voices[i].reset();
        couplingBus = 0.0f;
        coupledLast = 0;
        voiceLast.fill(0.0f);
        dcBlockX1 = dcBlockY1 = dcBlockX1R = dcBlockY1R = 0.0f;
    }

    // The voices are mono -- one tine, one pickup -- so the instrument is
    // summed once and the channels only separate afterwards, in the tremolo.
    // Everything downstream of that has to be per channel, though: the DC
    // blocker is stateful and the limiter is nonlinear, so sharing either
    // would couple the two sides back together.
    inline void render(const EngineParams& p, float& left, float& right) noexcept
    {
        shaper.setGeometry(p.voice.pickupDistance, p.voice.pickupOffset);
        setVoiceCount(p.polyphony);

        if (--pitchCountdown <= 0) {
            pitchCountdown = kPitchInterval;
            updatePitch(p);
        }

        // Sympathetic resonance: every undamped tine hears what the others are
        // doing, through the frame they share.  The bus is last sample's sum,
        // which breaks the algebraic loop -- and the delay is physically
        // honest anyway, since the frame does not transmit instantly.
        //
        // The gain has to be small.  These resonators peak at their own Q,
        // which key variation pushes past 3000, so it is the *loop* gain that
        // must stay under unity and the coupling is divided down accordingly.
        // Divided by however many tines are listening, so this is the *average*
        // of the others rather than their sum.  With a sum the loop gain grew
        // with the number of held notes: a chord was fine and a pedalled
        // handful of notes turned the instrument into a self-sustaining drone
        // that never decayed, then diverged outright.  Averaging makes the
        // control mean the same thing whether two notes are held or seventy.
        const float couple = p.voice.sympathetic * couplingScale
                           / (float) juce_max(1, coupledLast - 1);
        const bool coupling = p.voice.sympathetic > 0.0f;

        int coupledNow = 0;
        float sum = 0.0f;
        for (int i = 0; i < voiceCount; ++i)
            if (voices[i].isActive()) {
                // A damper resting on a tine stops it responding to anything.
                const bool damperOff = voices[i].isHeld() || p.voice.sustainPedal;
                const bool coupled = damperOff && coupling;
                // What the *other* tines are doing.  Feeding a voice its own
                // output back merely alters its own decay -- lengthening or
                // shortening it depending on the phase the round trip happens
                // to have -- which is not sympathy, and measured as the held
                // notes getting quieter the more of it was applied.
                const float drive = coupled ? (couplingBus - voiceLast[i]) * couple
                                            : 0.0f;
                const float out = voices[i].process(p.voice, shaper, drive, coupled);
                voiceLast[i] = out;
                sum += out;
                coupledNow += coupled ? 1 : 0;
            } else {
                voiceLast[i] = 0.0f;
            }
        couplingBus = sum;
        coupledLast = coupledNow;

        sum *= 0.5f * p.masterLin;

        float l = sum, r = sum;
        if (p.tremoloOn) {
            float gl = 1.0f, gr = 1.0f;
            tremoloGains(p, gl, gr);
            l *= gl;
            r *= gr;
        }

        left  = std::tanh(dcBlock(l, dcBlockX1, dcBlockY1));
        right = std::tanh(dcBlock(r, dcBlockX1R, dcBlockY1R));
    }

    // Mono convenience, for the offline probes in tests/.
    inline float process(const EngineParams& p) noexcept
    {
        float l = 0.0f, r = 0.0f;
        render(p, l, r);
        return l;
    }

    int activeVoices() const noexcept
    {
        int n = 0;
        for (int i = 0; i < voiceCount; ++i)
            n += voices[i].isActive() ? 1 : 0;
        return n;
    }

    void updatePitch(const EngineParams& p) noexcept
    {
        vibratoPhase += float(kPitchInterval * p.vibratoRateHz / sampleRate);
        if (vibratoPhase >= 1.0f)
            vibratoPhase -= 1.0f;

        const bool vibrato = p.vibratoDepthCents > 0.0f;
        const float bend = p.bendWheel * p.bendRangeSemis;
        const bool moving = vibrato || bend != 0.0f;
        if (! moving && ! pitchMoving)
            return;

        const float bendCents = bend * 100.0f;
        for (int i = 0; i < voiceCount; ++i) {
            if (! voices[i].isActive())
                continue;
            float cents = bendCents;
            if (vibrato) {
                const float phase = vibratoPhase + voices[i].vibratoPhaseOffset();
                cents += p.vibratoDepthCents
                       * std::sin(2.0f * float(kPi) * phase);
            }
            voices[i].retune(p.voice, std::pow(2.0f, cents / 1200.0f));
        }
        pitchMoving = moving;
    }

    void configureAll(const EngineParams& p) noexcept
    {
        for (int i = 0; i < voiceCount; ++i)
            if (voices[i].isActive())
                voices[i].configure(p.voice, /*releasing=*/!voices[i].isHeld());
    }

private:
    // Prefer the same note, then a free voice, then the quietest released one,
    // then the quietest held one.  Stealing by level rather than by age is what
    // makes a limited voice count survive pedalled playing: with the damper up
    // a Rhodes note rings for tens of seconds, so under the pedal every voice
    // is legitimately busy and something has to give -- it should be whatever
    // is least audible.  Pd's [poly] cycled blindly regardless of level.
    // A repeat of a pitch that is still sounding goes back to the *same* voice,
    // ahead of any free one: a Rhodes has one tine per note, and striking it
    // again while it is still moving is what produces the in-phase /
    // out-of-phase variation between repeats.  Voice::noteOn is told this is a
    // restrike so it leaves the resonator state alone.
    //
    // This used to take a fresh voice instead, because restriking put a step in
    // the output.  That step came from the retrigger mute, not from the
    // physics, and it is gone now that a restrike does not mute at all.
    int allocate(int midiNote) noexcept
    {
        for (int i = 0; i < voiceCount; ++i)
            if (voices[i].isActive() && (int)voices[i].getNote() == midiNote)
                return i;

        for (int i = 0; i < voiceCount; ++i)
            if (!voices[i].isActive())
                return i;

        int best = -1;
        float bestLevel = 0.0f;
        for (int i = 0; i < voiceCount; ++i) {
            if (voices[i].isHeld())
                continue;
            if (best < 0 || voices[i].getLevel() < bestLevel) {
                best = i;
                bestLevel = voices[i].getLevel();
            }
        }
        if (best >= 0)
            return best;

        best = 0;
        bestLevel = voices[0].getLevel();
        for (int i = 1; i < voiceCount; ++i)
            if (voices[i].getLevel() < bestLevel) { best = i; bestLevel = voices[i].getLevel(); }
        return best;
    }

    // What this particular strike does.  Seeded from a fixed value in
    // prepare(), so a render is reproducible: the instrument varies, the
    // recording of it does not.
    inline float nextRandom() noexcept
    {
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        return (float)(rngState >> 8) * (2.0f / 16777216.0f) - 1.0f;   // [-1, 1)
    }

    inline StrikeVariation drawStrike(float amount) noexcept
    {
        StrikeVariation sv;
        if (amount <= 0.0f)
            return sv;   // exactly the old behaviour, bit for bit

        // Now and then a strike lands harder and shorter than asked for.  A
        // player does not produce a smooth distribution of attacks, and it is
        // the outliers that stop a passage sounding sequenced.
        const bool unexpected = (nextRandom() > 1.0f - amount * 0.12f);
        const float scale = unexpected ? 3.0f : 1.0f;

        sv.contact   = std::pow(2.0f, amount * nextRandom() * 1.1f * scale);
        sv.amplitude = 1.0f + amount * nextRandom() * 0.30f * scale;
        sv.delaySec  = amount * nextRandom() * 0.0012f * scale;
        if (sv.amplitude < 0.05f) sv.amplitude = 0.05f;
        return sv;
    }

    inline float dcBlock(float x, float& x1, float& y1) noexcept
    {
        const float y = x - x1 + dcBlockCoeff * y1;   // hip~ 5
        x1 = x;
        y1 = y;
        return y;
    }

    // Blend of a sine and a triangle, matching the patch's shape control.
    inline void tremoloGains(const EngineParams& p, float& gl, float& gr) noexcept
    {
        tremoloPhase += float(p.tremoloRateHz / sampleRate);
        if (tremoloPhase >= 1.0f) tremoloPhase -= 1.0f;

        const float sine = 0.5f * std::cos(2.0f * float(kPi) * tremoloPhase) + 0.5f;
        const float tri  = 2.0f * std::fabs(tremoloPhase - 0.5f);
        const float blend = std::min(1.0f, std::max(0.0f, p.tremoloShape / 127.0f));
        const float lfo = sine * (1.0f - blend) + tri * blend;

        gl = 1.0f - p.tremoloDepthLin * (1.0f - lfo);
        // Antiphase: loud where the left is quiet.  Same depth, opposite swing,
        // so the sum of the two stays put and the movement is all in the image.
        gr = p.tremoloStereo ? 1.0f - p.tremoloDepthLin * lfo : gl;
    }

    double sampleRate = 48000.0;
    int voiceCount = 32;
    std::array<Voice, kMaxVoices> voices;

    // One shared waveshaper table for all voices; see PickupShaper.
    PickupShaper shaper;

    float tremoloPhase = 0.0f;

    // Bend and vibrato move a note that is already sounding, which means
    // re-deriving every resonator on every voice.  That is far too much to do
    // per sample, so it happens at a control rate -- 64 samples is 750 Hz at
    // 48k, which is smooth against a vibrato of a few Hz and cheap enough to
    // leave running.
    static constexpr int kPitchInterval = 64;
    float vibratoPhase = 0.0f;
    int   pitchCountdown = 0;
    // Whether anything was moving the pitch last time.  Without this, letting
    // the wheel go would leave every sounding note bent: the work is skipped
    // when nothing is moving, so the pass that puts the notes *back* has to be
    // allowed to happen once after it stops.
    bool  pitchMoving = false;
    float couplingScale = kCouplingScale;
    float couplingBus = 0.0f;
    int coupledLast = 0;
    std::array<float, kMaxVoices> voiceLast {};
    uint32_t rngState = 0x9E3779B9u;
    float dcBlockX1 = 0.0f, dcBlockY1 = 0.0f;
    float dcBlockX1R = 0.0f, dcBlockY1R = 0.0f;
    float dcBlockCoeff = 0.999f;
};

} // namespace epmk2
