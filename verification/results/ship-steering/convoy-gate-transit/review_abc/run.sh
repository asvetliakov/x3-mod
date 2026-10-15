#!/bin/sh
# Review of rounds A-C (convoy-gate-transit.md section 11 "Rounds A-C review fixes"): the working tree's scenario C fixture on its core.
#  1. --lane: the totals, the key-off control line, the review units' rows, the cost bench (lane.txt);
#  2. the casts the fixes touch or add, each alone, every seed's engine-path and review rows: <cast>.txt;
#  3. fail-before through the fixture's A/B switches (the fix's rule off): before_arrival_cross (X3M_C_ARR_PARAMS0=1: the launch hook's
#     JoinParams{}), before_small_gate and before_small_gate5 (-DX3M_SMALL_HOLD_EXT_MS=60000u: no extension), before_lane_fb_small
#     (-DX3M_LANE_ENCLOSE_MIN_M=0.0f: any fallback body holding d_end listed; the parked cast re-enabled);
#  4. every parked cast (the "// PARKED:" lines of kCSetups re-enabled, as engine_paths/run.sh): parked_<cast>.txt;
#  5. the cost bench alternating e7a7120c and the tree three times (bench.txt);
#  then summary.py -> summary.txt (per cast and time factor over the three seeds, and the failures).
# Usage: sh verification/results/ship-steering/convoy-gate-transit/review_abc/run.sh (from the repository root; no Wine, no game).
set -e
OUT=$(dirname "$0")
H=$(mktemp -d)
mkdir -p "$H/base" "$H/src/proxy" "$H/verification/probe"
F=verification/probe/ship_steering_host.cpp
cp src/proxy/*.h "$H/src/proxy/"
sed -E 's#^    // PARKED: (\{"[a-z0-9_]+".*\},)$#    \1#' $F > "$H/verification/probe/parked.cpp"
c++ -std=c++17 -O2 -Wall -Wextra -Werror $F -o "$H/host"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$H/verification/probe/parked.cpp" -o "$H/parked"
c++ -std=c++17 -O2 -Wall -Wextra -Werror -DX3M_SMALL_HOLD_EXT_MS=60000u $F -o "$H/hold60"
c++ -std=c++17 -O2 -Wall -Wextra -Werror -DX3M_LANE_ENCLOSE_MIN_M=0.0f "$H/verification/probe/parked.cpp" -o "$H/enclose0"
fails() { sed -E 's/^FAIL scenario C ([a-z0-9_]+) tf ([0-9]+) seed [0-9]+: /\1 tf\2: /' "$1" | sed -E 's/ \(.*//' | grep -v 'FAIL lane cost\|key off' | sort | uniq -c || true; }
"$H/host" --lane > "$H/lane.txt" 2> "$H/lane.err" || true
{
    grep -E '^ship_steering_host checks=' "$H/lane.txt"
    grep -E '^scenario C control|review A-C|^round C units \(review|^round B (small|profile) units|^lane cost' "$H/lane.txt" | cut -c1-400
    fails "$H/lane.err"
} > "$OUT/lane.txt"
cast() { # <bin> <cast> <file> [env...]
    b=$1; c=$2; o=$3; shift 3
    env "$@" X3M_C_ONLY=$c X3M_C_ENGALL=1 "$b" --lane > "$H/$o.txt" 2> "$H/$o.err" || true
    {
        grep -E "^scenario C $c (engine paths|spans|review)" "$H/$o.txt" || true
        fails "$H/$o.err"
    } > "$OUT/$o.txt"
}
for c in arrival_pass arrival_pass2 arrival_cross small_gate small_gate_m6 small_gate5 x12_near x12_far lane_fallback; do cast "$H/host" $c $c; done
cast "$H/host" arrival_cross before_arrival_cross X3M_C_ARR_PARAMS0=1
cast "$H/hold60" small_gate before_small_gate
cast "$H/hold60" small_gate5 before_small_gate5
cast "$H/enclose0" lane_fb_small before_lane_fb_small
for c in $(grep -E '^    // PARKED: \{"' $F | sed -E 's#^    // PARKED: \{"([a-z0-9_]+)".*#\1#'); do
    cast "$H/parked" $c parked_$c
done
git archive e7a7120c src/proxy verification/probe/ship_steering_host.cpp | tar -x -C "$H/base"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$H/base/$F" -o "$H/base_bin"
: > "$OUT/bench.txt"
for i in 1 2 3; do
    for b in base_bin host; do
        X3M_C_ONLY=none "$H/$b" --lane 2> /dev/null | grep -E '^lane cost \(host\) bench24' | sed "s/^/$b run $i: /" | cut -c1-200 >> "$OUT/bench.txt" || true
    done
done
rm -rf "$H"
python3 -I "$OUT/summary.py" "$OUT" > "$OUT/summary.txt"
