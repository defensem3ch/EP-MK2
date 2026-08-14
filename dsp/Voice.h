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
    // Q of the fundamental *at the reference note* (A4).  Q now varies across
    // the keyboard rather than being one global value -- see toneQTracking.
    // 1334 is chosen so the curve passes through the range reported for a real
    // instrument, 731 at the bottom of the keyboard to 2175 at the top.
    float toneQ = 1334.0f;
    // Octaves of Q per octave of pitch.  0.217 spans 731 to 2175 over MIDI
    // notes 21 to 108; 0 restores a single global Q.
    //
    // PROVISIONAL.  The 731-2175 range is from Shear (UCSB 2011, Table 2.1)
    // and is not verifiable from the papers on hand, and the shape between the
    // endpoints is assumed log-linear rather than measured.  The sample
    // benchmark gives per-partial decay times directly and should replace
    // both -- see docs/ROADMAP.md Part 3.
    float toneQTracking = 0.217f;
    float toneReleaseQ = 30.0f;      // note-off
    float toneLevelLin = 1.0f;
    float hammerLevelLin = 1.0e-5f;  // -100 dB
    float noteOffLevelLin = 0.0128f; // -37.9 dB

    // Hammer contact time, in milliseconds and independent of pitch.  A Rhodes
    // hammer is a neoprene tip on stiff steel and contact is well under a
    // millisecond, which is what makes the strike broadband enough to drive the
    // tine modes.  The model used to use a single cycle of the note's own
    // period instead -- 9 ms at A2, 36 ms at A0 -- whose spectrum collapses
    // above f0, leaving the modes 40-75 dB down.  See docs/ROADMAP.md 1.6.
    float hammerContactMs = 0.4f;
    // Octaves of shortening at full velocity: a harder strike has shorter
    // contact as well as more force, and that is where "harder is brighter"
    // physically comes from.
    float hammerVelContact = 1.5f;

    // pickup
    float pickupGainLin = 5.6234f;   // +15 dB
    // Off by default.  This injects the hammer pulse straight into the
    // pickup, which no real pickup sees -- it senses the tine, not the
    // hammer.  It existed to fake an attack transient the missing tine modes
    // should have supplied, and once the pickup differentiates its input, a
    // raw sub-millisecond pulse through it is literally a click: it put a
    // 0.42 step at the start of every note against a sustained level of 0.34.
    float pickupAttackLin = 1.0e-5f; // -100 dB
    float pickupLowpassHz = 2000.0f;
    // Pickup geometry, in units of the tine's vibration amplitude.  These
    // replace the old "symmetry" exponent: offset is what actually produces
    // asymmetry, and so it is now the Rhodes-to-Wurlitzer axis.  An offset of
    // 0 puts the tine on the magnetic axis, where the response is purely even
    // and the fundamental disappears.
    float pickupDistance = 0.8f;
    float pickupOffset = 0.8f;
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
        // Unity at A4 for the dPhi/dt differentiator.
        inducedGain = float(sr / (2.0 * M_PI * 440.0));
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
        fluxPrev = 0.0f;
        fluxPrimed = false;
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

        // The excitation is a raised-cosine force pulse: a ramp from 1 to 0
        // over the contact time, mapped through cos() so it rises from and
        // returns to zero.  The patch delays it 2 ms behind the note.
        //
        // Its width is the hammer's contact time, not the note's period.  Tying
        // it to 1/f0 made the excitation relatively *softer* towards the bass,
        // which is backwards: a real hammer's contact time barely depends on
        // pitch.
        const float velNorm = v / 127.0f;
        const float contactSec = p.hammerContactMs * 0.001f
                               * std::pow(2.0f, -p.hammerVelContact * velNorm);
        strikeContactSamples = std::max(2.0f, float(sampleRate * contactSec));
        // A hammer imparts *momentum*, so what velocity sets is the area of the
        // pulse, not its height: shortening contact raises the peak force for
        // the same strike.  Without this, the contact-time control would double
        // as a volume control, and the whole instrument would lose its low end
        // the moment contact was shortened.
        //
        // The reference is the width the old pitch-tied pulse had at A3, so the
        // default contact time leaves the instrument's output level roughly
        // where MK1 had it and this is a change of timbre rather than of level.
        strikeAmp = velocityAmp
                  * (kReferenceContactSec / std::max(1.0e-6f, contactSec));
        // The damper landing at note-off is a different, much softer contact,
        // so it keeps the period-width pulse it always had.
        releasePeriodSamples = float(sampleRate / frequency);
        strikeDelay = int(0.002 * sampleRate);
        strikeRamp = 1.0f;
        strikeInc = -1.0f / strikeContactSamples;

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
        noteOffInc = releasePeriodSamples > 0.0f ? -1.0f / releasePeriodSamples : -1.0f;
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
        strike *= strikeAmp;

        float release = 0.0f;
        if (noteOffRamp > 0.0f) {
            release = raisedCosine(noteOffRamp) * p.noteOffLevelLin;
            noteOffRamp += noteOffInc;
            if (noteOffRamp < 0.0f) noteOffRamp = 0.0f;
        }

        // --- tone bar ------------------------------------------------------
        const float toneRaw = toneBar.process(strike + release) * 0.707946f * kResonatorTrim;

        // --- tine ----------------------------------------------------------
        // Modes above Nyquist are skipped rather than run: see configure().
        const float tineIn = tineHighpass.process(strike) * gate;
        float tineRaw = 0.0f;
        if (mode1Active) tineRaw += tine1.process(tineIn);
        if (mode2Active) tineRaw += tine2.process(tineIn) * p.tineMode2LevelLin;
        if (mode3Active) tineRaw += tine3.process(tineIn) * p.tineMode3LevelLin;
        tineRaw *= kResonatorTrim;

        // --- pickup --------------------------------------------------------
        const float pickupIn = (strike * p.pickupAttackLin + toneRaw) * p.pickupGainLin
                             + tineRaw * p.tineSendLin;

        // The tine's excursion is bounded -- it cannot pass through the pickup
        // -- and tanh is a reasonable bound.  What comes out is displacement.
        // std::tanh rather than the Padé approximant used elsewhere: this feeds
        // a differentiator, which would put the approximant's curvature
        // mismatch straight into the audio.
        const float disp = std::tanh(pickupIn);
        // Flux linking the coil, from the pickup geometry.
        const float flux = shaper.process(disp);
        // The coil senses the *rate of change* of flux, not the flux.  This is
        // the pickup's inherent +6 dB/octave, and it is also what keeps the
        // output free of DC: flux at rest is a large non-zero constant, so a
        // static shaper would put Phi(0) straight into the signal and step by
        // it every time a voice's gate reopened -- a click at the start of
        // every note.  inducedGain normalises the difference to unity at A4.
        if (!fluxPrimed) {
            fluxPrev = flux;
            fluxPrimed = true;
        }
        const float induced = (flux - fluxPrev) * inducedGain;
        fluxPrev = flux;
        // The patch's object is [*~ -1], but its right inlet is driven by the
        // buzz-phase control, which replaces the -1 argument: the toggle sends
        // +1 or -1 through [* 2] -> [- 1].  So the sign comes from the control,
        // not from the object's argument.
        // The coil's own inductance rolls the output off above a corner in the
        // low kHz.  It belongs *after* the differentiator, not before the
        // geometry: a real magnetic pickup is a differentiator followed by an
        // inductive rolloff, which together make a bandpass.  Filtering the
        // input instead leaves the +6 dB/octave running unopposed, and the top
        // of the keyboard drives the limiter.
        const float coil = pickupLowpass.process(induced);
        const float buzz = buzzFourth(coil) * p.buzzPhase * p.buzzLevelLin;
        const float pickupOut = bodyHighpass.process(coil * p.pickupLevelLin + buzz);

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
        // Q rises with pitch.  This is only possible because the resonators
        // are normalised to a constant ringing amplitude: under the old
        // constant-skirt-gain form, Q swept across the keyboard would have
        // swung the level with it by about 9.5 dB, purely as an artefact.
        // The damper's release Q does not track -- that is the damper, not
        // the tine.
        float toneQ = p.toneQ;
        if (!releasing && p.toneQTracking != 0.0f)
            toneQ *= std::pow(2.0f, p.toneQTracking * (note - kQReferenceNote) / 12.0f);
        if (toneQ < 1.0f)      toneQ = 1.0f;
        if (toneQ > 20000.0f)  toneQ = 20000.0f;

        toneBar.setCoeffs(designBandpass(frequency,
                                         releasing ? p.toneReleaseQ : toneQ, sr));

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

    // A4: the pitch at which tone_decay means exactly what it says.
    static constexpr float kQReferenceNote = 69.0f;

    // The impulse a unit-velocity strike delivers, expressed as the width of
    // an equivalent unit-height pulse.  Chosen so the instrument sits at a
    // sensible level with the tine modes now carrying real energy; see the
    // rebalance sweep in tests/probe_modes.cpp.
