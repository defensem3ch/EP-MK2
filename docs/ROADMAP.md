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
* **The factory presets were tuned against the old excitation** and want
  revisiting.

### 1.2 Sub-fundamental — **done, from measurement**

A fourth resonator below the fundamental, driven by the same excitation as the
tone bar and routed exactly like it -- into the pickup and into the direct sum
-- because it is the same piece of metal. Controls are `Sub Level` (default
−30 dB) and `Sub Ratio` (default **0.55**).

**The ratio comes from the samples, not the literature.** This section
previously expected 0.58-0.83 from the papers. Measured in the reference
library, the sub sits at **0.42-0.60 x f0 from note 28 to note 70 and is absent
above** -- the bottom of the keyboard agrees with the papers, the middle sits
lower. 0.55 splits the measured range. Levels came out at −32 to −60 dB
relative; the default lands the model at about −31 dB at note 52.

Two hazards this section warned about, both real:

* **The body highpass.** `bodyHighpass` sits at exactly f0, so a partial at
  0.55 x f0 is attenuated by roughly 10 dB on its way through the pickup. The
  feature works anyway, but `Sub Level` is calibrated against what comes *out*,
  not what goes in. Verified by measurement at the output rather than by
  reading the signal path: −69.8 dB to −24.4 dB at 91 Hz when the control is
  opened.
* **Sub-audio frequencies.** At note 21 the sub would land at 15 Hz, where
  `designBandpass` clamps to 20 Hz and parks a resonator at a frequency the
  note does not have. It is skipped below 20 Hz instead, and the control is a
  bit-identical no-op there.

Not modelled: the measurement says the sub disappears above note 70, and the
model keeps it across the whole keyboard. Rolling it off with pitch would be
inventing a curve from an absence, so it is left to `Sub Level`.

### 1.3 Q that varies across the keyboard — **done, provisionally valued**

Q of the fundamental is now a function of pitch:

```
Q(note) = tone_decay * 2^(q_tracking * (note - 69) / 12)
```

`tone_decay` is Q **at A4** rather than everywhere, defaulting to 1334, and
`q_tracking` is octaves of Q per octave of pitch, defaulting to 0.217. That
spans **731 at note 21 to 2095 at note 105**. Setting `q_tracking` to 0
restores a single global Q. The damper's release Q deliberately does not
track -- that is the damper, not the tine.

Measured T60, held, at default:

| note | 21 | 45 | 69 | 93 | 105 |
| --- | --- | --- | --- | --- | --- |
| Q | 731 | 987 | 1334 | 1802 | 2095 |
| flat Q | >40 s | 23.4 | 6.5 | 1.7 | 0.8 |
| tracking | >40 s | 17.3 | 6.5 | 2.3 | 1.3 |

**This was only possible because of the resonator normalisation.** Under the
old constant-skirt-gain form, Q sweeping 731 to 2175 would have swung the level
with it by about 9.5 dB, purely as an artefact, and the feature would have been
unusable. Verified rather than assumed: peak level with tracking on versus off
is 0.4473 against 0.4473 in the bass and 0.5726 against 0.5729 in the treble.
Q sets decay and nothing else.

**The values are provisional and should be treated as scaffolding.** The
731-2175 range is recalled from Shear (UCSB 2011, Table 2.1) and is *not*
verifiable from the papers on hand -- none of them carries a per-note Q table.
The shape between the endpoints is assumed log-linear because that is the
simplest curve through two remembered numbers, not because anything measured
says so. The tine modes do not track at all yet; only the fundamental does.

The sample benchmark (Part 3) measures per-partial decay times directly at 13
pitches, which replaces every number in this section with a measured one. That
is the next thing to build.

### 1.4 A pickup with geometry instead of two dB knobs — **done**

`pickup_symmetry` is gone. In its place are **`pickup_distance`** (the tine's
rest gap from the pole) and **`pickup_offset`** (how far it sits off the
magnetic axis), both in units of the tine's vibration amplitude. Flux follows a
dipole falloff, `Phi ~ 1/(d^2 + (o+x)^2)^1.5`, tabulated and shared across
voices exactly as the old curve was.

Two things fall out of doing it properly, and both were the point:

