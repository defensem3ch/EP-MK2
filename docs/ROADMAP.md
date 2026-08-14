# EP-MK2 roadmap

What MK2 is for. Each item says what it is, why it is worth doing, how it lands
in *this* codebase, and how we will know it worked.

Ordering below is roughly by ratio of audible payoff to risk, not by section.
`docs/MODEL-NOTES.md` holds the physical meaning of the existing controls and
the Rhodes/Wurlitzer/clav/vibraphone morph axes; this file is about structure
the current model cannot express at all.

---

## Part 1 — Structure the measurements demand

### 1.1 More tine modes (7.1, 20.4, **39.7**) — **done, and it exposed 1.6**

**Done:** a third mode at 39.7 with its own level, per-mode Q via a
`Mode Damping` exponent (`Q_n = tineQ * (ratio1/ratio_n)^d`, default 0 so modes
1 and 2 keep their old behaviour), and — the real fix — **Nyquist culling**.

A mode whose frequency passes `0.45 * SR` is now *skipped*, not clamped.
`designBandpass` clips to 20 kHz, so the old code parked a Q-225 resonator just
under Nyquist wherever a mode should have ceased to exist. Mode 2 did this
above roughly C6, and it is the known cause of the 4 dB error at the top of the
keyboard. Verified by the mode level controls being **bit-identical** no-ops at
note 108 and audible at note 45.

**But adding mode 3 changed almost nothing audible, and that is the finding.**
See 1.6 — the excitation cannot reach these modes in the first place.

### 1.6 The hammer excitation was far too narrowband — **done**

The excitation was a single raised-cosine cycle at the note's own period — 9 ms
at A2, 36 ms at A0. Its spectrum collapses above f0, so the tine modes were
driven 65–71 dB below the fundamental and arrived 40–75 dB down in the output.

**Now:** a raised-cosine force pulse whose width is the hammer's **contact
time** in milliseconds, independent of pitch (`Hammer Contact`, default 0.4 ms),
with velocity shortening contact as well as raising force (`Vel to Contact`,
default 1.5 octaves at full velocity) — which is where "harder is brighter"
physically comes from. Amplitude is normalised on **impulse**, not peak: a
hammer imparts momentum, so a shorter contact means a higher peak force for the
same strike, and the contact control changes timbre rather than volume. The
damper thump at note-off keeps its old period-width pulse; that contact really
is soft and slow.

Result at A2, against MK1:

| | MK1 | MK2 |
| --- | --- | --- |
| fundamental | −14.4 dB | −18.9 dB |
| mode 1 (781 Hz) | −54.7 (−40 rel) | −41.6 (**−22.7 rel**) |
| mode 2 (2244 Hz) | −79.9 (−65 rel) | −51.7 (**−32 rel**) |
| mode 3 (4367 Hz) | −89.9 (−75 rel) | −60.5 (**−41 rel**) |

The tine is 17–18 dB more present through the bass and mid-range.

#### What it dragged in: resonator normalisation

The Pd original's `designBandpass` used RBJ **constant skirt gain**, whose gain
at resonance is Q. The tone bar therefore ran at +64 dB and the tine modes at
+47 dB, and **every resonator's level was set by its decay time**. That was
survivable only because the old excitation had almost no energy at the mode
frequencies; a real broadband strike clips instantly, and no amount of gain
staging fixes it because one strike gain cannot restore a balance that came
from two different Q values.

Resonators are now normalised to a **unit impulse response** (`b0 = 1`), so a
struck mode rings at an amplitude set by the strike and its own level control,
whatever its Q or frequency. This was not optional and it is not cosmetic:

* It is what makes **1.3 possible at all**. With the old normalisation, a Q
  varying 731→2175 across the keyboard would have swung the level ~9.5 dB with
  it, purely as an artefact.
* It makes the level controls mean what they say, which the sample fitting in
  Part 3 needs.
* Caveat: steady-state gain at resonance is now `1/alpha`, which is large.
  Harmless for impulsive excitation, but it **will matter for 2.2**, where
  resonators are driven continuously through a coupling bus.

Two calibration constants follow from it, both in `Voice`:
`kReferenceContactSec` (the impulse a unit-velocity strike delivers) and
`kResonatorTrim` (bringing unity-impulse resonators back to a sane level). The
trim is applied to the resonator *outputs* rather than the voice output on
purpose — applied at the end, the level would be right while the pickup's tanh
sat permanently saturated.

#### Rebalanced defaults

* `pickup_attack` −10 → **−31 dB**. Exactly compensates the 11.4× larger
  strike. This control existed to fake the attack transient the missing tine
  modes should have provided; now that they work, it is a trim rather than the
  main event.
* `master` ceiling 0 → **+12 dB**. MK1 inherited a 0 dB ceiling from the Pd
  patch's `dbtorms` convention; with the excitation rebalanced there has to be
  make-up available.

#### Verified

* Modes measurably present: `tests/probe_modes.cpp`.
* **0 unexplained discontinuities** on `ep_noodling.MID`. The raw slope
  detector now fires 233 times, but all 233 are inside note attacks — a 0.4 ms
  contact moves ~0.026 per sample at 48 kHz, so a detector tuned to MK1's 9 ms
  attacks flags the attack itself. `play_midi` now separates the two.
* CPU unchanged at 0.7–0.8% of a core; peak voices 6.

#### Still open

* **Treble tilt.** Output falls ~20 dB from note 21 to 105 (MK1 fell 16 dB, so
  this is a pre-existing characteristic made slightly worse, not a new one). The
  real cause is that a magnetic pickup senses dΦ/dt and so has an inherent
  +6 dB/octave tilt that this model does not have — which **1.4 will supply**.
  Worth resisting the temptation to paper over it with a gain curve first.
* The top octave loses its inharmonic content, because a 0.4 ms contact cannot
  excite 7.1× of 1760 Hz. Physically reasonable, but worth checking against the
  benchmark.
* **The six factory presets were tuned against the old excitation** and want
  revisiting.

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

### 2.1 Hammer strike phase — in phase vs out of phase — **done**

**The observation:** the hammer always strikes the same tine, but the tine may
already be moving. Whether the strike is with or against that motion changes
the attack.

**What was in the way.** `Voice::noteOn` muted the voice and called
`clearFilterState()` 2 ms in, wiping the resonators, and the allocator sent a
repeated pitch to a *fresh* voice. Both were added to kill a retrigger click
(measured jump 0.277). But that click came from the retrigger mute — gate
snapping 0→1 with the old resonators behind it — not from the physics.

**What it is now.** A repeat of a pitch that is still sounding goes back to the
same voice, and that voice is told it is a *restrike*: no mute, no state clear,
the new excitation simply sums into a resonator that is already moving. Whether
it arrives with or against the tine's motion falls out of the physics.

A different note taking a voice over still mutes and clears, because those
resonators are tuned to the wrong frequency and would be audible garbage. The
two cases genuinely differ; conflating them was the original mistake.

**Measured:** two repeats at different gaps come out **17.9% apart** with no
randomness anywhere in the model. `ep_noodling.MID` stays at **0
discontinuities**. Peak voices for that performance fell from 10 to 6 and CPU
from 0.8% to 0.7% of a core, because repeats no longer stack a second voice per
pitch — a real Rhodes has one tine per note.

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