#ifndef EPMK2_REF_CONTACT
#define EPMK2_REF_CONTACT (1.0f / 220.0f)
#endif
    static constexpr float kReferenceContactSec = EPMK2_REF_CONTACT;

    // Resonators are normalised to a unit impulse response, which leaves them
    // ringing far above unity for a real strike.  One constant brings the whole
    // voice back to the level MK1 ran at -- and because it is applied to the
    // resonator outputs rather than to the output as a whole, it also fixes how
    // hard the pickup's tanh is driven.  Applied at the output instead, the
    // level would be right while the pickup sat permanently saturated.
#ifndef EPMK2_RES_TRIM
#define EPMK2_RES_TRIM 0.0040f
#endif
    static constexpr float kResonatorTrim = EPMK2_RES_TRIM;

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
    float  strikeAmp = 1.0f;
    float  fluxPrev = 0.0f;
    bool   fluxPrimed = false;
    float  inducedGain = 1.0f;

    Biquad toneBar, tine1, tine2, tine3, tineHighpass, pickupLowpass, bodyHighpass;
    bool mode1Active = true, mode2Active = true, mode3Active = false;
    OnePole keytrack1, keytrack2;

    float strikeRamp = 0.0f, strikeInc = 0.0f;
    float strikeContactSamples = 0.0f, releasePeriodSamples = 0.0f;
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
