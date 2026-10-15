#!/bin/sh
# Review of the arrival round (convoy-gate-transit.md section 11 "Arrival round review fixes"): the working tree's scenario C fixture.
#  1. --lane, full, --capital: the totals and failures, the units' rows (lane.txt);
#  2. cast return_trip every seed: its return and ring rows (return_trip.txt); cast terran_rim (rim.txt);
#  3. fail-before (each on the final tree with the rule off, or the same case compiled against 97f3804b's src/proxy):
#     item1_before.txt (item1_before.sh)  the tree read's verdicts on the unit images, 97f3804b against the tree
#     before_item2.txt  the player's ship as B: return_trip without the gate-mouth hold (-DX3M_LANE_MOUTH=0, the round's measurement) and
#                       small_gate / small_gate_m6 / small_gate5, each with X3M_C_PLAYER_OLD=1 (site E before) and without
#     item3_before.txt (item3_before.sh)  the gate-mouth hold behind a leader that never launches, 97f3804b against the tree
#     item4_before.txt (item4_before.sh)  the lone decision against the block's predecessor, 97f3804b against the tree
#     before_item5.txt  --lane with X3M_C_RING_NOSTEP=1 (the ring row's horizon without the step) and the unit row
#     before_item8.txt  terran_rim with X3M_C_RING_NONE=1 (the tree read wrong toward none: the oracle's checks must fail)
#  4. the cost bench alternating 97f3804b and the tree three times (bench.txt).
# Usage: sh verification/results/ship-steering/convoy-gate-transit/arrival_round/review/run.sh (from the repository root; no Wine, no game).
set -e
OUT=$(dirname "$0")
H=$(mktemp -d)
F=verification/probe/ship_steering_host.cpp
CXX="c++ -std=c++17 -O2 -Wall -Wextra -Werror"
$CXX $F -o "$H/host"
$CXX -DX3M_LANE_MOUTH=0 $F -o "$H/nomouth"
fails() { sed -E 's/^FAIL scenario C ([a-z0-9_]+) tf ([0-9]+) seed [0-9]+: /\1 tf\2: /' "$1" | sed -E 's/ \(.*//' | grep -v 'key off' | sort | uniq -c || true; }
"$H/host" --lane > "$H/lane.txt" 2> "$H/lane.err" || true
"$H/host" > "$H/full.txt" 2> "$H/full.err" || true
"$H/host" --capital > "$H/cap.txt" 2> "$H/cap.err" || true
{
    for m in lane full cap; do echo "$m: $(grep -E '^ship_steering_host checks=' "$H/$m.txt")"; done
    grep -E '^arrival round units' "$H/lane.txt" | cut -c1-400
    fails "$H/lane.err"
} > "$OUT/lane.txt"
cast() { # <bin> <cast> <file> [env...]
    b=$1; c=$2; o=$3; shift 3
    env "$@" X3M_C_ONLY=$c X3M_C_ALL=1 "$b" --lane > "$H/$o.txt" 2> "$H/$o.err" || true
    {
        grep -E "^scenario C $c (return|ring|leaves|player) tf" "$H/$o.txt" | cut -c1-420 || true
        fails "$H/$o.err"
    }
}
cast "$H/host" return_trip rt > "$OUT/return_trip.txt"
cast "$H/host" terran_rim rim > "$OUT/rim.txt"
{
    echo "== return_trip, -DX3M_LANE_MOUTH=0, X3M_C_PLAYER_OLD=1 (before)"; cast "$H/nomouth" return_trip b2a X3M_C_PLAYER_OLD=1 | grep -E 'player tf|^ +[0-9]+ '
    echo "== return_trip, -DX3M_LANE_MOUTH=0 (after)"; cast "$H/nomouth" return_trip b2b | grep -E 'player tf|^ +[0-9]+ ' || true
    for c in small_gate small_gate_m6 small_gate5; do
        echo "== $c, X3M_C_PLAYER_OLD=1 (before)"; cast "$H/host" $c b2c X3M_C_PLAYER_OLD=1 | grep -E 'player tf|^ +[0-9]+ '
        echo "== $c (after)"; cast "$H/host" $c b2d | grep -E 'player tf|^ +[0-9]+ ' || true
    done
} > "$OUT/before_item2.txt"
{
    X3M_C_RING_NOSTEP=1 "$H/host" --lane > "$H/b5.txt" 2> "$H/b5.err" || true
    echo "--lane, X3M_C_RING_NOSTEP=1: $(grep -E '^ship_steering_host checks=' "$H/b5.txt")"
    grep -E '^scenario C [a-z0-9_]+ ring tf' "$H/b5.txt" | grep -v terran_rim | awk '{x+=$22; u+=$24} END {print "seed 1 and failing runs, every cast but terran_rim: non-member own-gate 0x12", x+0, "unsafe ring suppressions", u+0}'
    fails "$H/b5.err"
    grep -E '^arrival round units \(review item 5\)' "$H/lane.txt"
} > "$OUT/before_item5.txt"
cast "$H/host" terran_rim b8 X3M_C_RING_NONE=1 > "$OUT/before_item8.txt"
sh "$OUT/item1_before.sh"
sh "$OUT/item3_before.sh"
sh "$OUT/item4_before.sh"
git archive 97f3804b src/proxy verification/probe/ship_steering_host.cpp | (mkdir -p "$H/base" && tar -x -C "$H/base")
$CXX "$H/base/$F" -o "$H/base_bin"
: > "$OUT/bench.txt"
for i in 1 2 3; do
    for b in base_bin host; do
        X3M_C_ONLY=none "$H/$b" --lane 2> /dev/null | grep -E '^lane cost \(host\) bench24' | sed "s/^/$b run $i: /" | cut -c1-200 >> "$OUT/bench.txt" || true
    done
done
rm -rf "$H"
