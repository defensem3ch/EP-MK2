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
brightness   +389 Hz,      spread 452
tine level   +8.9 dB,      spread 6.4
```

**Decay is right.** +0.4 dB of Q on average across 18 notes is as close as this
measurement can resolve. The spread of 3.5 dB is larger than the mean, which
says the same thing the library said on its own: decay scatters from note to
note and does not follow pitch. The model does that too, by design.

**The tine is about 9 dB too loud.** The strongest inharmonic partial in the
attack sits ~9 dB higher in the model than in the instrument, consistently
enough (spread 6.4) to be worth acting on. This agrees with a judgement made
by ear before the benchmark existed -- the tine level was dropped 6 dB on
listening alone, and the measurement now says roughly the same again.

**Brightness diverges towards the treble, and that is the real finding.** The
average of +389 Hz hides the shape: at note 40 the model is *darker* than the
instrument by 239 Hz, and by note 100 it is brighter by 1465 Hz. The spread
(452) being larger than the mean is the tell. The model's spectral balance
tilts the wrong way across the keyboard -- it is too dark in the bass and much
too bright at the top -- which is a different fault from being wrong by a
constant, and not one a level control fixes.

## What this does not measure

* **One velocity layer.** The library has twelve; this compares the loudest.
  How velocity moves brightness is the obvious next comparison and the model
  has an explicit control for it (`Vel to Contact`).
* **One instrument**, and a sampled one. Its decay may carry the sampler's
  envelope as much as the tine's.
* **Nothing about the attack's shape in time** -- only what the first 120 ms
  contains, not how it arrives.
