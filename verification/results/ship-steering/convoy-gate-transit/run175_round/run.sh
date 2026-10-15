#!/bin/sh
# Run175 round (convoy-gate-transit.md section 11 "Run175 round"): the working tree's scenario C fixture.
#  1. --lane, full, --capital: the totals and failures, the Run175 units (lane.txt);
#  2. the round's casts on the tree: formed_size (item 1), return_trip with X3M_C_W_UNREAD=1 / X3M_C_W_FAIL=1 (item 2), side_commit and
#     return_pass (item 3), x12_near_side (item 4), parked (the hysteresis), each every seed: the lane summary's figures (casts.txt);
#  3. fail-before, each the final tree with the rule compiled off:
#     before_item1.txt  formed_size with -DX3M_LANE_GRID_ANY=0 (the 1 m grid of the hull's size only, no fallback)
#     before_item2.txt  return_trip with -DX3M_LANE_W_ENTRY=0 and X3M_C_W_UNREAD=1 (w = r as a member: the controller before)
#     before_item3.txt  side_commit and return_pass with -DX3M_LANE_FUNNEL=0 (every lane-stage member owned, the laws along F)
#     before_item4.txt  x12_near_side with -DX3M_LANE_LOOP_ZONE=0 (the loop block unbounded)
#     parked_formed_size2.txt  the parked cast (a script size 70 % above the hull: the fallback stands) on the tree;
#  4. the cost bench alternating a01815d4 and the tree three times (bench.txt).
# Usage: sh verification/results/ship-steering/convoy-gate-transit/run175_round/run.sh (from the repository root; no Wine, no game; ~30 min).
set -e
OUT=$(dirname "$0")
H=$(mktemp -d)
F=verification/probe/ship_steering_host.cpp
CXX="c++ -std=c++17 -O2 -Wall -Wextra -Werror"
$CXX $F -o "$H/host"
$CXX -DX3M_LANE_GRID_ANY=0 $F -o "$H/grid0"
$CXX -DX3M_LANE_W_ENTRY=0 $F -o "$H/w0"
$CXX -DX3M_LANE_FUNNEL=0 $F -o "$H/funnel0"
$CXX -DX3M_LANE_LOOP_ZONE=0 $F -o "$H/zone0"
fails() { sed -E 's/^FAIL scenario C ([a-z0-9_]+) tf ([0-9]+) seed [0-9]+: /\1 tf\2: /' "$1" | sed -E 's/ \(.*//' | grep -v 'key off' | sort | uniq -c || true; }
summ() { # the lane summary's figures per run
    grep -E "^scenario C $1 (lane|return) tf" "$2" | sed -E 's/ \(bound [^)]*\)//; s/ T_slot[^|]*rewrites/ rewrites/; s/ slot_max.*\| merges ([0-9]+)[^|]*/ merges \1/; s/ \| merge_frames[^|]*\| nose_away[^|]*\| order [^|]*\| esc_past[^|]*\| p2 [^|]*\| episodes/ | episodes/; s/ nonmember_warns[^|]*\| step_max/ | step_max/; s/ \| leader->escort.*\| law stops ([0-9]+ max [0-9.]+ s)[^|]*\|.*o4_cross/ | law stops \1 | o4_cross/; s/ \| pairs.*//' | cut -c1-330 || true
    grep -E "^scenario C $1 engine paths tf" "$2" | sed -E 's/scenario C [a-z0-9_]+ engine paths (tf [0-9]+ seed [0-9]+): (x12 flown [0-9]+)[^|]*\| (lane v_min [0-9.]+ m\/s slow [0-9.]+ s).*/  \1: \2, \3/' || true
}
cast() { # <bin> <cast> <tag> [env...]
    b=$1; c=$2; o=$3; shift 3
    env "$@" X3M_C_ONLY=$c X3M_C_ALL=1 "$b" --lane > "$H/$o.txt" 2> "$H/$o.err" || true
    echo "== $c ($o)"
    summ "$c" "$H/$o.txt"
    echo "FAIL rows (key off excluded): $(grep '^FAIL' "$H/$o.err" | grep -vc 'key off' || true)"
    fails "$H/$o.err"
}
"$H/host" --lane > "$H/lane.txt" 2> "$H/lane.err" || true
"$H/host" > "$H/full.txt" 2> "$H/full.err" || true
"$H/host" --capital > "$H/cap.txt" 2> "$H/cap.err" || true
{
    for m in lane full cap; do echo "$m: $(grep -E '^ship_steering_host checks=' "$H/$m.txt")"; done
    grep -E '^lane cost \(host\) bench24' "$H/lane.txt" | cut -c1-200
    fails "$H/lane.err"; fails "$H/full.err"; fails "$H/cap.err"
} > "$OUT/lane.txt"
{
    cast "$H/host" formed_size fs
    cast "$H/host" return_trip rt_unread X3M_C_W_UNREAD=1
    cast "$H/host" return_trip rt_fail X3M_C_W_FAIL=1
    cast "$H/host" side_commit sc
    cast "$H/host" return_pass rp
    cast "$H/host" x12_near_side xs
    cast "$H/host" parked pk
} > "$OUT/casts.txt"
cast "$H/grid0" formed_size b1 > "$OUT/before_item1.txt"
cast "$H/w0" return_trip b2 X3M_C_W_UNREAD=1 > "$OUT/before_item2.txt"
{ cast "$H/funnel0" side_commit b3a; cast "$H/funnel0" return_pass b3b; } > "$OUT/before_item3.txt"
cast "$H/zone0" x12_near_side b4 > "$OUT/before_item4.txt"
python3 -I - "$F" "$H/fs2.cpp" <<'PY'
import sys
s = open(sys.argv[1]).read()
a = '    // PARKED: {"formed_size2", kFormedSize2, 3, 1},'
assert a in s
open(sys.argv[2], 'w').write(s.replace(a, '    {"formed_size2", kFormedSize2, 3, 1},'))
PY
$CXX -I verification/probe "$H/fs2.cpp" -o "$H/fs2"
cast "$H/fs2" formed_size2 fs2 > "$OUT/parked_formed_size2.txt"
git archive a01815d4 src/proxy verification/probe/ship_steering_host.cpp | (mkdir -p "$H/base" && tar -x -C "$H/base")
$CXX "$H/base/$F" -o "$H/base_bin"
: > "$OUT/bench.txt"
for i in 1 2 3; do
    for b in base_bin host; do
        X3M_C_ONLY=none "$H/$b" --lane 2> /dev/null | grep -E '^lane cost \(host\) bench24' | sed "s/^/$b run $i: /" | cut -c1-200 >> "$OUT/bench.txt" || true
    done
done
rm -rf "$H"
