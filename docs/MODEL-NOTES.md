# Model notes: what each control physically means, and where it can be pushed

> **Some control names here are out of date.** This was written for MK1.
> `pickup_symmetry` no longer exists -- the pickup is modelled from its
> geometry now, and **`pickup_offset` is the axis this document describes as
> symmetry**: at offset 0 the tine sits on the magnetic axis and the response
> is purely even, and asymmetry increases as it moves away. Note the direction
> is reversed from the old control, so a Wurlitzer wants a *low* offset. See
> ROADMAP 1.4. The physical reasoning below is unaffected.

Working notes for the instrument-morphing direction — the idea that this should
do a Rhodes extremely well and then bend into a Wurlitzer, a clav, a
vibraphone, a kalimba, and into things that could not physically exist.

Nothing here is implemented as a switch yet. It is written down because the
values were expensive to work out, and because two of them were discovered by
getting them *wrong* and hearing the result.

## The two bugs that produced a Wurlitzer

Both were unit errors in the port, and both are directly useful as morph axes.

### 1. Pickup symmetry as a raw exponent

Every `snd-` control in the Pd original passes through `[+ 100] → [dbtorms]`
before reaching a voice, so the panel's **7** arrives as `10^(7/20) = 2.239`.
The port used **7** directly.

That value is an *exponent* in the pickup's transfer function:

```
y = (2^(s·x) − 1) / 2^s
```

so feeding 7 instead of 2.239 turns a gentle asymmetry into a severe
waveshaper. The result was described, without any prompting, as *"more
Wurlitzery than Rhodesy"* — which is exactly right. A Wurlitzer's reed and
electrostatic pickup produce far more harmonic bark than a Rhodes tine and
magnetic pickup.

**This is the single strongest Rhodes ↔ Wurlitzer axis in the model.**

| `pickup_symmetry` | linear `s` | character |
| --- | --- | --- |
| 0 dB | 1.0 | almost linear, glassy, closest to a clean tine |
| 7 dB | 2.24 | **Rhodes** (the shipped default) |
| 14 dB | 5.01 | bark, edging toward reed territory |
| 17 dB | 7.08 | roughly what the "wrong" value sounded like |
| 24 dB | 15.85 | extreme; well past any real instrument |

Note the panel range already reaches 24 dB, so the whole axis is reachable
today by turning one knob — a Rhodes/Wurly switch could be little more than a
named preset pair over this plus the tine ratios below.

### 2. Buzz phase inversion

The patch's object is `[*~ -1]`, but its right inlet is driven by the buzz
phase control, which *replaces* the `-1` argument. The port kept the negation
and applied the control on top, inverting the buzz relative to the fundamental.

Inverted buzz changes which half of the waveform gets the extra harmonics, and
so whether the tone reads as "hollow" or "forward". `buzz_phase` is exposed, so
both are available.

## What actually distinguishes the instruments

Collected from the papers and from what the model can currently express.

### Rhodes (what this is)

* Tine = a cylindrical **cantilever** with a tuning spring, coupled to a brass
  tone bar. Inharmonic partials measured at **7.1** and **20.4** times the
  fundamental (σ 0.3 and 0.4), with a fourth at 39.7 and a sub-fundamental at
  0.58–0.83 that the tone bar produces — Gabrielli et al., JASA 148(5) 2020.
* Fundamental Q between **731 and 2175** across the keyboard, Shear (UCSB 2011)
  Table 2.1. The model has a single global `tone_decay`; its default of 1642
  sits inside that range.
* Magnetic pickup, gentle asymmetry.

### Wurlitzer

* Reed is a **flat bar**, not a rod, and there is no tone bar. Different mode
  ratios — a flat cantilever's ideal ratios are 6.27 and 17.55, which is what
  the port wrongly used for the *Rhodes* and which sounded closer to a Wurly.
  Worth testing whether that is coincidence or the real reason.
* Electrostatic pickup, much stronger nonlinearity → `pickup_symmetry` high.
* Shorter sustain → lower `tone_decay`.

### Clav

* Plucked string against a fret: harmonic, not inharmonic. `tine_ratio1/2`
  toward integers (2, 3) and high `tine_level`; very short `tone_decay`;
  strong `pickup_gain`.

### Vibraphone / kalimba

* Vibraphone bars are tuned so the first overtone is exactly **4×** (bars are
  undercut deliberately), against a cantilever's natural 6.27. `tine_ratio1 = 4`
  with a long `tone_decay` and almost no pickup nonlinearity should get close.
* Kalimba tines are short cantilevers with almost no resonator: high
  `tine_level`, minimal `tone_level`, short decay.

### Things that cannot exist

The ratios are free parameters. Non-integer, irrational, or inverted ratios
(`tine_ratio1 > tine_ratio2`) have no physical instrument behind them but are
perfectly well defined here. Likewise a tone bar Q of 2000 with a
`pickup_symmetry` of 24 dB.

## What the model cannot currently express

Worth knowing before designing a preset system around it:

* **One global Q.** The real instrument's Q varies 731→2175 across the
  keyboard. Per-note or pitch-dependent Q would need a curve, not a value.
* **Only two tine modes.** The measurements found four plus a sub-fundamental.
* **The pickup is dB knobs, not geometry.** The papers model it from
  tine–pickup distance and offset; the sidebands around each inharmonic mode
  come out of that geometry. Reproducing those would mean replacing
  `pickup_gain`/`pickup_symmetry` with a physical pair.
* **No oversampling**, so the bottom notes alias slightly when the waveshaper
  is driven hard (measured: +1.6 dB in the 800 Hz–4 kHz band at A0).

## Preset system sketch

When it is time: a preset is a full set of the 29 parameter values, so the
existing value tree already carries everything. What is missing is a) named
factory presets, b) a morph between two of them, which for most of these
parameters is a plain interpolation — except the ratios and Q, where
interpolating in log space will sound more musical than in linear.
