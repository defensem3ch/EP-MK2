#!/usr/bin/env python3
"""Measure a sampled electric piano, to give the model something to be fitted to.

Reads the Renoise instrument in samples/ (13 pitches from E1 to E7, a tritone
apart, x 12 velocity layers) and reports, per pitch:

  * the partial frequencies as ratios of the fundamental -- which is what the
    tine mode ratios in the model are supposed to be
  * the decay rate of each partial, expressed as Q, which is what
    docs/ROADMAP.md 1.3 currently fills in from a half-remembered table
  * how level and brightness move with velocity, across all 12 layers

Nothing here is committed from the samples themselves; only the measurements,
which are ours.  See docs/ROADMAP.md Part 3.

    python3 tools/analyse_samples.py [--json out.json] [--pitch 40]
"""

import argparse
import glob
import json
import math
import os
import re
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SAMPLES = os.path.join(ROOT, "samples")


# --------------------------------------------------------------------------
def load_metadata():
    """Pitch and velocity for every sample, from the instrument definition.

    The filenames carry this too, but Renoise's note names are ambiguous about
    octave numbering -- its "E-2" is MIDI 28 -- so the XML is the authority.
    """
    path = os.path.join(SAMPLES, "Instrument.xml")
    with open(path, encoding="utf-8", errors="replace") as fh:
        xml = fh.read()

    base = [int(v) for v in re.findall(r"<BaseNote>([^<]*)</BaseNote>", xml)]
    vlo = [int(v) for v in re.findall(r"<VelocityStart>([^<]*)</VelocityStart>", xml)]
    vhi = [int(v) for v in re.findall(r"<VelocityEnd>([^<]*)</VelocityEnd>", xml)]
    names = re.findall(r"<Name>VST: Kontakt 5 8out_0x([0-9A-F]+)_([^<]+)</Name>", xml)

    if not (len(base) == len(vlo) == len(vhi) == len(names)):
        sys.exit("instrument metadata is inconsistent")

    def index_of(path):
        m = re.search(r"Sample(\d+)", os.path.basename(path))
        return int(m.group(1)) if m else -1

    files = sorted(glob.glob(os.path.join(SAMPLES, "SampleData", "*.flac")),
                   key=index_of)
    if len(files) != len(base):
        sys.exit(f"{len(files)} audio files but {len(base)} metadata entries")

    out = []
    for i, path in enumerate(files):
        out.append(
            dict(
                path=path,
                note=base[i],
                velocity=int(names[i][0], 16),
                vel_range=(vlo[i], vhi[i]),
                name=names[i][1],
            )
        )
    return out


def load_mono(path):
    data, rate = sf.read(path)
    if data.ndim > 1:
        data = data.mean(axis=1)
    return data.astype(np.float64), rate


# --------------------------------------------------------------------------
def refine_f0(x, rate, nominal):
    """Nominal pitch is known; find the actual peak near it.

    Real instruments are not exactly in equal temperament, and the tine's
    inharmonicity means the partial ratios must be measured against the
    fundamental that is actually there.
    """
    n = 1 << 18
    spec = np.abs(np.fft.rfft(x[: min(len(x), n)] * np.hanning(min(len(x), n)), n))
    freqs = np.fft.rfftfreq(n, 1.0 / rate)

    lo, hi = nominal * 0.94, nominal * 1.06
    band = (freqs >= lo) & (freqs <= hi)
    if not band.any():
        return nominal, 0.0
    idx = np.argmax(np.where(band, spec, 0))
    # Parabolic interpolation around the peak bin.
    if 0 < idx < len(spec) - 1:
        a, b, c = spec[idx - 1], spec[idx], spec[idx + 1]
        denom = a - 2 * b + c
        shift = 0.5 * (a - c) / denom if denom != 0 else 0.0
    else:
        shift = 0.0
    return float(freqs[idx] + shift * (freqs[1] - freqs[0])), float(spec[idx])