**Offset is the asymmetry, and so the new Rhodes↔Wurlitzer axis.** Measured at
A2, harmonics relative to the fundamental:

| offset | 2nd | 3rd | 4th |
| --- | --- | --- | --- |
| 0.00 | **+8.9** | −27.9 | +5.4 |
| 0.25 | +4.9 | +0.6 | −0.7 |
| 0.50 | −5.6 | −0.2 | −21.0 |
| **0.80** (default) | −9.5 | −9.3 | −10.8 |
| 1.00 | −5.1 | −23.4 | −15.1 |

At offset 0 the tine sits on the magnetic axis, the response is purely even,
and the fundamental collapses under a 2nd harmonic 9 dB above it — textbook,
and a good check that the geometry is right rather than merely plausible. Note
this reverses the preset mapping that was written before: a Wurlitzer's bark is
*even* harmonics, so it wants a **low** offset, not a high one. 0.80 is chosen
as the default because 2nd, 3rd and 4th all land within about a dB of each
other, which is a neutral place to tune from.

**The coil senses dPhi/dt, which supplies the missing treble tilt.** A
differentiator normalised to unity at A4 now sits after the flux table, and it
is what 1.6 left open. Output across the keyboard, relative:

| | note 21 | 45 | 69 | 93 |
| --- | --- | --- | --- | --- |
| after 1.6 | −18.8 | −18.9 | −20.6 | −30.4 |
| **now** | −15.4 | −15.6 | −18.0 | −38.4 |

Flat within 0.2 dB from note 21 to 45 and 2.6 dB out to note 69. The top of the
range is still down, and pushing the pickup harder flattens it further — but
that trade is real and is discussed below.

**Rebalanced:** `pickup_gain` stays at +15 dB, and `kResonatorTrim` came down
to 0.0018 for headroom. Preset `pickup_gain` values were re-levelled twice; all
six now sit within 3.7 dB of each other.

#### What this cost, and what is still provisional

The tilt correction lives in the pickup path, so **how flat the keyboard is
depends on how much of the output comes through the pickup** rather than
through the direct tone-bar and tine sums. Those direct sums are not physical —
a real Rhodes is heard only through its pickup — and cutting them (tone/tine
level to −24 dB, pickup gain to +20) does flatten the keyboard to within 0.6 dB
from note 21 to 69. But it also collapses the tine modes, because `tine_send`
is −77 dB and so the tine barely reaches the pickup at all: the Pd model has
the pickup mostly sensing the *tone bar*, when physically it faces the tine.
Raising `tine_send` does not recover them cleanly either — the response goes
non-monotonic as the geometry saturates.

**So the routing is the next structural question, not a tuning one**, and it is
left as it stands rather than half-changed.

Preset voicing is provisional: levelled and measurably distinct (27–54% apart),
but tuned against numbers, not ears.

#### Correction: the differentiator was not in the build

The commit that introduced 1.4 claimed the pickup senses dPhi/dt. It did not.
The edit that was supposed to replace the static shaper silently failed to
match, so what shipped was the **new geometry table driving the old static
waveshaper**. The keyboard-tilt figures reported at the time came from the
table shape alone, and the conclusion drawn from them -- that the
differentiator had flattened the keyboard -- was wrong. A rebuild was done and
the numbers did not move, which was taken as evidence against a hypothesis
when it was really evidence the code had not changed.

That also caused an audible fault. The flux table is a physical quantity and
`Phi(0)` is a large non-zero constant, where the old symmetry curve had been
zero at zero. Fed through a *static* shaper, that constant became a DC offset
sitting in the voice output, and it stepped into existence the moment the gate
reopened 2 ms into every note: a jump of **0.2969 from exact silence**, the
same magnitude as MK1's original crackle, identical at every pitch and every
contact time. Heard as a click at the start of every note.

The differentiator removes it inherently, since it blocks DC.

#### Two further things the differentiator then required

* **`pickup_attack` now defaults to off (−100 dB).** It injects the hammer
  pulse straight into the pickup, which no real pickup sees -- it senses the
  tine, not the hammer. It existed to fake the attack transient the missing
  tine modes should have supplied. Differentiated, a sub-millisecond pulse
  through it is literally a click: 0.42 at note onset against a sustained level
  of 0.34. With it off, 0.095.
