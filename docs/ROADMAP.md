# EP-MK2 roadmap

What MK2 is for. Each item says what it is, why it is worth doing, how it lands
in *this* codebase, and how we will know it worked.

Ordering below is roughly by ratio of audible payoff to risk, not by section.
`docs/MODEL-NOTES.md` holds the physical meaning of the existing controls and
the Rhodes/Wurlitzer/clav/vibraphone morph axes; this file is about structure
the current model cannot express at all.

---

## Part 1 — Structure the measurements demand

### 1.1 More tine modes (7.1, 20.4, **39.7**)

**Now:** two bandpass resonators, `tine1` and `tine2`, at `f0 * 7.1` and
`f0 * 20.4`, summed, sharing one `tineQ`.

**Measured:** Gabrielli et al. (JASA 148(5), 2020) found modes at 7.1 (σ 0.3),
20.4 (σ 0.4) and 39.7.

**Implementation.** Structurally trivial — a third `Biquad` in `Voice`, summed
with the others. Two things are not trivial:

* **Nyquist.** At 48 kHz, `39.7 * f0 < 24 kHz` only below `f0 ≈ 604 Hz`, i.e.
  roughly D5. Above that the mode does not exist and must be *skipped*, not
  clamped — `designBandpass` clamps to 20 kHz today, which parks a high-Q
  resonator just under Nyquist and is worse than omitting it. The same applies
  to mode 2 at the top: `20.4 * f0` passes Nyquist at `f0 ≈ 1176 Hz` (D6), and
  this is already the known cause of the 4 dB parity gap at note 88.
  So: per-mode `enabled = (ratio * f0 < 0.45 * sampleRate)`.
* **Per-mode Q and level.** One shared `tineQ` is a simplification. Higher
  modes damp faster in every measurement. Give each mode its own Q multiplier
  and level.

**Verify:** FFT a rendered note, check for peaks at the expected ratios; sweep
the whole keyboard and confirm no resonator is ever placed above `0.45 * SR`.

### 1.2 Sub-fundamental (0.58–0.83)

**Measured:** the tone bar produces a partial *below* f0.

**Implementation.** A fourth bandpass fed from the tone bar path. **Watch the
body highpass:** `bodyHighpass` is a highpass at exactly `f0`, so a partial at
`0.7 * f0` routed through it gets attenuated by design. The sub must either
bypass that filter or be summed after it. This is the kind of interaction that
silently produces "the feature does nothing" — check it explicitly with the
mode soloed.

**Verify:** solo the sub, confirm a peak at the intended ratio at the intended
level, then confirm it survives the full chain.

### 1.3 Q that varies across the keyboard

**Now:** one global `toneQ` (default 1642.18) and one `tineQ` (225).

**Measured:** fundamental Q between 731 and 2175 across the keyboard (Shear,
UCSB 2011, Table 2.1). A single value is wrong at both ends — the bass rings
too short, the treble too long, or the compromise is wrong everywhere.

**Implementation.** `configure()` already runs per voice with `frequency` in
hand, so this costs nothing extra: replace the scalar with `Q(note)` from a
curve fitted to Table 2.1. Keep the existing panel control as a **scale factor
over that curve** rather than an absolute Q, so the parameter still does
something musical and presets stay meaningful.

**Verify:** measure T60 per note from rendered notes and compare against the
table; and against the sample benchmark (§3), which gives decay times directly.

### 1.4 A pickup with geometry instead of two dB knobs

**Now:** `pickupGain` and `pickupSymmetry` — the latter an exponent in a
tabulated curve `y = (2^(s·x) − 1) / 2^s` (see `PickupShaper`).

**Measured:** the papers model the pickup from tine–pickup **distance** and
vertical **offset**. The sidebands around each inharmonic mode fall out of that
geometry; they cannot be produced by a symmetric gain control.

**Implementation.** This is a smaller change than it sounds, because
`PickupShaper` is already a tabulated static curve shared across voices — swap
the *table generator*, keep the interpolation and the sharing. A magnetic
pickup's flux falls off with distance, so something of the form
`B(x) = 1 / (1 + ((x − offset) / distance)²)` , with the output proportional to
`dB/dt`, gives both the asymmetry and the distance-dependent harmonic content.
The differentiator is new and belongs in `Voice`, not the shared table.

Expose `distance` and `offset`; keep `gain`. Retire `symmetry`, or keep it as a
preset-compatibility alias — but note that `pickup_symmetry` is currently the
strongest Rhodes↔Wurlitzer axis we have (`MODEL-NOTES.md` §1), so it must not
simply disappear.

**Verify:** confirm sidebands appear around the tine modes and move with
`offset`; A/B against the benchmark samples' spectra.

### 1.5 Oversampling, targeted

**Now:** none. Measured aliasing is confined to the bottom octave: +1.6 dB in
the 800 Hz–4 kHz band at A0, from the pickup waveshaper.

**Implementation.** Do **not** oversample the whole engine — the resonators are
linear and gain nothing from it, and `dsp/` is deliberately framework-free so
`juce::dsp::Oversampling` is not available there anyway. Oversample only the
nonlinear block: `tanhApprox → PickupShaper → buzzFourth`. 2× with a short
polyphase halfband is enough, and it is the only part that generates new
harmonics. Optionally engage it only below some note, since that is where the
problem measurably is.

**Verify:** re-run the A0 aliasing measurement; confirm the CPU cost lands
where expected (the nonlinearity is a small fraction of per-voice cost).

---

## Part 2 — Behaviour: a note that is not identical every time

