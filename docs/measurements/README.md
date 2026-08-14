# Measurements

Analysis output from `tools/analyse_samples.py`, run against the reference
sample library. The samples themselves are not in the repository -- they are
commercial content and are gitignored -- but measurements derived from them are
ours and belong here.

## kontakt-mki.json

Kontakt Factory MkI, **25 pitches (MIDI 28-100, every 3 semitones) x 12
velocity layers**, 48 kHz, notes held for up to 10 s.

One sample failed to capture -- note 61 at velocity 0x4A came back at 0.38 s
against a 10.09 s median for that pitch -- and is excluded automatically.
`validate()` compares each sample against **its own pitch's** median rather
than a global one, because the top octave is legitimately short: notes 97 and
100 decay to nothing inside a second and Renoise trims the silence. A global
length test would have condemned the whole treble.

## What it says

### Q of the fundamental: no trend, and a lot of scatter

18 usable fits, note 49 to note 100. Least squares over all of them:

```
tracking  +0.056 octaves of Q per octave of pitch   (r2 = 0.02)
Q at A4    1751
```

**An r-squared of 0.02 means there is no reliable trend of Q against pitch in
this instrument.** Q wanders between about 900 and 3600 with no pattern. Two
earlier readings were wrong and are worth recording as such:

* The `+0.217` the model used came from a 731-2175 range attributed to Shear
  (UCSB 2011, Table 2.1), which is not verifiable against any paper on hand.
  Not supported by measurement.
* An earlier `-0.108` from this same tool used only the first and last usable
  points. Two samples of a scattered quantity are not a slope.

The model now uses the measured 1750 at A4 and a nearly flat +0.056.

Fitting the decay also needed bounding to the clean exponential region. Several
notes reported 90 dB of "decay", which was the fit running into the noise floor
and flattening -- reading as a far higher Q than the instrument has. The fit now
stops 50 dB below where the partial starts.

**The scatter is probably the real finding.** Tines are individually clamped
and individually variable, so per-note variation in Q is physically plausible,
and it belongs with `ROADMAP` 2.3 (a note that is not identical every time)
rather than as a curve across the keyboard.

### Sub-fundamental: real, and below what the papers said

Present from note 28 to 70 at **0.42-0.60 x f0**, absent above. The literature
expected 0.58-0.83. Implemented at 0.55 -- see ROADMAP 1.2.

### Inharmonic modes live in the attack

Measured over a whole held note the spectrum is almost purely integer
harmonics, because at Q around 225 a mode at 7 x f0 is 60 dB down within
~120 ms. Over the first 120 ms instead: 6.6x, 7.2x, and a spread of higher
inharmonic peaks. Broadly consistent with the model's 7.1 and 20.4, not a
clean match.
