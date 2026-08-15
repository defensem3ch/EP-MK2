#!/usr/bin/env python3
"""Measure what each factory preset actually sounds like.

A preset is a claim -- "this is a Wurlitzer", "this is a clav" -- and the
claim is checkable. A clav is plucked, so its partials should sit near whole
multiples; a Wurlitzer's bark is even harmonics, so its second should stand
above its third; a vibraphone's first overtone is a deliberate 4x. None of
that needs an ear, and none of it was ever checked.

    ./build/EPMK2_bank_artefacts/Release/EPMK2_bank /tmp/presets \
        --preset "Wurlitzer" --notes 40,52,64 --velocities 127 --seconds 4
    python3 tools/presets.py /tmp/presets
"""

import argparse
import glob
import math
import os
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyse_samples import load_mono, refine_f0, find_release, partial_ratios, decay_q


def partial_level(x, rate, freq):
    """One partial's level, by Goertzel, in dB relative to full scale."""
    w = 2.0 * math.pi * freq / rate
    c = 2.0 * math.cos(w)
    s1 = s2 = 0.0
    for v in x:
        s0 = v + c * s1 - s2
        s2, s1 = s1, s0
    mag = math.sqrt(max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2)) / max(1, len(x))
    return 20.0 * math.log10(max(mag, 1e-12))


def measure(path, note):
    x, rate = load_mono(path)
    release = find_release(x, rate)
    held = x[: int(release * rate)]
    f0, _ = refine_f0(held, rate, 440.0 * 2 ** ((note - 69) / 12.0))

    fundamental = partial_level(held, rate, f0)
    harmonics = [partial_level(held, rate, f0 * n) - fundamental for n in range(2, 7)]

    # The loudest partial that is not near a whole multiple: what the tine is
    # doing, as distinct from what the pickup's nonlinearity is doing.
    attack = held[: int(min(0.12, release) * rate)]
    inharmonic = [p for p in partial_ratios(attack, rate, f0, floor_db=-60.0)
                  if p["ratio"] > 1.5 and abs(p["ratio"] - round(p["ratio"])) > 0.08]
    inharmonic.sort(key=lambda p: -p["level_db"])

    d = decay_q(x, rate, f0, until=release)
    return dict(f0=f0, peak=20 * math.log10(max(float(np.abs(x).max()), 1e-9)),
                harmonics=harmonics, q=d.get("q"), r2=d.get("r2", 0.0),
                inharmonic=inharmonic[0] if inharmonic else None)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("directory")
    args = ap.parse_args()

    files = sorted(glob.glob(os.path.join(args.directory, "note_*_vel_*.wav")))
    if not files:
        sys.exit(f"no renders in {args.directory}")

    print(f"  {'note':>4} {'f0':>7} {'peak':>7} {'Q':>6} {'2nd':>6} {'3rd':>6}"
          f" {'4th':>6}  {'loudest inharmonic':>22}")
    evens, odds = [], []
    for path in files:
        m = re.search(r"note_(\d+)_vel_(\d+)", os.path.basename(path))
        note = int(m.group(1))
        r = measure(path, note)
        inh = (f"{r['inharmonic']['ratio']:.2f}x at {r['inharmonic']['level_db']:.0f} dB"
               if r["inharmonic"] else "none above -60 dB")
        q = f"{r['q']:.0f}" if r["q"] and r["r2"] > 0.8 else "-"
        print(f"  {note:>4} {r['f0']:>7.1f} {r['peak']:>6.1f}dB {q:>6}"
              f" {r['harmonics'][0]:>6.1f} {r['harmonics'][1]:>6.1f}"
              f" {r['harmonics'][2]:>6.1f}  {inh:>22}")
        evens += [r["harmonics"][0], r["harmonics"][2]]
        odds += [r["harmonics"][1], r["harmonics"][3]]

    # The one number that separates a Wurlitzer from a Rhodes: its bark is
    # even harmonics, which is what a tine sitting near the magnetic axis
    # produces.
    print(f"\n  even harmonics minus odd: {np.mean(evens) - np.mean(odds):+.1f} dB")


if __name__ == "__main__":
    main()