* **The coil rolloff moved after the differentiator.** `pickup_lopass` was
  filtering the pickup's *input*. Physically the coil's inductance rolls off
  its *output*, and a magnetic pickup is a differentiator followed by that
  rolloff -- together a bandpass, not a rising tilt. Filtering the input left
  the +6 dB/octave running unopposed and notes 81 and 93 drove the limiter at
  3.1 pre-limit. Moved, nothing exceeds 0.70.

Keyboard response with all three in place, and this time actually measured
against the built binary:

| note | 21 | 45 | 69 | 81 | 93 |
| --- | --- | --- | --- | --- | --- |
| MK1 | −14.7 | −14.4 | −15.0 | −17.4 | −23.1 |
| MK2 | −24.6 | −22.3 | −18.2 | −17.8 | −21.5 |

MK1 falls 16.4 dB across the keyboard; MK2 spans 6.8 dB, though it now rises
towards the middle rather than falling. Overall level is lower and the master
ceiling at +12 dB is there to make it up.

#### The resonator had direct feedthrough, and that was the click

With the step fixed, a note played quietly still clicked. The cause was older
than the pickup work and had been there since 1.6: the resonator numerator
`(1, 0, -1)` gives `h[0] = 1`, so **the output contained a copy of the hammer
pulse** -- a sub-millisecond hump at full tone level, quite independent of any
resonance. A resonator's displacement cannot instantaneously follow the force
applied to it, so this was never physical, and it is a click by construction.

It showed up worst at low velocity because it does not scale the way the
ringing does: at velocity 20 the attack peaked at 2.3 ms, on the pulse itself,
rather than at 9.6 ms where the resonators finish building.

**Resonators are now two-pole with no zeros**, `b = (0, sin(w0), 0)`:

* `h[0] = 0`, so no feedthrough.
* **−12 dB/octave above resonance** where a bandpass gives only −6, so the
  excitation's out-of-band content is rejected rather than leaking. The pickup
  then differentiates, giving −6 dB/octave overall, which is what a magnetic
  pickup on a struck tine actually does.
* The `sin(w0)` numerator keeps ringing amplitude independent of pitch and Q --
  measured 0.996 to 1.001 across 30 Hz to 4 kHz and Q 225 to 1642, a 0.0 dB
  spread. A bare `b1 = 1` would ring as 1/sin(w0), which is 1/f^2 across the
  keyboard and measured 25x pre-limiter at the bottom.

That last property is not a detail: **it is what makes 1.3 possible**. Q now
sets decay time and nothing else.

Keyboard response, measured against the built binary:

| note | 21 | 45 | 69 | 81 | 93 |
| --- | --- | --- | --- | --- | --- |
| MK1 | −14.7 | −14.4 | −15.0 | −17.4 | −23.1 |
| MK2 | −27.8 | −25.0 | −20.9 | −20.8 | −24.7 |

7 dB across notes 21-93, where MK1 spanned 16.4 dB falling. Level overall is
lower; the +12 dB master ceiling is there for it.

#### What the detectors could not do, and what replaced them

Three attempts failed to catch this class of fault:

* `play_midi`'s slope detector -- the click was perfectly smooth.
* Onset level ratio -- the feedthrough sat only 1.46x above the note's own
  level at velocity 20, under any threshold that would not fire constantly.
* Onset high-frequency content -- measured 2.99x against a clean 1.96x, but
  ranges overlapped across velocity, so nothing separated them.

What works is testing the **property** rather than the symptom, on the filter
rather than through the instrument: `designBandpass` must return `ff1 == 0` at
every frequency and Q, and its ringing amplitude must not vary with either.
Both are now in the suite. The audible-symptom check is kept as a coarse guard,
but the exact invariant is what will fail if this regresses.

The glitch detector was also rebuilt to compare each jump against a running
mean of |derivative| rather than a fixed threshold, and the noodling render now
reports **0 slope events**, down from 269. But the more useful lesson is its
exemption: classifying anything within 30 ms of a note-on as "within a note
attack" is what let a 0.2969 step from silence be reported as normal for two
commits. The onset is exactly where a click is least excusable.

### 1.5 Oversampling — **measured, and not needed**

