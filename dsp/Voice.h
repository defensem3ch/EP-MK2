// One EP-MK2 voice, ported from ep-voice.pd.
//
// Signal path, in the order the original patch builds it:
//
//   strike ---> tone bar resonator --+--> pickup lowpass -> gain -+
//     |                              |                            |
//     |                              +--> tone bar level ---------+--> out
//     |                                                           |
//     +-> tine highpass -> gate -> 3 tine resonators --+--> level -+
//     |                                                |
//     |                                     tine send -+-> pickup sum
//     +-> hammer level ------------------------------------------> out
//
//   pickup sum -> tanh -> asymmetry -> level -+-> highpass at f0 -> out
//                              |              |
//                              +-> ^4 -> buzz +
//
// Everything is then multiplied by an amplitude envelope and passed through
// the keytracked lowpass.
//
// Deliberately framework-free so it can be diffed against the hvcc build.
#pragma once

#include "Biquad.h"
#include "Nonlinear.h"

namespace epmk2 {

// Per-voice parameters, already converted out of the dB the GUI uses.
// `*Lin` fields are linear gains; frequencies are Hz; Q values are Q.
struct VoiceParams {
    // tine.  Three inharmonic modes; the ratios are measured, not derived --
    // an ideal cantilever would give 6.27 and 17.55, which is audibly a
    // different instrument (see docs/MODEL-NOTES.md).
    float tineRatio1 = 7.1f;      // measured: Gabrielli et al. 2020, sigma 0.3
    float tineRatio2 = 20.4f;     //                                 sigma 0.4
    float tineRatio3 = 39.7f;
    float tineHighpassHz = 20.0f;
    float tineQ = 225.0f;
    // Relative levels of modes 2 and 3.  Mode 1 is the reference at unity.
    float tineMode2LevelLin = 1.0f;
    float tineMode3LevelLin = 0.501187f;  // -6 dB
    // How much faster the higher modes damp: Q_n = tineQ * (ratio1/ratio_n)^d.
    // 0 gives every mode the same Q, which is what the model did before mode 3
    // existed.  Physically this should be positive; the value wants measuring
    // off the sample benchmark rather than guessing, so it defaults to off.
    float tineModeDamping = 0.0f;
    float tineLevelLin = 1.0f;
    float tineSendLin = 0.000141f;   // -77 dB

    // tone bar
    float toneQ = 1642.18f;          // note-on
    float toneReleaseQ = 30.0f;      // note-off
    float toneLevelLin = 1.0f;
    float hammerLevelLin = 1.0e-5f;  // -100 dB
    float noteOffLevelLin = 0.0128f; // -37.9 dB

    // pickup
    float pickupGainLin = 5.62f;     // +15 dB
    float pickupAttackLin = 0.316f;  // -10 dB
    float pickupLowpassHz = 2000.0f;
    // NOT the 7 shown on the panel.  Every "snd-" control in the patch goes
    // through [+ 100] -> [dbtorms] before it reaches a voice, so the panel's
    // 7 arrives as 10^(7/20) = 2.239.  It matters more here than anywhere
    // else because this value is an exponent: feeding 7 in raw turns the
    // pickup into a far harsher waveshaper than the model intends, and the
    // extra harmonics read as a Wurlitzer bark rather than a Rhodes.
    float pickupSymmetryLin = 2.23872f;   // panel: 7 dB
    float pickupLevelLin = 2.0f;     // +6 dB
    float buzzLevelLin = 1.0f;
    float buzzPhase = 1.0f;          // +1 or -1

    bool sustainPedal = false;
};

class Voice {
public:
    void prepare(double sr) noexcept
    {
        sampleRate = sr;
        // ~50 ms peak-follower release.
        envelopeDecay = float(std::exp(-1.0 / (0.05 * sr)));
        reset();
    }

    void clearFilterState() noexcept
    {
        toneBar.reset();
        tine1.reset();
        tine2.reset();
        tine3.reset();
        tineHighpass.reset();
        pickupLowpass.reset();
        bodyHighpass.reset();
        keytrack1.reset();
        keytrack2.reset();
    }

