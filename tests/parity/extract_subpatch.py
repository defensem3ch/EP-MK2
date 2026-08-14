#!/usr/bin/env python3
"""Lift a named subpatch out of a Pd file into a standalone abstraction.

The port is verified against the original patch rather than against my reading
of it, so the parity fixtures need to instantiate the *actual* Pd DSP blocks.
This pulls one out (e.g. [pd resonator.coeff]) and writes it as an abstraction
that a fixture patch can then instantiate by name.

  extract_subpatch.py <source.pd> <subpatch name> <output.pd>
"""
import os
import sys

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    '..', '..', '..', 'EP-MK2-Plugins', 'EP-MK2.5-heavy', 'tools'))
from pdsurgery import read_records


def extract(src, name):
    recs = read_records(src)
    depth = 0
    start = None
    for pos, r in enumerate(recs):
        if r.startswith('#N canvas'):
            depth += 1
            # '#N canvas x y w h <name> <open>'
            if start is None and r.rstrip(';').split(' ')[-2:-1] == [name]:
                start = pos
                start_depth = depth
        elif r.startswith('#X restore'):
            if start is not None and depth == start_depth:
                return recs[start:pos + 1]
            depth -= 1
    raise SystemExit(f'subpatch "{name}" not found in {src}')


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    src, name, dst = sys.argv[1:4]
    body = extract(src, name)

    # An abstraction is a plain canvas: keep the opener, drop the '#X restore'
    # that only exists because it was embedded in a parent.
    out = [body[0]] + body[1:-1]
    with open(dst, 'w') as f:
        f.write('\n'.join(out) + '\n')
    print(f'{os.path.relpath(src)} [{name}] -> {os.path.relpath(dst)} '
          f'({len(out)} records)')


if __name__ == '__main__':
    main()
