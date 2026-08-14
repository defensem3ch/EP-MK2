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
#include <vector>

#include "Voice.h"

namespace epmk2 {

struct EngineParams {
    VoiceParams voice;

    // tuning: "N divisions of interval I", as in the Pd patch
    float baseFreq = 440.0f;
    float baseNote = 69.0f;
    float divisions = 12.0f;
    float interval = 2.0f;

    // tremolo
    bool  tremoloOn = false;
    float tremoloRateHz = 3.0f;
    float tremoloShape = 0.0f;       // 0..127, sine -> triangle
    float tremoloDepthLin = 0.355f;  // -9 dB

    float masterLin = 1.0f;
    int   polyphony = 32;
};

class Engine {
public:
    static constexpr int kMaxVoices = 128;

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

    void prepare(double sr, int numVoices) noexcept
    {
        sampleRate = sr;
        voiceCount = std::max(1, std::min(numVoices, kMaxVoices));
        for (int i = 0; i < kMaxVoices; ++i)
            voices[i].prepare(sr);
        tremoloPhase = 0.0f;
        dcBlockX1 = dcBlockY1 = 0.0f;
        dcBlockCoeff = float(1.0 - 2.0 * M_PI * 5.0 / sr);   // hip~ 5
    }

    // The patch's tuning: interval^((note - baseNote) / divisions) * baseFreq.
    float noteToFrequency(float midiNote, const EngineParams& p) const noexcept
    {
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
                 noteToFrequency((float)midiNote, p), p.voice, restrike);
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
        dcBlockX1 = dcBlockY1 = 0.0f;
    }

    inline float process(const EngineParams& p) noexcept
    {
        shaper.setSymmetry(p.voice.pickupSymmetryLin);
        setVoiceCount(p.polyphony);

        float sum = 0.0f;
        for (int i = 0; i < voiceCount; ++i)
            if (voices[i].isActive())
                sum += voices[i].process(p.voice, shaper);

        sum *= 0.5f;

        if (p.tremoloOn)
            sum *= tremolo(p);

        sum *= p.masterLin;

        // hip~ 5, then Pd's tanh~ as a soft limiter.
        const float hp = sum - dcBlockX1 + dcBlockCoeff * dcBlockY1;
        dcBlockX1 = sum;
        dcBlockY1 = hp;

        return std::tanh(hp);
    }

    int activeVoices() const noexcept
    {
        int n = 0;
        for (int i = 0; i < voiceCount; ++i)
            n += voices[i].isActive() ? 1 : 0;
        return n;
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

    // Blend of a sine and a triangle, matching the patch's shape control.
    inline float tremolo(const EngineParams& p) noexcept
    {
        tremoloPhase += float(p.tremoloRateHz / sampleRate);
        if (tremoloPhase >= 1.0f) tremoloPhase -= 1.0f;

        const float sine = 0.5f * std::cos(2.0f * float(M_PI) * tremoloPhase) + 0.5f;
        const float tri  = 2.0f * std::fabs(tremoloPhase - 0.5f);
        const float blend = std::min(1.0f, std::max(0.0f, p.tremoloShape / 127.0f));
        const float lfo = sine * (1.0f - blend) + tri * blend;

        return 1.0f - p.tremoloDepthLin * (1.0f - lfo);
    }

    double sampleRate = 48000.0;
    int voiceCount = 32;
    std::array<Voice, kMaxVoices> voices;

    // One shared waveshaper table for all voices; see PickupShaper.
    PickupShaper shaper;

    float tremoloPhase = 0.0f;
    float dcBlockX1 = 0.0f, dcBlockY1 = 0.0f, dcBlockCoeff = 0.999f;
};

} // namespace epmk2
