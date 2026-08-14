# Measurements

Analysis output from `tools/analyse_samples.py`, run against the reference
sample library. The samples themselves are not in the repository -- they are
commercial content and are gitignored -- but measurements derived from them are
ours and belong here.

## kontakt-mki.json

Kontakt Factory MkI, 13 pitches (MIDI 28-100, a tritone apart) x 12 velocity
layers, 48 kHz.

### Known limitation of this capture

The samples run about 1 second and **hold for only ~0.5 s**, then fade to the
noise floor in roughly 100 ms. That fade is a note-off, not the instrument
decaying.

It matters more than it sounds. A 41 Hz partial with a Q around 1000 falls less
than a dB in half a second, so **the bass decay is not measurable from this
capture at all** -- and fitting through the fade instead reports a Q of about
30, which is off by a factor of thirty. `find_release()` exists to stop that,
and `trustworthy()` refuses to quote a fit that had less than 3 dB of decay to
work with.

A re-capture holding each note for 20-30 s would make the whole keyboard
measurable.

## What it says so far

**Sub-fundamental — confirmed, and lower than expected.** Present from note 28
to 70 at **0.42-0.60 x f0**, −32 to −60 dB, absent above. `ROADMAP.md` 1.2
expected 0.58-0.83 from the literature; the bottom of the keyboard agrees, the
middle sits lower.

**Inharmonic modes live in the attack.** Measured across the whole held note,
the spectrum is almost purely integer harmonics -- because at Q≈225 a mode at
7 x f0 is 60 dB down within ~120 ms. Measured over the first 120 ms instead:
note 28 at 6.64x, 14.08x, 16.09x; note 34 at 6.62x, 25.08x; note 70 at 7.24x.
Broadly consistent with the model's 7.1 and 20.4, not a clean match.

**Q of the fundamental — measurable only from note 70 up**, where it reads
1208, 993, 2077, 2118, 1504, 1002 at notes 70, 76, 82, 88, 94, 100. Noisy and
not monotonic. The implied slope is **−0.108 octaves of Q per octave of pitch**,
where the model currently assumes **+0.217, rising**.

That is worth flagging and *not* worth acting on yet: six scattered points, no
monotonic trend, and a sampled library's decay may be shaped by the sampler's
own envelope rather than by the instrument. If the sign survives a longer
capture, the 1.3 curve is backwards.
