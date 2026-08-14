# What changed from EP-MK1

EP-MK1 is a Pure Data physical model by Miguel Moreno, and the JUCE port of it
that preceded this reached parity with the patch — bands agreeing within
0.1–0.2 dB. MK2 is not a bug-fix release of that. Almost every stage of the
model has been replaced, mostly because the old one could not express what the
measurements say.

Ordered by how much it changes the sound.

---

## The excitation is a hammer, not a period

**MK1** excited the resonators with a single raised-cosine cycle *at the note's
own period* — 9 ms at A2, 36 ms at A0. That spectrum collapses above the
fundamental, so the tine modes were driven 65–71 dB below it and arrived 40–75
dB down in the output. The instrument's brightness therefore came from the
pickup's waveshaper rather than from the tine, which is backwards.

**MK2** uses a raised-cosine force pulse whose width is the hammer's **contact
time in milliseconds** (`Contact Time`, 0.4 ms), independent of pitch, with
velocity shortening contact as well as raising force (`Vel to Contact`). That
is where "harder is brighter" physically comes from.

Amplitude is normalised on **impulse**, not peak: a hammer imparts momentum, so
shorter contact means higher peak force for the same strike, and the contact
control changes timbre rather than volume.

At A2 the tine modes went from −40, −65 and −75 dB relative to the fundamental
to **−22.7, −32 and −41**.

## The resonators were rebuilt twice

**MK1** used the Pd patch's RBJ band-pass with *constant skirt gain*, whose gain
at resonance is Q. The tone bar therefore ran at +64 dB and the tine modes at
+47 dB, and **every resonator's level was set by its decay time**. That survived
only because the old excitation had almost no energy at the mode frequencies; a
real broadband strike clips it instantly.

**MK2** uses a two-pole resonator with no zeros, `b = (0, sin(w0), 0)`:

* **No direct feedthrough.** With `b0 = 1` the output contained a copy of the
  hammer pulse — a sub-millisecond hump at full level, independent of any
  resonance. That is a click by construction, and it was loudest, relative to
  the note, at quiet velocities.
* **−12 dB/octave above resonance**, where a band-pass gives −6, so the
  excitation's out-of-band content is rejected rather than leaking through.
* **Ringing amplitude independent of pitch and Q** — measured 0.996 to 1.001
  across 30 Hz to 4 kHz and Q 225 to 1642.

That last property is what makes a keyboard-varying Q possible at all.

## The pickup is geometry, and it differentiates

**MK1** had `pickup_gain` and `pickup_symmetry`, the latter an exponent in a
tabulated curve. Asymmetry was dialled in by hand.

**MK2** models the pickup from **distance** and **offset** — where the tine sits
relative to the pole piece and its magnetic axis — with flux following a dipole
falloff. Two consequences:

* **Offset is the asymmetry**, and the strongest Rhodes↔Wurlitzer control in
  the instrument. At offset 0 the tine sits on the magnetic axis, the response
  is purely even, and the fundamental collapses under a 2nd harmonic 9 dB above
  it. A Wurlitzer's bark is even harmonics, so it wants a *low* offset.
* **The coil senses dΦ/dt**, which is a differentiator and an inherent
  +6 dB/octave. The coil's own inductance (`Coil Low-Pass`) then rolls the top
  off, and the two together make the broad band-pass a real pickup has.

`Pickup Attack`, which fed the hammer blow straight into the pickup, is off by
default. No real pickup sees the hammer — it senses the tine — and once the
signal is differentiated, a sub-millisecond pulse through that path is audibly
a click.

## New structure

* **A third tine mode** at 39.7×, with per-mode levels and a damping control,
  and **Nyquist culling**: a mode past `0.45 × SR` is *skipped*, not clamped.
  MK1's `designBandpass` clipped to 20 kHz, which parked a Q-225 resonator just
  under Nyquist wherever a mode should have ceased to exist — the cause of its
  4 dB error at the top of the keyboard.
* **A sub-fundamental** below the note, from the tone bar. Measured in the
  reference library at 0.42–0.60 × f0 and absent in the treble; the literature
  expected 0.58–0.83.
* **Q varies across the keyboard** — `Tone Decay` is now Q at A4, with
  `Decay Tracking` in octaves of Q per octave of pitch.
* **Bass Tilt**, opposing the pickup's differentiator. A hammer imparts
  momentum and the resulting displacement goes as 1/ω, so a bass tine swings
  much further for the same blow. Without it the keyboard tilted 9.3 dB against
  the bass.

## No two notes alike

MK1 played every note identically every time. MK2 has two different things:

* **Key Variation** — fixed per key, from a hash of the note number, so a note
  sounds like *itself* every time. Q, tuning and level each get an independent
  draw. Measured justification: Q in the reference library scatters between
  about 900 and 3600 **with no pattern in pitch**.
* **Strike Variation** — random per note-on: contact time, force and timing,
  with a low-probability draw at three times the depth. A player does not
  produce a smooth distribution of attacks, and it is the outliers that stop a
  passage sounding sequenced.

Both are exactly off at zero — asserted as bit-identical output — and the
engine is seeded so a render repeats sample for sample.

**Restriking** also changed. MK1 sent a repeated pitch to a *fresh* voice and
wiped the old one's filter state, because restriking produced a click. That
click came from the retrigger mute, not from the physics. A repeat now returns
to the same voice with its resonators still moving — a Rhodes has one tine per
note — so whether the hammer arrives with or against the tine's motion falls
out for free. Two repeats at different gaps measure 17.9% apart with no
randomness anywhere.

## Sympathetic resonance

New. Every tine whose damper is off is driven by the average of what the others
are doing, through the frame they share. Hold a chord silently, strike a low
note, and the held pitches come up 12.2 dB.

## Stereo

MK1 was mono throughout — one render copied to both channels. MK2 keeps the
voices mono (one tine, one pickup) and separates the channels at the tremolo,
which now has a **stereo mode** swinging them in antiphase, as a suitcase does
by panning between two amplifiers. Everything downstream of that is per channel.

## Bugs fixed that MK1 also had

* **MIDI CC64 never worked.** `handleMidi` set the pedal state, but the
  parameter table's `sustain` toggle overwrote it at the top of every block, so
  a pedal held by CC64 survived about 10 ms. The panel toggle also never
  released held notes when switched off.
* **The level depended on the sample rate** — 4.6 dB louder at 96 kHz than at
  48, and 9 dB at 192. The resonators sum their input sample by sample while
  the strike is defined in seconds, so a higher rate drove them harder. The
  excitation is a density now; level varies 0.02 dB across 44.1 to 192 kHz.
* **A NaN in the audio path segfaulted the host.** `PickupShaper::process`
  tested `x <= -1` and `x >= 1`, both false for NaN, so it reached `int(pos)`
  and indexed far outside its table.

## Interface

* 44 controls, grouped by physical part and ordered **level, then tone, then
  time** within each group, in signal-flow order with tuning last.
* **An info bar**: every control carries a line of what it is and a line of
  what it does to the sound, shown on hover.
* The panel scales with the window, remembers its size per user *and* per
  session, and survives a layout change between versions.
* Polyphony reaches 128. Cost is 0.41–0.46% of a core per sounding voice; a
  full 88-key keyboard held down is 36.6%.

## What is not carried over

MK1's Pd/hvcc lineage. MK2 has no parity target: `tests/parity/` remains from
the port but the model it compared against is no longer what this is. The
factory presets were rebuilt, since several of the controls they most want —
contact time above all — did not exist when they were written.
