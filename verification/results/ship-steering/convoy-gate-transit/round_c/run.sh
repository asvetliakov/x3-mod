#!/bin/sh
# Round C (convoy-gate-transit.md section 11 "Round C (loop-back join)"; section 12.4): the working tree's scenario C fixture on the working
# tree's core, and the fail-before on d2871285 (rounds A and B merged, the two casts parked).
#  1. --lane: the totals, the key-off control line, the round C unit rows, the cost bench (lane.txt);
#  2. the casts round C un-parked (x12_near, x12_far), each alone, every seed's engine-path row: <cast>.txt;
#  3. the casts still parked (the "// PARKED:" lines re-enabled, as engine_paths/run.sh): parked_<cast>.txt;
#  4. fail-before: d2871285's fixture and core (git archive) with its PARKED lines re-enabled, the two casts: before_<cast>.txt;
#  5. every other cast's scenario C rows over all seeds, d2871285 against the tree: laneon_cmp.txt;
#  then summary.py -> summary.txt (per cast and time factor over the three seeds, and the failures), span_cost.py -> span_cost.txt (the
#  loop's span cost per run against d2871285).
# Usage: sh verification/results/ship-steering/convoy-gate-transit/round_c/run.sh (from the repository root; no Wine, no game).
set -e
OUT=$(dirname "$0")
H=$(mktemp -d)
mkdir -p "$H/src/proxy" "$H/verification/probe" "$H/base"
cp src/proxy/*.h "$H/src/proxy/"
cp verification/probe/ship_steering_host.cpp "$H/verification/probe/host.cpp"
sed -E 's#^    // PARKED: (\{"[a-z0-9_]+".*\},)$#    \1#' verification/probe/ship_steering_host.cpp > "$H/verification/probe/parked.cpp"
git archive d2871285 src/proxy verification/probe/ship_steering_host.cpp | tar -x -C "$H/base"
sed -i -E 's#^    // PARKED: (\{"[a-z0-9_]+".*\},)$#    \1#' "$H/base/verification/probe/ship_steering_host.cpp"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$H/verification/probe/host.cpp" -o "$H/host"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$H/verification/probe/parked.cpp" -o "$H/parked"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$H/base/verification/probe/ship_steering_host.cpp" -o "$H/before"
fails() { sed -E 's/^FAIL scenario C ([a-z0-9_]+) tf ([0-9]+) seed [0-9]+: /\1 tf\2: /' "$1" | sed -E 's/ \(.*//' | grep -v 'FAIL lane cost\|key off' | sort | uniq -c || true; }
"$H/host" --lane > "$H/lane.txt" 2> "$H/lane.err" || true
{
    grep -E '^ship_steering_host checks=' "$H/lane.txt"
    grep -E '^scenario C control|^round C|^lane cost' "$H/lane.txt" | cut -c1-400
    fails "$H/lane.err"
} > "$OUT/lane.txt"
cast() { # <bin> <cast> <file>
    X3M_C_ONLY=$2 X3M_C_ENGALL=1 "$1" --lane > "$H/$3.txt" 2> "$H/$3.err" || true
    {
        grep -E "^scenario C $2 (engine paths|spans)" "$H/$3.txt" || true
        fails "$H/$3.err"
    } > "$OUT/$3.txt"
}
for c in x12_near x12_far; do cast "$H/host" $c $c; cast "$H/before" $c before_$c; done
# every other cast's rows, every seed, d2871285 against the tree (laneon_cmp.py: 0 differing expected)
X3M_C_ALL=1 "$H/before" --lane > "$H/before_all.txt" 2> /dev/null || true
X3M_C_ALL=1 "$H/host" --lane > "$H/tree_all.txt" 2> /dev/null || true
python3 -I "$OUT/laneon_cmp.py" "$H/before_all.txt" "$H/tree_all.txt" > "$OUT/laneon_cmp.txt"
for c in $(grep -E '^    // PARKED: \{"' verification/probe/ship_steering_host.cpp | sed -E 's#^    // PARKED: \{"([a-z0-9_]+)".*#\1#'); do
    cast "$H/parked" $c parked_$c
done
rm -rf "$H"
python3 -I "$OUT/summary.py" "$OUT" > "$OUT/summary.txt"
python3 -I "$OUT/span_cost.py" "$OUT" > "$OUT/span_cost.txt"
