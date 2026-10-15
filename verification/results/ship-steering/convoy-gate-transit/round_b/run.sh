#!/bin/sh
# Round B (convoy-gate-transit.md section 11 "Round B (small ships, statics on Lambda)"; section 12.2 / 12.3): the working tree's scenario
# C fixture on the working tree's core.
#  1. --lane: the totals, the key-off control line, the round B unit rows, the cost bench (lane.txt);
#  2. the casts round B un-parked (small_gate, small_gate_m6, lane_fallback), each alone, every seed's engine-path row: <cast>.txt;
#  3. the casts still parked (the "// PARKED:" lines re-enabled, as engine_paths/run.sh): parked_<cast>.txt;
#  4. lane_station and lane_belt with the lateral profile's shift on (X3M_C_PROFSHIFT=1; off in the DLL): shift_<cast>.txt;
#  then summary.py -> summary.txt (per cast and time factor over the three seeds, and the failures).
# Usage: sh verification/results/ship-steering/convoy-gate-transit/round_b/run.sh (from the repository root; no Wine, no game).
set -e
OUT=$(dirname "$0")
H=$(mktemp -d)
mkdir -p "$H/src/proxy" "$H/verification/probe"
cp src/proxy/*.h "$H/src/proxy/"
cp verification/probe/ship_steering_host.cpp "$H/verification/probe/host.cpp"
sed -E 's#^    // PARKED: (\{"[a-z0-9_]+".*\},)$#    \1#' verification/probe/ship_steering_host.cpp > "$H/verification/probe/parked.cpp"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$H/verification/probe/host.cpp" -o "$H/host"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$H/verification/probe/parked.cpp" -o "$H/parked"
fails() { sed -E 's/^FAIL scenario C ([a-z0-9_]+) tf ([0-9]+) seed [0-9]+: /\1 tf\2: /' "$1" | sed -E 's/ \(.*//' | grep -v 'FAIL lane cost\|key off' | sort | uniq -c || true; }
"$H/host" --lane > "$H/lane.txt" 2> "$H/lane.err" || true
{
    grep -E '^ship_steering_host checks=' "$H/lane.txt"
    grep -E '^scenario C control|^round B|^lane cost' "$H/lane.txt" | cut -c1-400
    fails "$H/lane.err"
} > "$OUT/lane.txt"
cast() { # <bin> <cast> <file> [env]
    if [ -n "$4" ]; then env "$4" X3M_C_ONLY=$2 X3M_C_ENGALL=1 "$1" --lane > "$H/$3.txt" 2> "$H/$3.err" || true
    else X3M_C_ONLY=$2 X3M_C_ENGALL=1 "$1" --lane > "$H/$3.txt" 2> "$H/$3.err" || true; fi
    {
        grep -E "^scenario C $2 (engine paths|spans)" "$H/$3.txt" || true
        fails "$H/$3.err"
    } > "$OUT/$3.txt"
}
for c in small_gate small_gate_m6 lane_fallback; do cast "$H/host" $c $c; done
for c in $(grep -E '^    // PARKED: \{"' verification/probe/ship_steering_host.cpp | sed -E 's#^    // PARKED: \{"([a-z0-9_]+)".*#\1#'); do
    cast "$H/parked" $c parked_$c
done
for c in lane_station lane_belt; do cast "$H/parked" $c shift_$c X3M_C_PROFSHIFT=1; done
rm -rf "$H"
python3 -I "$OUT/summary.py" "$OUT" > "$OUT/summary.txt"