Not implemented, on evidence. The aliasing this section was written to fix is
no longer there, and the architecture changes that removed it are the ones made
for other reasons.

Measured against an 8x render, probing the exact frequencies a folded harmonic
lands on -- `|n*f0 - k*SR|`, which for most f0 is not a harmonic of f0 at all,
so there is a clean gap to measure in:

| | alias level | below the fundamental |
| --- | --- | --- |
| defaults, note 100 | −134 dB | 104 dB |
| worst case, note 88 | −102 dB | 84 dB |
| worst case, note 100 | −98 dB | **77 dB** |

"Worst case" is the hammer fed straight into the pickup at unity, +24 dB of
pickup drive, buzz at full and the tine send 50 dB above default -- settings
nobody would use. Even there the alias sits at −98 dBFS, about the 16-bit noise
floor, at 16 kHz where hearing is least sensitive. At the bottom of the
keyboard, in the 800 Hz–4 kHz band MK1's measurement used, the native render
has *less* energy than the 8x reference, not more.

Three changes made for other reasons had already fixed it:

* `pickup_attack` defaults to off (1.4), so the broadband hammer pulse no
  longer feeds the nonlinearity at all.
* Resonators became two-pole with **−12 dB/octave above resonance** (2.3's
  click fix), so out-of-band content is rejected *before* the nonlinearity
  rather than after.
* The coil low-pass moved *after* the differentiator (1.4), band-limiting the
  signal before `buzzFourth` raises it to the fourth power.

Oversampling the nonlinear block would cost real CPU to attenuate something
77 dB down at its worst. If the pickup model later gains a sharper
nonlinearity, measure again -- `tests/` has the probe.

#### What the measurement did find

The instrument was **4.6 dB louder at 96 kHz than at 48**, and 9 dB louder at
192. The resonators sum their input sample by sample, but the strike is defined
in seconds, so a higher rate put more samples under the same pulse and drove
them proportionally harder. The excitation is a density, not an amplitude, and
is now scaled by `dt`. Level now varies 0.03 dB across 44.1 to 192 kHz.

That was worth more than the item it was found under: a session's sample rate
is not a tone control, and nothing in the test suite would have caught it,
because every test ran at 48 kHz.

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

### 2.2 Sympathetic resonance — **done**

`Sympathetic` in the Tone Bar section, default 0.35. Every tine whose damper is
off hears what the others are doing, through the frame they share. Measured two ways, because
the first one hides it: with the struck note still ringing it masks the
sympathetic response, and the honest test is to *damp the struck note* and
listen to what is left.

| | held pitches, after the struck note is damped |
| --- | --- |
| off | −49.0 dB |
| 0.35 (default) | ≈ −28 dB |
| 1.0 | **−13.8 dB** |

35 dB at full. The coupling scale was raised from 0.1 to 0.25 once stability
had margin to spare -- it is stable at twice the control's maximum with 72
undamped voices at the widest spread of Q.

Four things had to be right, and each was wrong first:

**It must reach the tone bar, not just the tine.** Coupling into the tine input
alone excited modes at 7.1x the note and above, so a sympathetically ringing
note had no pitch at all -- the fundamental comes from the tone bar. The frame
drives the whole assembly.

**A voice must not hear itself.** The bus is the sum of all voices, so
subtracting nothing meant every voice fed its own output back one sample late,
which merely alters its own decay depending on the phase of the round trip. It
measured as the held notes getting *quieter* the more coupling was applied.

**It has to be an average, not a sum.** With a sum the loop gain grew with the
number of held notes: fine on a chord, and with a pedalled seventy notes the
instrument became a self-sustaining drone that never decayed, then diverged
outright. Dividing by the number of listening tines makes the control mean the
same thing whether two notes are held or seventy. Stable now at twice the
control's maximum with 68 voices and the widest spread of Q.

**Undamped voices cannot retire on level alone.** They were being retired as
inaudible before the note meant to excite them was struck, so the demonstration
was impossible by construction. They are kept alive only while coupling is
switched on, since otherwise this would pin every played voice for as long as
the pedal is down for nothing.

#### The crash it exposed

