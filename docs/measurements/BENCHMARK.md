# The model against the instrument

Both sides measured by the same code, in `tools/analyse_samples.py`, so a
difference in the numbers is a difference in the sound rather than in how it
was measured. Render the model's side with `EPMK2_bank` and compare with
`tools/benchmark.py`.

Reference: Kontakt Factory MkI, 25 pitches, velocity 0x7F, 10-second notes.
Model rendered on the same grid with variation off.

## Where it stands

```
decay        +0.4 dB of Q, spread 3.5   (18 notes measurable)
brightness    -1 Hz,       spread 231
tine level   +0.6 dB,      spread 4.6
```

Two defaults changed to get there, both from the measurement rather than by
ear, and each verified by re-rendering the whole bank and re-running the
comparison.

### Decay was already right

+0.4 dB of Q across 18 notes is as close as this can resolve. The spread being
larger than the mean says what the library said on its own: decay scatters from
note to note and does not follow pitch. The model does that too, by design.

### The tine was 9 dB too loud

`Tine Level` −6 → **−15 dB**. Worth noting it had already been dropped 6 dB on
listening alone, before this harness existed, and the measurement then said
roughly the same again.

### The brightness tilt was the coil, and nothing else

This was the interesting one. The model was ~250 Hz *darker* than the
instrument at the bottom of the keyboard and up to 750 Hz *brighter* through
the middle — a tilt, not an offset, so no level control could fix it.

Correcting the tine did not touch it (+265 → +261 Hz): two independent faults.

What the per-note figures showed is that **the instrument's brightness barely
moves across most of the keyboard** — a centroid of roughly 600–900 Hz from
note 31 to note 79 — while the model's tracked the fundamental all the way up.
The instrument has a fixed spectral centre and the model did not, because
almost everything in the model is defined relative to f0. The one thing that
is not is the coil's low-pass, and it sat at 2 kHz, well above where the
instrument's centre actually is.

`Coil Low-Pass` 2000 → **900 Hz**, and the gap closes to −1 Hz with the spread
falling from 345 to 231.

A 900 Hz corner is low for a pickup coil alone, and it should be read as the
whole chain the reference was captured through — coil, cable, the instrument's
own passive tone control, and whatever Kontakt's sampling added — rather than
as a claim about the inductance.

## Still open: velocity

The model's dynamic range is much too narrow. Soft to hard, the instrument
spans 33 to 53 dB depending on pitch; the model spans 25 to 32.

```
level range over velocity   +15.1 dB (model has less)
```

`velocityAmp` is `2^(-5(1 - v/127))`, five octaves, and pitch-independent. The
instrument's range also *widens* towards the treble — 33 dB at note 31, 53 dB
at note 91 — which the model has no mechanism for at all.

Brightness against velocity is not usable from this capture: the quiet layers
sit near the noise floor, and the instrument's measured centroid falls with
velocity at several pitches, which is not a thing an instrument does.

## What this does not measure

* **Brightness against velocity**, for the reason above: the reference's quiet
  layers are too close to its noise floor to measure.
* **One instrument**, and a sampled one. Its decay may carry the sampler's
  envelope as much as the tine's.
* **Nothing about the attack's shape in time** -- only what the first 120 ms
  contains, not how it arrives.