    void reset() noexcept
    {
        clearFilterState();
        strikeRamp = noteOffRamp = 0.0f;
        strikeInc = noteOffInc = 0.0f;
        gate = 0.0f;
        gateTarget = 0.0f;
        gateInc = 0.0f;
        strikeDelay = 0;
        envelope = 0.0f;
        active = false;
        held = false;
    }

    bool isActive() const noexcept { return active; }
    float getNote()  const noexcept { return note; }
    bool  isHeld()   const noexcept { return held; }

    // Current output level, for choosing which voice to steal.
    float getLevel() const noexcept { return envelope; }

    // `restrike` means this is the same tine being struck again while it is
    // still moving -- a repeat of the pitch this voice is already playing, not
    // a new note taking the voice over.  The two cases have to behave
    // differently: see the end of this function.
    void noteOn(float midiNote, float velocity, float freqHz,
                const VoiceParams& p, bool restrike = false) noexcept
    {
        note = midiNote;
        held = true;
        active = true;

        frequency = freqHz < 20.0f ? 20.0f
                  : (freqHz > 20000.0f ? 20000.0f : freqHz);

        // 2^(-5 * (1 - (vel-1)/126)): a five-octave velocity range, matching
        // the patch's [/ 126] -> [1 $1] -> [-] -> [* -5] -> [2 $1] -> [pow].
        const float v = velocity < 0.0f ? 0.0f : (velocity > 127.0f ? 127.0f : velocity);
        velocityAmp = std::pow(2.0f, -5.0f * (1.0f - (v - 1.0f) / 126.0f));

        configure(p, /*releasing=*/false);

        // The excitation is a single cycle of raised cosine: a ramp from 1 to
        // 0 over one period, mapped through cos() so it rises from and returns
        // to zero.  The patch delays it 2 ms behind the note.
        strikePeriodSamples = float(sampleRate / frequency);
        strikeDelay = int(0.002 * sampleRate);
        strikeRamp = 1.0f;
        strikeInc = strikePeriodSamples > 0.0f ? -1.0f / strikePeriodSamples : -1.0f;

        if (restrike) {
            // The hammer has struck a tine that is still vibrating.  Keep every
            // resonator exactly as it is and let the new excitation sum into
            // it: whether the strike happens to arrive with or against the
            // tine's current motion then falls out of the physics, and the
            // attack differs from one repeat to the next without any randomness
            // being added.  A real Rhodes has one tine per note, so this is
            // also simply what the instrument does.
            //
            // No mute, no state clear, and so no step to hide.
            gate = 1.0f;
            gateInc = 0.0f;
            gateRestoreAt = 0;
            noteOffRamp = 0.0f;   // cancel a release thump still in flight
        } else {
            // A different note is taking this voice over, so its resonators are
            // tuned to the wrong frequency and would be audible garbage.  Fade
            // out over 1 ms, and at +2 ms -- with the gate already at zero, so
            // it cannot be heard -- clear the filter state before restoring
            // gain.  Without the clear, the old note's ringing comes back the
            // instant the gate reopens, which is a step and audible as a click.
            gateTarget = 0.0f;
            gateInc = -gate / float(0.001 * sampleRate);
            gateRestoreAt = int(0.002 * sampleRate);
        }
    }

    void noteOff(const VoiceParams& p) noexcept
    {
        held = false;
        if (p.sustainPedal)
            return;

        // Release swaps the tone bar to its (much lower) release Q and fires a
        // second excitation pulse scaled by the note-off level -- the thump of
        // the damper landing on the tine.
        configure(p, /*releasing=*/true);
        noteOffRamp = 1.0f;
        noteOffInc = strikePeriodSamples > 0.0f ? -1.0f / strikePeriodSamples : -1.0f;
    }

    // Called when the pedal lifts while this voice is no longer held.
    void pedalReleased(const VoiceParams& p) noexcept
    {
        if (!held && active)
            noteOff(p);
    }