Driving the coupling past stability produced a **segfault**, not a NaN.
`PickupShaper::process` tested `x <= -1` and `x >= 1`, and both are false for
NaN, so a NaN reached `int(pos)` -- undefined, and in practice an index far
outside the table. Written as negations now, so a NaN takes the first branch.

That bug was reachable by anything that could put a NaN in the audio path, and
would have taken the host down rather than making a bad sound. It had nothing
to do with sympathetic resonance beyond being found by it.

#### The scale was ten times too high, and the test could not see it

Each voice is driven by the **average of the others**, which was meant to make
the control mean the same thing whether two notes are held or seventy. It does
the opposite for stability: with two coupled voices the divisor is one, so each
hears the other at full scale, while with sixty-eight it is divided by
sixty-seven. **The loop is tightest when the fewest notes are down.**

The stability check exercised sixty-eight voices. It passed at a setting where
two held notes grew to **187% of their own peak**, and a note held through a
short phrase never decayed at all -- which is how this was actually found, from
a MIDI file where a held F stopped fading.

| held notes | level after 3 s, coupling at maximum | uncoupled |
| --- | --- | --- |
| 2 | 187% | 22% |
| 3 | 160% | 25% |
| 4 | 104% | 22% |
| 16 | 42% | 20% |

`kCouplingScale` is now **0.05**, set by the two-note case, and the checks
cover two, three and four held notes against the same passage uncoupled.

That also means the effect is smaller than previously claimed. The 24.6 dB
reported earlier was measured on four voices -- squarely inside the
regenerative regime -- and was mostly feedback rather than sympathy. Honestly
measured, with the struck note damped: **+8 dB at maximum**, +3.7 dB at the
0.35 default. Which is about right for tines coupled through a frame rather
than strings over a soundboard.

#### Cost

Coupling keeps undamped voices alive, so a pedalled passage accumulates voices
up to the polyphony limit -- about 13% of a core at the default 32, and 25% at
64. Setting `Sympathetic` to 0 restores the old retire-on-silence behaviour
exactly.

### 2.3 Variation — **done**, and it turned out to be two things

The roadmap asked for randomness at note-on. The sample measurements say that
is only half of it, and the more interesting half is not random at all.

**Key variation** (`Key Variation`, default 0.35, in Tine). Fixed per key, from
a hash of the note number -- so a note sounds like *itself* every time, which
is what an instrument does. Q, tuning (±7 cents) and level (±2.5 dB) each get
an independent draw. The justification is measured: Q in the reference library
scatters between about 900 and 3600 **with no pattern in pitch** (r² 0.02, see
`docs/measurements/`). That scatter was the leftover from 1.3 that would not
fit a curve, and this is what it was.

**Strike variation** (`Strike Variation`, default 0.30, in Hammer). Random per
note-on: contact time, amplitude, and the delay before the strike lands. Plus a
low-probability "unexpected" draw, three times the depth -- a player does not
produce a smooth distribution of attacks, and it is the outliers that stop a
passage sounding sequenced. At the default it spans about 0.95 dB of level and
0.9 dB of brightness; at 1.0, about 3.4 dB of each.

Both are **exactly** off at 0, which is asserted as bit-identical output, and
the engine is seeded in `prepare()` so a render repeats sample for sample.
Variation that cannot be reproduced would make every other test in the suite
worthless.

#### The bug it exposed

Jittering the strike delay made notes drop out entirely -- silent, roughly one
in four at high settings. The gate that hides the filter clear on a fresh voice
reopened at a fixed 2 ms, while the strike now arrived at a jittered time, and
a negative draw put the whole excitation -- about ten samples of it -- in front
of the gate, where `mix * gate` swallowed it.

The gate now reopens exactly when the strike lands. That is what it should
always have done: the mute exists to cover the state clear until the note
starts, so tying it to a constant rather than to the strike was only ever right
by coincidence.

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

## Part 4 — Playing it

Neither of these is in the model at all yet, and both are things a player
reaches for rather than refinements of the physics.

### 4.1 Pitch bend

MIDI pitch bend is currently ignored: `handleMidi` looks at note on/off, CC64,
CC120 and CC123 and nothing else.