def partial_ratios(x, rate, f0, limit=45.0, floor_db=-60.0):
    """Spectral peaks above the noise floor, as ratios of f0.

    This is the measurement the model's tine ratios (7.1, 20.4, 39.7) are
    meant to reproduce, and the one that says whether a sub-fundamental below
    f0 is really there.
    """
    n = 1 << 18
    seg = x[: min(len(x), n)]
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    freqs = np.fft.rfftfreq(n, 1.0 / rate)

    nyq = rate * 0.5
    top = min(f0 * limit, nyq * 0.95)
    band = (freqs > f0 * 0.4) & (freqs < top)
    if not band.any():
        return []

    mag = np.where(band, spec, 0.0)
    peak = mag.max()
    if peak <= 0:
        return []
    thresh = peak * (10.0 ** (floor_db / 20.0))

    # Local maxima above the floor, kept apart by a fraction of f0 so one
    # broad peak is not reported several times.
    spacing = max(1, int((f0 * 0.4) / (freqs[1] - freqs[0])))
    found = []
    order = np.argsort(mag)[::-1]
    taken = np.zeros(len(mag), dtype=bool)
    for idx in order:
        if mag[idx] < thresh:
            break
        lo, hi = max(0, idx - spacing), min(len(mag), idx + spacing + 1)
        if taken[lo:hi].any():
            continue
        taken[idx] = True
        found.append((float(freqs[idx]), float(mag[idx])))

    found.sort(key=lambda t: t[0])
    return [
        dict(freq=f, ratio=f / f0, level_db=20 * math.log10(max(m / peak, 1e-12)))
        for f, m in found
    ]


def find_release(x, rate, hop=0.025, drop_db=3.5, earliest=0.15):
    """Where the sampled note is released, in seconds.

    These samples hold for about half a second and are then faded to the noise
    floor -- they were captured with a short note, not left to ring.  Fitting a
    decay through that fade measures the fade, and reports a Q of about 30 for
    an instrument whose real Q is in the hundreds or thousands.  So find the
    knee where the level starts falling far faster than any natural decay, and
    only look before it.
    """
    n = max(1, int(hop * rate))
    frames = [x[i:i + n] for i in range(0, len(x) - n, n)]
    if len(frames) < 4:
        return len(x) / rate
    rms = np.array([20 * math.log10(max(float(np.sqrt((f ** 2).mean())), 1e-12))
                    for f in frames])

    first = max(1, int(earliest / hop))
    for i in range(first, len(rms) - 1):
        if rms[i] - rms[i + 1] > drop_db:
            return i * hop
    return len(x) / rate


def decay_q(x, rate, freq, skip=0.05, tail=0.03, until=None):
    """Fit an exponential to one partial's envelope and report it as Q.

    A mode decays as exp(-pi*f*t/Q), so the slope of log-amplitude gives Q
    directly.  That matters here because the samples are under a second long:
    a full T60 is not observable, but the decay *rate* is.

    Returns (Q, r2, span_db) -- the fit quality and how far the partial
    actually fell over the window are reported because for low notes it barely
    falls at all, and a Q derived from that is not worth much.
    """
    # Narrow complex heterodyne + lowpass: cleaner than a bandpass for one
    # known frequency, and phase-insensitive.
    t = np.arange(len(x)) / rate
    mixed = x * np.exp(-2j * math.pi * freq * t)

    # Moving average of ~4 cycles, at least 256 samples.
    win = max(256, int(round(4.0 * rate / max(freq, 1.0))))
    kernel = np.ones(win) / win
    env = np.abs(np.convolve(mixed, kernel, mode="same"))

    a = int(skip * rate)
    b = len(env) - int(tail * rate) if until is None else int(until * rate)
    b = min(b, len(env) - int(tail * rate))
    if b - a < win:
        return None
    seg = env[a:b]
    if seg.max() <= 0:
        return None

    y = np.log(np.maximum(seg, seg.max() * 1e-6))
    tt = np.arange(len(seg)) / rate
    slope, intercept = np.polyfit(tt, y, 1)

    resid = y - (slope * tt + intercept)
    ss_res = float((resid ** 2).sum())
    ss_tot = float(((y - y.mean()) ** 2).sum())
    r2 = 1.0 - ss_res / ss_tot if ss_tot > 0 else 0.0

    span_db = 20.0 / math.log(10) * (y[0] - y[-1])

    if slope >= 0:
        return None
    q = math.pi * freq / (-slope)
    return dict(q=float(q), r2=float(r2), span_db=float(span_db),
                tau=float(-1.0 / slope), window=float((b - a) / rate))


