# How the model works

A Rhodes makes its sound like this: a hammer strikes a steel **tine**; the tine
is clamped beside a brass **tone bar** that resonates with it; and a magnetic
**pickup** facing the tine's tip turns its motion into a voltage. Everything
here is an attempt to model those four things and nothing else — the controls
are named after parts, and where a control does something unphysical it says so.

This document describes what the code actually does, in signal order. Where a
number came from a measurement, the measurement is named;
`docs/measurements/` holds the analysis of the reference library and
`docs/ROADMAP.md` records what is still guesswork.

---

## 1. Voice allocation

`Engine::noteOn` picks a voice in this order:

1. **A voice already playing this pitch.** A Rhodes has one tine per note, so
   striking a key again is the *same tine* struck while it is still moving.
   That voice is told this is a **restrike**: it keeps its resonator state, and
   the new excitation sums into a tine already in motion. Whether the hammer
   arrives with or against that motion changes the attack, and it falls out of
   the physics without any randomness. Two repeats at different gaps measure
   17.9% apart.
2. A free voice.
3. The quietest voice whose key is up.
4. The quietest voice, held or not.

Cases 2–4 are a *different* note taking the voice over, so its resonators are
tuned to the wrong frequency. Those mute the voice for 1 ms, clear the filter
state, and reopen the gate exactly when the strike lands. The gate must follow
the strike rather than a fixed delay: strike timing is jittered by variation,
and a negative draw used to put the whole excitation in front of the gate,
where it was multiplied by zero and the note came out silent.

Voices retire when their peak follower falls below −80 dBFS, except while
sympathetic coupling can still drive them.

## 2. Pitch

```
f0 = interval ^ ((note − baseNote) / divisions) × baseFreq
```

The Pd original's tuning, kept because it generalises: any equal division of
any interval, not just 12 per octave. Unequal scales are not supported yet
(roadmap 4.5).

Two per-key offsets are applied on top, both derived from a hash of the note
number so they are fixed for a given key rather than random:

* **Detune**, up to ±7 cents × `Key Variation`.
* **Level**, up to ±2.5 dB × `Key Variation`.

## 3. The hammer

A single raised-cosine pulse, delayed 2 ms behind the note-on:

```
strike(t) = cos(2π(0.75 + 0.5·ramp))     ramp: 1 → 0 over the contact time
```

**Width is contact time in milliseconds, not the note's period.** This is the
single most consequential difference from MK1, which used one cycle at f0 — 9 ms
at A2, 36 ms at A0 — whose spectrum collapses above the fundamental and left the
tine modes 40–75 dB down in the output. A short contact is broadband and drives
the inharmonic modes; a long one is soft and pure. Velocity shortens contact as
well as raising force, which is where "harder is brighter" comes from.

**Amplitude is normalised on impulse, not peak.** A hammer imparts momentum, so
halving the contact time doubles the peak force for the same strike:

```
strikeAmp = velocityAmp × strikeVar × keyLevel × rateScale
          × (1/220 s) / contactTime
          × (440 / f0) ^ bassTilt
```

* `velocityAmp = 2^(−5(1 − (v−1)/126))` — a five-octave velocity range, from
  the Pd original.
* `rateScale = 48000 / sampleRate`. The resonators sum their input sample by
  sample while the strike is defined in seconds, so without this a higher rate
  puts more samples under the same pulse and drives them harder — the
  instrument was 4.6 dB louder at 96 kHz than at 48.
* `bassTilt` compensates for the pickup's differentiator (§6). A hammer imparts
  momentum and the resulting displacement goes as 1/ω, so a bass tine swings
  much further for the same blow. The bare law, exponent 1.0, is what the
  benchmark against the reference instrument settles on. The default is
  **0.69**, a voicing choice rather than a measurement: the full law is
  correct and heavier in the bottom octave than the instrument is nice to
  play. 0 removes the compensation entirely.

**Strike Variation** draws contact time, amplitude and delay fresh for every
note-on, with a low-probability draw at three times the depth — a player does
not produce a smooth distribution of attacks, and the outliers are what stop a
passage sounding sequenced. The engine is seeded in `prepare()`, so a render
repeats exactly.

At note-off a second, much softer pulse fires: the damper landing on the tine
(`Damper Thump`). It keeps the old period-width shape, because that contact
really is slow.

