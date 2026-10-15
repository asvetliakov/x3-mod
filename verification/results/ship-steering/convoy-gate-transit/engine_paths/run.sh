#!/bin/sh
# Fixture engine paths (convoy-gate-transit.md section 11 "Fixture engine paths"; holistic review section 4 items 1, 3, 4, 6 and
# ship-navigation-ai.md section 10.14): the working tree's scenario C fixture on the working tree's core.
#  1. item A: every active lane cast under the engine model before this round (X3M_C_OLDWARN=1 X3M_C_NOFLY=1), the corrected model with the
#     0x12 counted (X3M_C_NOFLY=1) and flown (the default): the engine-warn figures that change (cmp_warns.py) and each run's failures;
#  2. items B-E: the parked casts (the "// PARKED:" lines of kCSetups) re-enabled, each run alone (X3M_C_ONLY, X3M_C_ENGALL: every seed's
#     engine-path row), their failures grouped by time factor and check: <cast>.txt.
# Usage: sh verification/results/ship-steering/convoy-gate-transit/engine_paths/run.sh (from the repository root; no Wine, no game).
set -e
OUT=$(dirname "$0")
H=$(mktemp -d)
mkdir -p "$H/src/proxy" "$H/verification/probe"
cp src/proxy/*.h "$H/src/proxy/"
cp verification/probe/ship_steering_host.cpp "$H/verification/probe/host.cpp"
sed -E 's#^    // PARKED: (\{"[a-z0-9_]+".*\},)$#    \1#' verification/probe/ship_steering_host.cpp > "$H/verification/probe/parked.cpp"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$H/verification/probe/host.cpp" -o "$H/host"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$H/verification/probe/parked.cpp" -o "$H/parked"
fails() { sed -E 's/^FAIL scenario C ([a-z0-9_]+) tf ([0-9]+) seed [0-9]+: /\1 tf\2: /' "$1" | sed -E 's/ \(.*//' | grep -v '^FAIL lane cost' | sort | uniq -c || true; }
X3M_C_OLDWARN=1 X3M_C_NOFLY=1 "$H/host" --lane > "$H/old.txt" 2> "$H/old.err" || true
X3M_C_NOFLY=1 "$H/host" --lane > "$H/nofly.txt" 2> "$H/nofly.err" || true
"$H/host" --lane > "$H/fly.txt" 2> "$H/fly.err" || true
{
    echo "== item A: the engine model before this round -> the corrected model, the 0x12 counted (seed 1, every active lane cast)"
    python3 -I "$OUT/cmp_warns.py" "$H/old.txt" "$H/nofly.txt"
    echo "== ... the corrected model counted -> flown"
    python3 -I "$OUT/cmp_warns.py" "$H/nofly.txt" "$H/fly.txt"
    for v in old nofly fly; do
        echo "== failures, $v (host-timing bench checks left out)"
        fails "$H/$v.err"
        grep -E '^ship_steering_host checks=' "$H/$v.txt"
    done
    echo "== the flown 0x12 in the active casts (seed 1)"
    grep -E '^scenario C .* engine paths ' "$H/fly.txt" | cut -c1-200
} > "$OUT/item_a.txt"
for c in $(grep -E '^    // PARKED: \{"' verification/probe/ship_steering_host.cpp | sed -E 's#^    // PARKED: \{"([a-z0-9_]+)".*#\1#'); do
    X3M_C_ONLY=$c X3M_C_ENGALL=1 "$H/parked" --lane > "$H/$c.txt" 2> "$H/$c.err" || true
    {
        echo "== $c (parked; every seed's engine-path row, the seed-1 rows, the spans, the failures by time factor and check)"
        grep -E "^scenario C $c (engine paths|spans)" "$H/$c.txt" || true
        grep -E "^scenario C $c lane tf" "$H/$c.txt" | cut -c1-600 || true
        fails "$H/$c.err" | grep -v 'key off (stage A)'
        grep -E '^ship_steering_host checks=' "$H/$c.txt"
    } > "$OUT/$c.txt"
done
rm -rf "$H"
