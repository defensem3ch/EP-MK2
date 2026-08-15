#!/usr/bin/env python3
"""Is the fundamental a plain exponential decay, or does it beat?

A single resonator decays as a straight line in dB.  Two resonators tuned
near unison -- which is what a Rhodes tine and its tone bar are -- beat
against each other, and the envelope ripples around that line.  So the
question "does the model need a coupled pair?" is answerable by measuring
the ripple, and answerable on both sides with the same code.

    python3 tools/beating.py                  # the reference library
    python3 tools/beating.py --model DIR      # a bank rendered by EPMK2_bank

Read the caveats before the numbers.  This measurement produced a confident
wrong answer twice before it produced a usable one:

* The decayed tail is noise, and its wander measures as ripple.  Before the
  30 dB floor guard the instrument read 2.2 dB at note 88 and the model 7.4,
  and both were the noise.  The guard changed the *instrument* numbers by
  more than the effect being looked for.
* Below about note 45 the sub-fundamental leaks through the moving-average
  filter and reads as ripple that is not there.
* Ten seconds is three cycles of a 0.3 Hz beat, and the treble decays out of
  the window long before that -- hence the window column, and the refusal to
  print a figure under three seconds.

And a warning about the conclusion: what survived all of that still did not
agree with a direct high-resolution spectrum, which shows a *single* peak at
the fundamental on both sides.  See roadmap 1.7.  If the two disagree, the
spectrum is the one to believe: it needs no envelope filter, no floor guard
and no fit.
"""

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyse_samples import load_metadata, load_mono, refine_f0, find_release

NOTES = (28, 40, 52, 64, 76, 88)


def ripple(path, note):
    """Standard deviation of the fundamental's envelope about its own decay.

    Heterodyning to DC and taking the magnitude gives the fundamental's
    amplitude on its own, free of every other partial; fitting a line in dB
    and keeping the residual removes the decay, leaving whatever modulates it.
    """
    x, rate = load_mono(path)
    release = find_release(x, rate)
    held = x[: int(release * rate)]
    f0, _ = refine_f0(held, rate, 440.0 * 2 ** ((note - 69) / 12.0))

    t = np.arange(len(held)) / rate
    z = held * np.exp(-2j * np.pi * f0 * t)
    window = int(rate / max(f0 / 8.0, 8.0)) | 1
    env = np.abs(np.convolve(z, np.ones(window) / window, mode="valid"))
    env = env[int(0.15 * rate):]          # past the attack
    if len(env) < rate:
        return None, None, 0.0

    db = 20 * np.log10(np.maximum(env, 1e-12))

    # Stop where the note has fallen 30 dB.  Past that the window is the noise
    # floor and its wander gets measured as ripple -- which is not a small
    # effect: without this the instrument read 2.2 dB at note 88 and the model
    # 7.4, and both figures were the noise rather than the note.
    floor = np.nonzero(db < db[0] - 30.0)[0]
    if len(floor):
        db = db[: floor[0]]

    # A 0.3 Hz beat needs seconds to show.  Under three of them there is
    # nothing to measure, and a number here would be a number about noise --
    # which is the whole failure this function exists to avoid.
    seconds = len(db) / rate
    if seconds < 3.0:
        return None, None, seconds

    tt = np.arange(len(db)) / rate
    resid = db - np.polyval(np.polyfit(tt, db, 1), tt)

    spec = np.abs(np.fft.rfft(resid * np.hanning(len(resid))))
    freqs = np.fft.rfftfreq(len(resid), 1.0 / rate)
    band = (freqs > 0.3) & (freqs < 25.0)
    return float(resid.std()), float(freqs[band][np.argmax(spec[band])]), seconds


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", help="directory of renders from EPMK2_bank")
    ap.add_argument("--velocity", type=lambda v: int(v, 0), default=0x7F)
    args = ap.parse_args()

    meta = [m for m in load_metadata()
            if m["suspect"] is None and m["velocity"] == args.velocity]

    print("Ripple on the fundamental's envelope, about its own decay")
    print("  (below note 45 the sub-fundamental leaks in; read the treble)\n")
    header = f"  {'note':>4} {'instrument':>12} {'beat':>8} {'window':>7}"
    if args.model:
        header += f" {'model':>11} {'beat':>8} {'window':>7}"
    print(header)

    for note in NOTES:
        ref = next((m for m in meta if m["note"] == note), None)
        if ref is None:
            continue
        a, fa, sa = ripple(ref["path"], note)
        row = (f"  {note:>4} {a:>11.2f}dB {fa:>6.2f}Hz {sa:>6.1f}s"
               if a is not None
               else f"  {note:>4} {'-':>11}   {'-':>6}   {sa:>6.1f}s")

        if args.model:
            path = os.path.join(args.model,
                                f"note_{note:03d}_vel_{args.velocity:03d}.wav")
            if os.path.exists(path):
                b, fb, sb = ripple(path, note)
                row += (f" {b:>9.2f}dB {fb:>6.2f}Hz {sb:>6.1f}s"
                        if b is not None
                        else f" {'-':>9}   {'-':>6}   {sb:>6.1f}s")
        print(row)


if __name__ == "__main__":
    main()