def centroid(x, rate):
    n = 1 << 15
    seg = x[: min(len(x), n)]
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    freqs = np.fft.rfftfreq(n, 1.0 / rate)
    total = spec.sum()
    return float((freqs * spec).sum() / total) if total > 0 else 0.0


# --------------------------------------------------------------------------
def trustworthy(d):
    """Whether a decay fit is worth quoting.

    A partial that barely moved over the window gives a slope indistinguishable
    from noise, and the Q that comes out of it can be off by an order of
    magnitude.  A 41 Hz partial with Q around 1000 falls under a dB in the half
    second these samples hold for, so the bass is genuinely not measurable here
    and should say so rather than report a number.
    """
    return d is not None and d["r2"] >= 0.90 and d["span_db"] >= 3.0


def model_q(note, ref_q=1334.0, tracking=0.217):
    """What EP-MK2 currently uses, for comparison -- see ROADMAP 1.3."""
    return ref_q * 2 ** (tracking * (note - 69) / 12.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", help="write full measurements here")
    ap.add_argument("--pitch", type=int, action="append",
                    help="only this MIDI note (repeatable)")
    ap.add_argument("--velocity", type=int, default=0x7F,
                    help="velocity layer used for the spectral work")
    args = ap.parse_args()

    if not os.path.isdir(os.path.join(SAMPLES, "SampleData")):
        sys.exit(f"no samples in {SAMPLES} -- unzip the .xrni there first")

    meta = load_metadata()
    notes = sorted({m["note"] for m in meta})
    if args.pitch:
        notes = [n for n in notes if n in args.pitch]

    results = dict(pitches={}, velocity={})
    analysed = {}

    for note in notes:
        entry = next((m for m in meta
                      if m["note"] == note and m["velocity"] == args.velocity), None)
        if entry is None:
            continue
        x, rate = load_mono(entry["path"])
        nominal = 440.0 * 2 ** ((note - 69) / 12.0)
        release = find_release(x, rate)
        held = x[: int(release * rate)]
        f0, _ = refine_f0(held, rate, nominal)

        parts = partial_ratios(held, rate, f0)
        for p in parts:
            p["decay"] = decay_q(x, rate, p["freq"], until=release)

        # The inharmonic tine modes are short-lived -- at Q around 225 a mode
        # at 7 x f0 is down 60 dB within a couple of hundred milliseconds -- so
        # looking at the whole held note averages them away entirely.  They
        # have to be measured in the attack.
        attack = x[: int(min(0.12, release) * rate)]
        attack_parts = partial_ratios(attack, rate, f0, floor_db=-45.0)

        analysed[note] = dict(f0=f0, nominal=nominal, release=release,
                              duration=len(x) / rate, partials=parts,
                              attack_partials=attack_parts)
        results["pitches"][note] = analysed[note]

    # ---- 1. Q of the fundamental: the measurement ROADMAP 1.3 needs --------
    print("Q of the fundamental, against what the model currently assumes")
    print(f"  {'note':>4} {'f0 Hz':>9} {'measured Q':>11} {'fit':>5} {'fell':>7} "
          f"{'model Q':>8}  verdict")
    for note in notes:
        rec = analysed.get(note)
        if not rec:
            continue
        fund = min(rec["partials"], key=lambda p: abs(p["ratio"] - 1.0), default=None)
        mq = model_q(note)
        if fund is None or fund["decay"] is None:
            print(f"  {note:>4} {rec['f0']:9.2f} {'-':>11} {'-':>5} {'-':>7} "
                  f"{mq:8.0f}  still rising, no decay to fit")
            continue
        d = fund["decay"]
        ok = trustworthy(d)
        verdict = "usable" if ok else "too little decay to trust"
        print(f"  {note:>4} {rec['f0']:9.2f} {d['q']:11.0f} {d['r2']:5.2f} "
              f"{d['span_db']:6.1f}dB {mq:8.0f}  {verdict}")

    usable = [(n, min(analysed[n]["partials"],
                      key=lambda p: abs(p["ratio"] - 1.0))["decay"])
              for n in notes if analysed.get(n) and analysed[n]["partials"]]
    usable = [(n, d) for n, d in usable if trustworthy(d)]
    if len(usable) >= 2:
        lo, hi = usable[0], usable[-1]
        octaves = (hi[0] - lo[0]) / 12.0
        slope = math.log2(hi[1]["q"] / lo[1]["q"]) / octaves if octaves else 0.0
        print(f"\n  measurable range: note {lo[0]} Q {lo[1]['q']:.0f} "
              f"-> note {hi[0]} Q {hi[1]['q']:.0f}")
        print(f"  implied tracking: {slope:.3f} octaves of Q per octave of pitch "
              f"(model uses 0.217)")
        results["q_tracking_measured"] = slope

    # ---- 2. inharmonic partials -------------------------------------------
    print("\nPartials above the fundamental, flagged harmonic or inharmonic")
    print("  (the model places tine modes at 7.1, 20.4 and 39.7 x f0)")
    for note in notes:
        rec = analysed.get(note)
        if not rec:
            continue
        inh = [p for p in rec["partials"]
               if p["ratio"] > 1.5 and abs(p["ratio"] - round(p["ratio"])) > 0.06]
        top = sorted(rec["partials"], key=lambda p: -p["level_db"])[:10]
        harm = sum(1 for p in top if abs(p["ratio"] - round(p["ratio"])) <= 0.06)
        desc = ", ".join(f"{p['ratio']:.2f}x ({p['level_db']:.0f} dB)"
                         for p in sorted(inh, key=lambda p: -p["level_db"])[:5])
        print(f"  note {note:>3}: {harm}/{len(top)} strongest are near-integer"
              + (f" | inharmonic: {desc}" if desc else " | no inharmonic peaks found"))

    # ---- 2b. inharmonic content in the attack ------------------------------
    print("\nInharmonic partials in the first 120 ms, where the tine modes live")
    print("  (model: 7.1, 20.4, 39.7 x f0)")
    for note in notes:
        rec = analysed.get(note)
        if not rec:
            continue
        inh = [p for p in rec.get("attack_partials", [])
               if p["ratio"] > 3.0 and abs(p["ratio"] - round(p["ratio"])) > 0.08]
        inh.sort(key=lambda p: -p["level_db"])
        if inh:
            desc = ", ".join(f"{p['ratio']:.2f}x ({p['level_db']:.0f} dB)"
                             for p in inh[:6])
            print(f"  note {note:>3}: {desc}")
        else:
            print(f"  note {note:>3}: none above the floor")

    # ---- 3. sub-fundamental ------------------------------------------------
    print("\nSub-fundamental (the model has none; ROADMAP 1.2 expects 0.58-0.83)")
    for note in notes:
        rec = analysed.get(note)
        if not rec:
            continue
        subs = [p for p in rec["partials"] if p["ratio"] < 0.95]
        if subs:
            best = max(subs, key=lambda p: p["level_db"])
            print(f"  note {note:>3}: {best['ratio']:.3f} x f0 "
                  f"({best['freq']:.1f} Hz) at {best['level_db']:.0f} dB")
        else:
            print(f"  note {note:>3}: none above the floor")

    # ---- 4. velocity -------------------------------------------------------
    print("\nVelocity response")
    print(f"  {'note':>4}  {'velocities':>12}  {'level':>18}  {'brightness':>20}")
    for note in notes:
        rows = sorted((m for m in meta if m["note"] == note),
                      key=lambda m: m["velocity"])
        vel_rec = []
        for m in rows:
            x, rate = load_mono(m["path"])
            pk = float(np.abs(x).max())
            vel_rec.append(dict(velocity=m["velocity"],
                                peak_db=20 * math.log10(max(pk, 1e-12)),
                                centroid=centroid(x, rate)))
        results["velocity"][note] = vel_rec
        lo, hi = vel_rec[0], vel_rec[-1]
        print(f"  {note:>4}  {lo['velocity']:3d} -> {hi['velocity']:3d}   "
              f"{lo['peak_db']:6.1f} -> {hi['peak_db']:6.1f} dB   "
              f"{lo['centroid']:6.0f} -> {hi['centroid']:6.0f} Hz")

    if args.json:
        with open(args.json, "w") as fh:
            json.dump(results, fh, indent=1)
        print(f"\nwrote {args.json}")


if __name__ == "__main__":
    main()