## 4. Resonators

Every resonator is a two-pole filter with no zeros and a one-sample delay:

```
H(z) = sin(w0)·z⁻¹ / (1 + a₁z⁻¹ + a₂z⁻²)
```

with the usual RBJ denominator (`alpha = sin(w0)/2Q`). Three properties matter,
and the band-pass this replaced had none of them:

* **No direct feedthrough** (`h[0] = 0`). A resonator's displacement cannot
  instantaneously follow the force applied to it. With `b0 ≠ 0` the output
  contained a copy of the hammer pulse — a sub-millisecond hump at full level,
  independent of any resonance, which is a click by construction and worst at
  quiet velocities where it did not scale down with the note.
* **−12 dB/octave above resonance**, where a band-pass gives only −6. The
  excitation's out-of-band content is rejected rather than leaking through, and
  the pickup's differentiator then brings the total to −6, which is what a
  magnetic pickup on a struck tine actually does.
* **Ringing amplitude independent of pitch and Q.** The `sin(w0)` numerator is
  what does this: an all-pole resonator rings at roughly `b1/sin(w0)`, so a
  bare `b1 = 1` is 1/f² across the keyboard. Measured 0.996 to 1.001 across
  30 Hz–4 kHz and Q 225–1642.

That last property is why `Tone Decay` and `Tone Level` are independent
controls, and it is what makes a keyboard-varying Q possible at all.

### The tone bar — the note's pitch

One resonator at f0. Its Q is `Tone Decay`, which is **Q at A4**, scaled by:

* `Decay Tracking` — octaves of Q per octave of pitch. Nearly flat by default
  (0.056): 18 usable decay fits from the reference library give a least-squares
  slope of +0.056 with an r² of **0.02**, which is to say the instrument shows
  no reliable trend of decay against pitch. The value the model used before
  (+0.217) came from a range that could not be verified against any paper.
* `Key Variation` — up to ±1 octave of Q, fixed per key. This is where the
  library's Q scatter went: 900 to 3600 with no pattern in pitch is not noise
  in the measurement, it is tines being individually cut and clamped.

At note-off the tone bar switches to `Tone Release`, a much lower Q.

### The sub-fundamental

A second resonator at `Sub Ratio × f0`, driven by the same excitation, because
it is the same piece of metal. Measured in the reference library at
**0.42–0.60 × f0** from note 28 to 70 and absent above; the literature expected
0.58–0.83. Skipped below 20 Hz, where `designBandpass` would clamp it up and
park a resonator at a frequency the note does not have.

### The tine — the inharmonic modes

Three resonators at `Mode 1/2/3 Ratio × f0`, measured on a real Rhodes at
**7.1, 20.4 and 39.7** (Gabrielli et al., JASA 148(5) 2020). They share `Tine
Decay`, optionally scaled per mode by `Mode Damping`, since higher modes damp
faster.

A mode whose frequency passes `0.45 × SR` is **skipped, not clamped**. This
matters: clamping parks a high-Q resonator just under Nyquist wherever a mode
should have ceased to exist, and was the cause of MK1's 4 dB error at the top
of the keyboard. Mode 2 passes Nyquist around C6, mode 3 around D5, so the
top of the keyboard has fewer modes than the bottom — as the real instrument
does.

The tine is fed through a high-pass (`Tine High-Pass`) and the voice's gate.

## 5. Sympathetic resonance

Tines share a frame, so an undamped tine is driven by the others. Each voice
receives the **average of every other coupled voice's output**, delayed one
sample:

```
drive = sympathetic × 0.1 × (bus − thisVoice) / (coupledVoices − 1)
```

Three details are load-bearing:

* **It reaches the tone bar, not just the tine.** Coupling into the tine alone
  excites modes at 7.1× and above, so a sympathetically ringing note has no
  pitch.
* **A voice must not hear itself.** Feeding a voice its own output back one
  sample late merely alters its own decay depending on the phase of the round
  trip, which measured as the held notes getting *quieter* with more coupling.
* **It is an average, not a sum.** These resonators have a gain of Q at their
  own frequency — into the thousands — so with a sum the loop gain grew with
  the number of held notes: fine on a chord, a self-sustaining drone on a
  pedalled seventy. Averaging makes the control mean the same thing regardless.

