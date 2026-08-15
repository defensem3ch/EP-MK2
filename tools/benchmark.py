#!/usr/bin/env python3
"""Compare the model against the reference instrument, measurement for measurement.

Both sides go through the functions in `analyse_samples`, so a difference in
the numbers is a difference in the sound rather than in how it was measured.
That matters more than it sounds: nearly every wrong conclusion in this project
came from measuring two things two ways and comparing the results.

Render the model's side first:

    ./build/EPMK2_bank_artefacts/Release/EPMK2_bank /tmp/model-bank
    python3 tools/benchmark.py --model /tmp/model-bank
"""

import argparse
import glob
import math
import os
import re
import statistics
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from analyse_samples import (load_metadata, load_mono, refine_f0, find_release,
                             partial_ratios, decay_q, centroid, trustworthy,
                             validate)


def model_files(directory):
    """The model's renders, keyed the same way as the library's."""
    out = {}
    for path in glob.glob(os.path.join(directory, "note_*_vel_*.wav")):
        m = re.search(r"note_(\d+)_vel_(\d+)", os.path.basename(path))
        if m:
            out[(int(m.group(1)), int(m.group(2)))] = path
    return out


def measure(path, note):
    """Every number this file has to offer, from one load."""
    x, rate = load_mono(path)
    nominal = 440.0 * 2 ** ((note - 69) / 12.0)
    release = find_release(x, rate)
    held = x[: int(release * rate)]
    f0, _ = refine_f0(held, rate, nominal)

    fund = None
    for p in partial_ratios(held, rate, f0):
        if abs(p["ratio"] - 1.0) < 0.1:
            fund = p
            break

    d = decay_q(x, rate, f0, until=release)
    attack = x[: int(min(0.12, release) * rate)]

    # The strongest partial that is not near a whole multiple: the tine.
    inharmonic = [p for p in partial_ratios(attack, rate, f0, floor_db=-50.0)
                  if p["ratio"] > 3.0 and abs(p["ratio"] - round(p["ratio"])) > 0.08]
    inharmonic.sort(key=lambda p: -p["level_db"])

    # A sub-fundamental, if there is one.
    subs = [p for p in partial_ratios(held, rate, f0) if p["ratio"] < 0.95]
    sub = max(subs, key=lambda p: p["level_db"]) if subs else None

    return dict(f0=f0, release=release, decay=d,
                peak=float(np.abs(x).max()),
                centroid=centroid(x, rate),
                inharmonic=inharmonic[0] if inharmonic else None,
                sub=sub,
                fundamental=fund)


def line(label, a, b, unit="", fmt="{:.1f}"):
    """One row: the instrument, the model, and the gap between them."""
    if a is None or b is None:
        return f"  {label:<26} {'-':>10} {'-':>10} {'':>9}"
    delta = b - a
    return (f"  {label:<26} {fmt.format(a):>10} {fmt.format(b):>10} "
            f"{delta:>+8.1f} {unit}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", required=True, help="directory of model renders")
    ap.add_argument("--velocity", type=lambda v: int(v, 0), default=0x7F)
    ap.add_argument("--notes", type=int, nargs="*")
    args = ap.parse_args()

    meta = load_metadata()
    for bad in validate(meta):
        pass
    meta = [m for m in meta if m["suspect"] is None]
    model = model_files(args.model)
    if not model:
        sys.exit(f"no renders in {args.model} -- run EPMK2_bank first")

    notes = sorted({m["note"] for m in meta} & {n for n, _ in model})
    if args.notes:
        notes = [n for n in notes if n in args.notes]

    print(f"Reference instrument vs the model, velocity 0x{args.velocity:02X}")
    print(f"  {'':<26} {'instrument':>10} {'model':>10} {'difference':>10}\n")

    gaps = dict(decay=[], centroid=[], tine=[], sub=[])
    for note in notes:
        ref = next((m for m in meta if m["note"] == note
                    and m["velocity"] == args.velocity), None)
        mod = model.get((note, args.velocity))
        if ref is None or mod is None:
            continue

        a = measure(ref["path"], note)
        b = measure(mod, note)
        print(f"  note {note}  ({a['f0']:.1f} Hz)")

        # Decay, as Q, where both sides can actually be measured.
        if trustworthy(a["decay"]) and trustworthy(b["decay"]):
            qa, qb = a["decay"]["q"], b["decay"]["q"]
            print(line("decay (Q)", qa, qb, fmt="{:.0f}"))
            gaps["decay"].append(20 * math.log10(qb / qa))
        else:
            print(f"  {'decay (Q)':<26} {'-':>10} {'-':>10}   not measurable")

        print(line("brightness (Hz)", a["centroid"], b["centroid"], fmt="{:.0f}"))
        gaps["centroid"].append(b["centroid"] - a["centroid"])

        # The tine: where its strongest inharmonic partial sits, and how loud.
        if a["inharmonic"] and b["inharmonic"]:
            print(line("tine partial (x f0)", a["inharmonic"]["ratio"],
                       b["inharmonic"]["ratio"], fmt="{:.2f}"))
            print(line("tine level (dB)", a["inharmonic"]["level_db"],
                       b["inharmonic"]["level_db"]))
            gaps["tine"].append(b["inharmonic"]["level_db"]
                                - a["inharmonic"]["level_db"])
        if a["sub"] and b["sub"]:
            print(line("sub-fundamental (x f0)", a["sub"]["ratio"],
                       b["sub"]["ratio"], fmt="{:.2f}"))
            gaps["sub"].append(b["sub"]["ratio"] - a["sub"]["ratio"])
        print()

    print("Summary, model minus instrument")
    if gaps["decay"]:
        print(f"  decay      {statistics.mean(gaps['decay']):+6.1f} dB of Q, "
              f"spread {statistics.pstdev(gaps['decay']):.1f}  "
              f"({len(gaps['decay'])} notes)")
    if gaps["centroid"]:
        print(f"  brightness {statistics.mean(gaps['centroid']):+6.0f} Hz, "
              f"spread {statistics.pstdev(gaps['centroid']):.0f}")
    if gaps["tine"]:
        print(f"  tine level {statistics.mean(gaps['tine']):+6.1f} dB, "
              f"spread {statistics.pstdev(gaps['tine']):.1f}")
    if gaps["sub"]:
        print(f"  sub ratio  {statistics.mean(gaps['sub']):+6.2f} x f0")


if __name__ == "__main__":
    main()