    inline float process(const VoiceParams& p, const PickupShaper& shaper) noexcept
    {
        // --- amplitude reset envelope --------------------------------------
        if (gateRestoreAt > 0 && --gateRestoreAt == 0) {
            clearFilterState();
            gate = 1.0f;
            gateInc = 0.0f;
        } else if (gateInc != 0.0f) {
            gate += gateInc;
            if (gate <= 0.0f) { gate = 0.0f; gateInc = 0.0f; }
        }

        // --- excitation ----------------------------------------------------
        float strike = 0.0f;
        if (strikeDelay > 0) {
            --strikeDelay;
        } else if (strikeRamp > 0.0f) {
            strike = raisedCosine(strikeRamp);
            strikeRamp += strikeInc;
            if (strikeRamp < 0.0f) strikeRamp = 0.0f;
        }
        strike *= velocityAmp;

        float release = 0.0f;
        if (noteOffRamp > 0.0f) {
            release = raisedCosine(noteOffRamp) * p.noteOffLevelLin;
            noteOffRamp += noteOffInc;
            if (noteOffRamp < 0.0f) noteOffRamp = 0.0f;
        }

        // --- tone bar ------------------------------------------------------
        const float toneRaw = toneBar.process(strike + release) * 0.707946f;

        // --- tine ----------------------------------------------------------
        // Modes above Nyquist are skipped rather than run: see configure().
        const float tineIn = tineHighpass.process(strike) * gate;
        float tineRaw = 0.0f;
        if (mode1Active) tineRaw += tine1.process(tineIn);
        if (mode2Active) tineRaw += tine2.process(tineIn) * p.tineMode2LevelLin;
        if (mode3Active) tineRaw += tine3.process(tineIn) * p.tineMode3LevelLin;

        // --- pickup --------------------------------------------------------
        const float pickupIn = pickupLowpass.process(strike * p.pickupAttackLin + toneRaw)
                                   * p.pickupGainLin
                             + tineRaw * p.tineSendLin;

        const float shaped = shaper.process(tanhApprox(pickupIn));
        // The patch's object is [*~ -1], but its right inlet is driven by the
        // buzz-phase control, which replaces the -1 argument: the toggle sends
        // +1 or -1 through [* 2] -> [- 1].  So the sign comes from the control,
        // not from the object's argument.
        const float buzz = buzzFourth(shaped) * p.buzzPhase * p.buzzLevelLin;
        const float pickupOut = bodyHighpass.process(shaped * p.pickupLevelLin + buzz);

        // --- sum and output ------------------------------------------------
        const float mix = strike * p.hammerLevelLin
                        + pickupOut
                        + toneRaw * p.toneLevelLin
                        + tineRaw * p.tineLevelLin;

        const float out = keytrack2.process(keytrack1.process(mix * gate));

        // Peak follower, used both to retire the voice and to pick which one
        // to steal.  The Pd version had neither: with no [switch~] it simply
        // ran all 32 voices forever.
        const float mag = std::fabs(out);
        envelope = mag > envelope ? mag : envelope * envelopeDecay;

        // Retire once inaudible.  This deliberately does not require the key
        // to be up: with the sustain pedal down a released note keeps `held`
        // false but never decays, and a note held to silence should also go.
        if (strikeRamp <= 0.0f && noteOffRamp <= 0.0f && envelope < kSilence)
            active = false;

        return out;
    }

