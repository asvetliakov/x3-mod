#!/bin/sh
# The arrival round (Run174; convoy-gate-transit.md section 11 "Arrival round (Run174)"): the working tree's scenario C fixture on its core.
#  1. --lane: the totals and failures, the units (lane.txt);
#  2. cast return_trip, every seed: its return rows, its ring rows (return_trip.txt);
#  3. fail-before through the A/B switches (each on the final tree, the rule off):
#     before_item1.txt  return_trip with the controller before the round (X3M_C_LEAVE_OLD=1 X3M_C_RING_OFF=1, -DX3M_LANE_RMIN=0 -DX3M_LANE_MOUTH=0)
#     before_item2.txt  every cast, site A's old lane test (X3M_C_LEAVE_OLD=1): failures per cast and the board leaves on Why::target
#     before_item3.txt  return_trip and redeparture without the lone re-departure (-DX3M_LANE_RMIN=0)
#     before_item4.txt  every cast without site E's ring row (X3M_C_RING_OFF=1): failures per cast, the non-member own-gate 0x12 episodes
#     before_item5.txt  return_trip without the gate-mouth hold (-DX3M_LANE_MOUTH=0)
#  4. ring.txt: the ring row's counters per cast (seed 1; every run with an unsuppressed episode or an unsafe suppression);
#  5. the cost bench alternating 98a21d7c and the tree three times (bench.txt).
# Usage: sh verification/results/ship-steering/convoy-gate-transit/arrival_round/run.sh (from the repository root; no Wine, no game).
set -e
OUT=$(dirname "$0")
H=$(mktemp -d)
F=verification/probe/ship_steering_host.cpp
CXX="c++ -std=c++17 -O2 -Wall -Wextra -Werror"
$CXX $F -o "$H/host"
$CXX -DX3M_LANE_RMIN=0 -DX3M_LANE_MOUTH=0 $F -o "$H/old"
$CXX -DX3M_LANE_RMIN=0 $F -o "$H/normin"
$CXX -DX3M_LANE_MOUTH=0 $F -o "$H/nomouth"
fails() { sed -E 's/^FAIL scenario C ([a-z0-9_]+) tf ([0-9]+) seed [0-9]+: /\1 tf\2: /' "$1" | sed -E 's/ \(.*//' | grep -v 'key off' | sort | uniq -c || true; }
"$H/host" --lane > "$H/lane.txt" 2> "$H/lane.err" || true
{
    grep -E '^ship_steering_host checks=' "$H/lane.txt"
    grep -E '^arrival round units|^lane cost \(host\) bench24' "$H/lane.txt" | cut -c1-300
    fails "$H/lane.err"
} > "$OUT/lane.txt"
grep -E '^scenario C [a-z0-9_]+ ring tf' "$H/lane.txt" > "$OUT/ring.txt" || true
cast() { # <bin> <cast> <file> [env...]
    b=$1; c=$2; o=$3; shift 3
    env "$@" X3M_C_ONLY=$c X3M_C_ALL=1 "$b" --lane > "$H/$o.txt" 2> "$H/$o.err" || true
    {
        grep -E "^scenario C $c (return|ring|leaves) tf" "$H/$o.txt" | cut -c1-400 || true
        fails "$H/$o.err"
    }
}
cast "$H/host" return_trip return_trip > "$OUT/return_trip.txt"
cast "$H/old" return_trip b1 X3M_C_LEAVE_OLD=1 X3M_C_RING_OFF=1 > "$OUT/before_item1.txt"
{ cast "$H/normin" return_trip b3a; cast "$H/normin" redeparture b3b; } > "$OUT/before_item3.txt"
cast "$H/nomouth" return_trip b5 > "$OUT/before_item5.txt"
X3M_C_LEAVE_OLD=1 "$H/host" --lane > "$H/b2.txt" 2> "$H/b2.err" || true
{
    grep -E '^ship_steering_host checks=' "$H/b2.txt"
    grep -E '^scenario C [a-z0-9_]+ leaves tf' "$H/b2.txt" | awk '{print $3}' | sort | uniq -c | sed 's/$/ runs (seed 1 or failing) with a board leave on Why::target/'
    fails "$H/b2.err"
} > "$OUT/before_item2.txt"
X3M_C_RING_OFF=1 "$H/host" --lane > "$H/b4.txt" 2> "$H/b4.err" || true
{
    grep -E '^ship_steering_host checks=' "$H/b4.txt"
    grep -E '^scenario C [a-z0-9_]+ ring tf' "$H/b4.txt" | tr -d '()' | awk '{x[$3]+=$22} END {for (k in x) print k, "x12_nm", x[k]}' | sort
    fails "$H/b4.err"
} > "$OUT/before_item4.txt"
git archive 98a21d7c src/proxy verification/probe/ship_steering_host.cpp | (mkdir -p "$H/base" && tar -x -C "$H/base")
$CXX "$H/base/$F" -o "$H/base_bin"
: > "$OUT/bench.txt"
for i in 1 2 3; do
    for b in base_bin host; do
        X3M_C_ONLY=none "$H/$b" --lane 2> /dev/null | grep -E '^lane cost \(host\) bench24' | sed "s/^/$b run $i: /" | cut -c1-200 >> "$OUT/bench.txt" || true
    done
done
rm -rf "$H"