Only voices whose damper is off are coupled, and they are kept alive rather
than retiring on level, or they would be gone before the note meant to excite
them was struck.

## 6. The pickup

A magnet and coil facing the tine. Its input is

```
pickupIn = (strike·hammerToPickup + toneBar + sub) · pickupDrive
         + tine · tineToPickup
```

and then, in order:

**Displacement.** `tanh(pickupIn)` — the tine's excursion is bounded, and it
cannot pass through the pickup. `std::tanh` rather than the Padé approximant
used elsewhere, because this feeds a differentiator.

**Flux.** A tabulated dipole falloff:

```
Φ(x) = 1 / (distance² + (offset + x)²)^1.5
```

normalised so its swing is unity — moving the pickup changes the *shape* of the
response, not the level. `Pickup Offset` is where the asymmetry lives, and is
the strongest Rhodes↔Wurlitzer control in the instrument. At offset 0 the tine
sits on the magnetic axis, the response is purely even, and the fundamental
collapses under a 2nd harmonic **9 dB above it** — which is a good check that
the geometry is right rather than merely plausible. A Wurlitzer's bark is even
harmonics, so it wants a *low* offset.

The table is shared by every voice. Reading it is guarded against NaN by
negated comparisons: `x <= −1` and `x >= 1` are both false for NaN, which used
to reach `int(pos)` and index far outside the table — a segfault in the host
rather than a wrong sample.

**Induced voltage.** The coil senses the rate of change of flux, so the output
is the difference between this sample's flux and last sample's, both evaluated
on the *current* table. Evaluating the previous sample from a stored value
would also differentiate any change to the geometry, so moving the pickup while
a note sounded stepped the output by the difference between two geometries —
audibly a click. The gain normalises the differentiator to unity at A4.

This differentiator is a real +6 dB/octave, and is the reason `Bass Tilt` (§3)
exists.

**The coil.** `Coil Low-Pass` is the coil's own inductance rolling the top off.
It belongs *after* the differentiator: together they make the broad band-pass a
real pickup has, where filtering the input instead leaves the +6 dB/octave
running unopposed and the top of the keyboard drives the limiter.

**Buzz.** A fourth-power term on the loud half of the waveform, with `Buzz
Phase` choosing which half. From the Pd original; it is the one part of the
pickup that is a tone control rather than a model.

**Body high-pass**, at f0.

## 7. Voice output and the engine

```
mix = strike·hammerLevel + pickupOut
    + (toneBar + sub)·toneLevel + tine·tineLevel
```

then the voice's gate, then a keytracked low-pass kept from the Pd original —
measured to be doing nothing audible at the shipped settings, and retained
because removing it has not been tested rather than because it earns its place.

The direct `toneLevel` and `tineLevel` paths are not physical: a real Rhodes is
heard only through its pickup. They are how the Pd model got a fundamental when
its pickup path was too weak to supply one, and they remain because **the
keyboard's evenness depends on them** -- they carry the fundamental without
passing through the pickup's differentiator, which is the only thing opposing
its +6 dB/octave.

Removing them was tried and measured, and made the instrument worse: the spread
across the keyboard went from 13.1 dB to 15.1, and to 33 dB with the pickup's
input bound removed as well. See roadmap 4.6 for what would have to change
first.

The engine sums the voices, halves, applies master, and then splits into two
channels at the tremolo — the only stereo stage. Everything after it is per
channel, since the DC blocker is stateful and the limiter is nonlinear:

```
tanh(highpass₅Hz(sum × tremoloGain))
```

`Stereo` swings the channels in antiphase, as a suitcase does by panning
between two amplifiers.

---

## What the model still cannot do

* **The pickup carries too little of the tine.** See §7.
* **Only the fundamental's Q varies per key**; the tine modes do not.
* **No unequal tunings** (roadmap 4.5), no pitch bend or vibrato (4.1, 4.2).
* **The sub-fundamental does not fade out in the treble**, though the
  measurements say the real one does. Rolling it off would mean inventing a
  curve from an absence.
* **Nothing is oversampled**, and measurement says nothing needs to be: the
  worst-case alias sits 77 dB below the fundamental. See roadmap 1.5.