    // Re-derive every filter from the current parameters.  Called on note-on
    // and whenever a parameter that feeds a coefficient changes.
    void configure(const VoiceParams& p, bool releasing) noexcept
    {
        const double sr = sampleRate;
        toneBar.setCoeffs(designBandpass(frequency,
                                         releasing ? p.toneReleaseQ : p.toneQ, sr));

        // A mode whose frequency is past Nyquist does not exist on the real
        // instrument at that pitch, and must be skipped rather than clamped.
        // designBandpass clips to 20 kHz, so clamping parks a Q-225 resonator
        // just under Nyquist where nothing should be at all -- which is what
        // used to happen to mode 2 above ~1 kHz, and is the known cause of the
        // 4 dB error at the top of the keyboard.
        const double limit = kNyquistFraction * sr;
        auto setMode = [&](Biquad& b, float ratio, bool& active) {
            const double f = (double)frequency * (double)ratio;
            active = f > 0.0 && f < limit;
            if (active) {
                // Higher modes damp faster.  With tineModeDamping at 0 every
                // mode keeps the same Q, which is the pre-mode-3 behaviour.
                double q = (double)p.tineQ;
                if (p.tineModeDamping != 0.0f && ratio > 0.0f && p.tineRatio1 > 0.0f)
                    q *= std::pow((double)p.tineRatio1 / (double)ratio,
                                  (double)p.tineModeDamping);
                if (q < 0.5) q = 0.5;
                b.setCoeffs(designBandpass((float)f, (float)q, sr));
            } else {
                // Clear it so it cannot ring on if it becomes active again.
                b.reset();
            }
        };
        setMode(tine1, p.tineRatio1, mode1Active);
        setMode(tine2, p.tineRatio2, mode2Active);
        setMode(tine3, p.tineRatio3, mode3Active);
        tineHighpass.setCoeffs(designHighpass(p.tineHighpassHz, kFilterQ, sr));
        pickupLowpass.setCoeffs(designLowpass(p.pickupLowpassHz, kFilterQ, sr));
        bodyHighpass.setCoeffs(designHighpass(frequency, kFilterQ, sr));

        // Keytracked lowpass on the voice output: clip(100*f^1.3 - 2000, 200, 20000).
        // Measured to be doing nothing audible at the shipped settings, but kept
        // for parity with the Pd version until that is revisited.
        double cutoff = 100.0 * std::pow((double)frequency, 1.3) - 2000.0;
        if (cutoff < 200.0)   cutoff = 200.0;
        if (cutoff > 20000.0) cutoff = 20000.0;
        keytrack1.setCutoff(cutoff * 1.5538, sr);
        keytrack2.setCutoff(cutoff * 1.5538, sr);

    }

private:
    // The Q the patch hands to every plain lowpass/highpass in the voice.
    static constexpr float kFilterQ = 0.404061f;

    // Modes are skipped above this fraction of the sample rate.  Short of
    // Nyquist itself: a high-Q bandpass placed right at the edge is both
    // numerically poor and inaudible.
    static constexpr float kNyquistFraction = 0.45f;

    // -80 dBFS.  Below this a voice cannot be heard even solo, and holding on
    // any longer just burns CPU: with the pedal down the tone bar's Q of 1642
    // means a note takes the better part of a minute to reach -100 dB.
    static constexpr float kSilence = 1.0e-4f;

    // A ramp of 1 -> 0 mapped to a single raised-cosine hump.  In Pd this is
    // [*~ 0.5] -> [+~ 0.75] -> [cos~], and cos~ takes its phase in cycles.
    static inline float raisedCosine(float ramp) noexcept
    {
        return std::cos(2.0f * float(M_PI) * (0.75f + 0.5f * ramp));
    }

    double sampleRate = 48000.0;
    float  frequency = 440.0f;
    float  note = 69.0f;
    float  velocityAmp = 1.0f;

    Biquad toneBar, tine1, tine2, tine3, tineHighpass, pickupLowpass, bodyHighpass;
    bool mode1Active = true, mode2Active = true, mode3Active = false;
    OnePole keytrack1, keytrack2;

    float strikeRamp = 0.0f, strikeInc = 0.0f, strikePeriodSamples = 0.0f;
    float noteOffRamp = 0.0f, noteOffInc = 0.0f;
    int   strikeDelay = 0;

    float gate = 0.0f, gateTarget = 0.0f, gateInc = 0.0f;
    int   gateRestoreAt = 0;

    float envelope = 0.0f;
    float envelopeDecay = 0.9999f;
    bool active = false;
    bool held = false;
};

} // namespace epmk2