The tuning already goes through `noteToFrequency`, so the value itself is easy.
What is not free is that **bend has to move a note that is already sounding**,
and every resonator's coefficients are derived at note-on. Retuning means
re-deriving them per voice as the wheel moves, which is the same path
`configureAll` takes for a parameter change -- so it needs to be smooth enough
to sweep. Worth checking what that costs at 32 voices before assuming it is
free, and worth checking it does not click: changing a high-Q resonator's
frequency while it rings is exactly the kind of edit that steps the output.

Range should be a parameter (±2 semitones conventionally).

### 4.2 Vibrato

Pitch modulation, as distinct from the tremolo already present -- that is
amplitude. Same mechanism as pitch bend and best built on it: an LFO feeding
the same per-voice retune path, with rate, depth, and delay-to-onset.

Two things worth getting right, since a Rhodes is not a synth:

* **Per-voice, not global phase.** A single LFO applied to every voice makes a
  chord move as one object, which is the giveaway of a synthesiser. Real
  instruments do not do this.
* **It should interact with the pickup.** The pickup's output depends on where
  the tine sits relative to the pole, so real vibrato on an electric piano is
  not purely a pitch effect -- it also moves the geometry. Whether to model
  that or leave vibrato purely in the pitch domain is a question for when 4.1
  exists.

Neither is on the critical path for the physics, but both are needed before
anyone would call this playable.

### 4.3 Inline help on hover — **done**

37 controls, and several of them mean nothing without the physics behind them.
`Pickup Offset` is the Rhodes-to-Wurlitzer axis and reads as an abstract
number; `Decay Tracking` is in octaves of Q per octave of pitch; `Bass Tilt`
exists to oppose a differentiator. None of that is guessable from a knob.

**What it should say.** For each control, one line of what it is and one of
what it does to the sound -- not the units, which the value box already shows.
"Where the tine sits relative to the pickup's magnetic axis. At 0 the response
is purely even and the fundamental disappears; higher is cleaner." The material
largely exists already, spread across `MODEL-NOTES.md` and the comments in
`Parameters.h`, and the parameter table is the obvious place to keep it since
it is already the single source of truth for name, range and default.

**Where it should appear.** A fixed strip along the bottom of the panel rather
than a floating tooltip: it can hold two lines comfortably, it does not
obscure the control being read, and it does not require hovering to be
discovered. JUCE's `MouseListener` on each `ParamControl` is enough; there is
no need for `TooltipWindow` and its timing behaviour.

Built as described: a fixed strip along the bottom, fed by a `help` field in
the parameter table so a control's explanation lives beside its range and
default and cannot drift from them. All 44 carry one, which is asserted.

### 4.4 Mono / stereo tremolo — **done**

`Stereo` in the Tremolo section, on by default. Stereo swings the channels in
antiphase, which is what a suitcase does -- it pans between two amplifiers
rather than simply ducking, and that movement is most of what the effect is
*for*. Off moves both together, the mono behaviour.

Measured: one channel swings 10.1 dB while their **sum** swings only 4.4 dB,
which is the test that it is really antiphase rather than merely two different
signals. With the tremolo off, or in mono, the channels are bit-identical.

**This built the stereo path.** The instrument was mono throughout --
`processBlock` rendered once and copied to both channels -- so this is the
first feature that needed two. The voices are still mono, and correctly so: one
tine, one pickup. The channels separate at the tremolo and everything after it
is per channel, because the DC blocker is stateful and the limiter is
nonlinear, and sharing either would couple the two sides back together.

`Engine::render(p, left, right)` is the entry point now; `process()` remains as
a mono convenience for the offline probes in `tests/`. **2.2** (sympathetic
resonance) and **4.2** (vibrato) both wanted this to exist and can now use it.

### 4.5 Scale Workshop / .scl tunings — **done**

The tuning section already generalises beyond 12-tone equal temperament --
`Divisions` and `Interval` give any equal division of any interval, which is
how the Pd original did it and is more than most instruments offer. What it
cannot do is an *unequal* scale: a table of arbitrary ratios or cents per
degree, which is what Scale Workshop (sevish.com/scaleworkshop) exports.

**Formats.** `.scl` (Scala) is the lingua franca and is trivial to parse --
a line count then one ratio or cents value per degree. `.kbm` maps degrees to
MIDI keys and matters as soon as a scale is not 12 notes. Scale Workshop
exports both.