From the GSi argument: a sample sounds identical every time, and a physical
model that also sounds identical every time has given up its main advantage.
These are as important as Part 1, and cheaper.

One correction to the source, for accuracy rather than pedantry: a Rhodes has
no soundboard in the piano sense. The tines and tone bars are mounted on a
harp/frame assembly. The coupling the video describes is real — it is just
through the frame and the shared pickup rail, not a soundboard. That changes
how strong the effect should be, not whether to model it.

### 2.1 Hammer strike phase — in phase vs out of phase

**The observation:** the hammer always strikes the same tine, but the tine may
already be moving. Whether the strike is with or against that motion changes
the attack.

**Now: we actively destroy this.** `Voice::noteOn` sets `gateRestoreAt` and
calls `clearFilterState()` 2 ms in, wiping the resonator state. That was added
deliberately, and for a good reason — retriggering a still-ringing voice
produced a step in the output and an audible click (measured jump 0.277). The
fix was a fresh voice plus a state clear, and it took the noodling render from
several glitches to zero.

But the click was a *gate discontinuity*, not a physical consequence. The
correct fix keeps the ringing tine and removes the step:

* Do not clear filter state on restrike. Let the new excitation sum into a
  resonator that is already moving — the in-phase/out-of-phase behaviour then
  falls out of the physics for free, with no randomness needed.
* Remove the click at its source: no instantaneous gate change. Either
  crossfade the gate over a few ms, or drop the reset envelope entirely once
  state is no longer being wiped (the step existed *because* of the wipe).

This is the single most interesting item on the list: it makes the model more
physical and less code, and it converts a known workaround back into a feature.

**Verify:** the existing discontinuity detector in `tests/play_midi.cpp` — the
`ep_noodling.MID` render must stay at **0 discontinuities**. Then confirm
repeated strikes of one note actually differ, by rendering the same note twice
at a controlled interval and diffing.

### 2.2 Sympathetic resonance

**The observation:** with the sustain pedal down, undamped tines are excited by
their neighbours through the shared frame.

**Implementation.** A shared coupling bus in `Engine`: sum voice outputs, feed a
scaled fraction into the tine input of every voice whose damper is *off*
(`!held` with pedal down, plus the always-undamped top octave on a real
Rhodes). Two hazards:

* **Feedback stability.** These are Q≈1600 resonators; a coupling loop can and
  will blow up. Break the algebraic loop with a one-block delay and keep total
  loop gain well under unity, with a hard limiter as a backstop.
* **Cost.** Coupling means idle-but-undamped voices now do work. Voices already
  retire at −80 dBFS; sympathetic excitation will keep them alive longer, so
  measure the pedal-down voice count before and after.

**Verify:** hold a chord silently (pedal down, keys struck and released), strike
a low note, and confirm energy appears at the held notes' frequencies. Then
re-check CPU at 32 sounding voices with the pedal down.

### 2.3 Attack variation, including the unexpected one

**The observation:** the attack is never static; even a heavy strike sometimes
lands differently.

**Implementation.** Per-note random draws applied at `noteOn`: small jitter on
`velocityAmp`, on `strikeDelay` (currently a fixed 2 ms), and on the excitation
width; plus a low-probability "unexpected attack" that departs further. One
`humanize` depth control, 0 = exactly today's behaviour.

**This must be seedable.** A fixed seed per instance, resettable, so renders are
reproducible and every test in `tests/` stays deterministic. A model that
cannot be rendered twice identically is untestable, and several existing checks
compare exact levels.

**Verify:** with depth 0, output is bit-identical to the current build — worth
asserting in the test suite. With depth up, two renders of the same MIDI differ
audibly but neither clips nor glitches.

---

## Part 3 — The benchmark

`Kontakt Factory MKI.xrni` — a Renoise instrument holding **156 FLAC samples**:
13 pitches (E-2 to E-8, one every tritone) × 12 velocity layers (0x0B to 0x7F).
A sampled Rhodes MkI, so it is a reference for *an* instrument, not the ground
truth for all of them.

It is a benchmark, not a target — the point is not to converge on one sampled
piano, it is to have something measurable to argue with.

What it gives us directly, and which roadmap item each feeds:

| measurement | feeds |
| --- | --- |
| partial frequencies vs f0, per pitch | §1.1 mode ratios, §1.2 sub-fundamental |
| per-partial decay (T60), per pitch | §1.3 Q curve — this is the real prize |
| sideband structure around each mode | §1.4 pickup geometry |
| spectral centroid vs velocity, 12 layers | velocity → excitation mapping |
| level vs velocity, 12 layers | `velocityAmp`'s 5-octave curve |

**Licensing:** these are Kontakt factory samples. They stay out of git — the
`.xrni` and anything extracted from it are gitignored, exactly as the paywalled
JASA paper is. Analysis *results* (ratios, decay tables) are ours and can be
committed.

**Tooling to write:** `tools/analyse_samples.py` — unpack the `.xrni`, read the
FLACs, and emit a table of the measurements above. Everything in Part 1 gets
checked against it.

---

## Deliberately not doing yet

* **CLAP** — wanted, and MK1 will get it first; nothing here depends on it.
* **Fitting the model to samples automatically** — parameter optimisation only
  makes sense once the structure is right. Fitting a model that lacks the 39.7
  mode just distorts the parameters it does have to compensate.
* **Preset system beyond the six factory presets** — MK2 inherits Rhodes
  MkI/Bright, Wurlitzer, Clav, Vibraphone and Kalimba from MK1. Morphing
  between them, and user preset management, are still to do. Worth
  doing after §1.4, because the pickup change moves the main morph axis.
