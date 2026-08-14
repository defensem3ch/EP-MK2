#!/usr/bin/env bash
# Build and run the parity tests.
#
# Each test drives a piece of the original Pd patch (compiled through hvcc) and
# the corresponding C++ side by side, then compares the results.  The point is
# that the port is checked against the patch itself rather than against a
# reading of it.
#
#   tests/parity/run.sh [hvcc-binary]
set -euo pipefail

cd "$(dirname "$0")"
HVCC="${1:-hvcc}"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

VOICE=../../../EP-MK2-Plugins/EP-MK2.5-heavy/ep-voice.pd

echo "== extracting DSP blocks from the original patch"
python3 extract_subpatch.py "$VOICE" resonator.coeff fixtures/ep.resonator.coeff.pd

fail=0

run_case() {
    local name="$1" fixture="$2" main="$3"
    echo
    echo "== $name"
    "$HVCC" "fixtures/$fixture" -n "$name" -o "$WORK/$name" -g c -p fixtures 2>&1 \
        | grep -i error && { echo "  hvcc failed"; fail=1; return; }
    g++ -O2 -std=c++14 -I "$WORK/$name/c" -o "$WORK/$name.bin" \
        "$main" "$WORK/$name"/c/*.c "$WORK/$name"/c/*.cpp -lm
    "$WORK/$name.bin" || fail=1
}

run_case Coeffs coeffs.pd parity_coeffs.cpp

echo
if [ "$fail" -eq 0 ]; then
    echo "PARITY OK"
else
    echo "PARITY FAILED"
fi
exit "$fail"