**Where it lands.** `Engine::noteToFrequency` is the only place pitch is
decided, so the model does not care. The work is a tuning table on the
processor, file loading in the editor, and deciding what travels with the
session -- the file path is fragile, so the table itself should be in the
state.

**What shipped.** `dsp/Scale.h` parses .scl and holds a table of cents;
`Engine::noteToFrequency` uses it when one is loaded and the equal divisions
when it is not. Fourteen scales are built in, generated by `tools/scales.py`
from their definitions rather than copied from a table of cents -- the
tempered ones are computed by narrowing named fifths by named commas, so the
numbers can be checked against the reasoning. The chooser is in the header,
not the Tuning section: it is not a parameter, and a fifth control in Tuning
would take that section to two rows and put every column off the row grid.

**Still open.** `.kbm` is not read, so degrees map straight up from `Base MIDI
Note` -- fine for 12-note scales, and for a 19- or 31-note one it means the
keyboard covers less than an octave per octave of keys, which is what a
generalised keyboard would fix and a piano keyboard cannot. A scale that does
not ascend is refused rather than played.

### 4.6 The routing: what the pickup actually carries — **attempted, mostly rejected**

`MODEL.md` calls the direct `Tone Direct` and `Tine Direct` paths unphysical,
and they are: a real Rhodes is heard only through its pickup. The rework was to
remove them, feed the tine and tone bar into the pickup properly, and re-stage
the drive. It was measured at every step and it did not work.

| | keyboard spread | tine at note 45 |
| --- | --- | --- |
| as it was | 13.1 dB | −25.9 dB |
| pickup only | 15.1 dB | −19.7 dB |
| pickup only, no `tanh` bound | 33 dB | **+9.4 dB at note 21** |

The direct paths were doing real work: they carry the fundamental *without*
passing through the differentiator, and that is what keeps the keyboard even.
Removing them leaves the pickup's inherent +6 dB/octave with nothing opposing
it, and `Bass Tilt` cannot make up the difference -- driving the pickup harder
in the bass only pushes it further into saturation, so the compensation cancels
itself.

Removing the `tanh` bound as well, on the reasoning that the flux curve is its
own limit, was worse again: the bass ran out into the tail of the curve where
flux barely changes, so the fundamental collapsed while the harmonics grew and
at the bottom of the keyboard the tine came out *above* the note it belonged
to.

**Kept from the attempt:** the tine now enters the pickup *before* the drive
stage rather than after it. That is the right structure -- the drive means one
thing rather than two -- and it fixes the non-monotonic behaviour where raising
`Tine to Pickup` past a point gave *less* tine, because the tine was pushing
the flux curve into saturation on its own.

Its default was moved to −20 dB at the same time, on the reasoning that this
is a level the pickup can actually carry, and **that part was reverted**: −20
sounded worse, and −77 dB is what ships. The structural change stands on its
own; the level did not.

**Since then:** `Tone to Pickup` exists (0 dB by default, which is what it was
hard-wired at), so both of the tone bar's paths are controls, and the whole
experiment above is now reachable from the panel -- `Tone Direct` and `Tine
Direct` to −100, sends up -- without a rebuild. That makes the conclusion
cheap to re-test rather than cheap to forget. It does not change the
conclusion.

**What would need to change for the rest.** The differentiator has to be
opposed by something that is not a bigger excursion, since a bigger excursion
saturates. That means either compensating after the pickup rather than before
it -- which is honest as a tone control but not as physics -- or modelling the
tine's displacement in real units so that the geometry, the excursion and the
drive are all calibrated against each other rather than tuned independently.
The second is the real answer and it is a larger piece of work than this was.

## Deliberately not doing yet

* **CLAP** — wanted, and MK1 will get it first; nothing here depends on it.
* **Fitting the model to samples automatically** — parameter optimisation only
  makes sense once the structure is right. Fitting a model that lacks the 39.7
  mode just distorts the parameters it does have to compensate.
* **Preset system beyond the factory presets** — MK2 inherits Rhodes
  MkI/Bright, Wurlitzer, Clav, Vibraphone and Kalimba from MK1. Morphing
  between them, and user preset management, are still to do. Worth
  doing after §1.4, because the pickup change moves the main morph axis.
