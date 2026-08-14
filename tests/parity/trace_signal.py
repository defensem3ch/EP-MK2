#!/usr/bin/env python3
"""Print the signal-rate graph of a Pd patch, walking back from its outlet~.

Used to read the voice's audio path in dependency order so it can be ported
without guessing at the topology.

  trace_signal.py <patch.pd>
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    '..', '..', '..', 'EP-MK2-Plugins', 'EP-MK2.5-heavy', 'tools'))
from pdsurgery import Patch

# Objects that carry audio.  Everything else is control rate and shows up only
# as "sets the right inlet of" in the listing.
SIGNAL = re.compile(
    r'^(inlet~|outlet~|\*~|\+~|-~|/~|clip~|cos~|sin~|osc~|phasor~|lop~|hip~|'
    r'bp~|biquad~|tanh~|pow~|sqrt~|abs~|min~|max~|wrap~|line~|vline~|hv\.vline~|'
    r'sig~|noise~|delread~|delwrite~|env~|snapshot~|r~|s~|ep\.|pd )')


def main():
    p = Patch(sys.argv[1])
    body = {i: p.body(i) for i in range(p.n)}
    edges = []
    for pos in p.connects:
        m = re.match(r'#X connect (\d+) (\d+) (\d+) (\d+);', p.recs[pos])
        edges.append(tuple(int(x) for x in m.groups()))

    def is_sig(i):
        return bool(SIGNAL.match(body[i]))

    producers = {}
    for a, ao, b, bo in edges:
        producers.setdefault(b, []).append((bo, a, ao))

    order, seen = [], set()

    def walk(i):
        if i in seen:
            return
        seen.add(i)
        for bo, a, ao in sorted(producers.get(i, [])):
            if is_sig(a):
                walk(a)
        order.append(i)

    for i in body:
        if body[i] == 'outlet~':
            walk(i)

    print(f'{sys.argv[1]}: {len(order)} objects on the audio path\n')
    for i in order:
        ins = []
        for bo, a, ao in sorted(producers.get(i, [])):
            kind = '~' if is_sig(a) else ' '
            ins.append(f'in{bo}<-[{a}]{kind}{body[a][:24]}')
        print(f'[{i:3}] {body[i][:34]:36} {"   ".join(ins)}')


if __name__ == '__main__':
    main()
