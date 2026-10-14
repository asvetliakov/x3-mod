#!/bin/sh
# Review fixes (convoy-gate-transit.md section 11, 2026-10-14): the unit cases of items 3 to 6 built against the HEAD core of the
# review (7fb54627) and against the working tree. Usage: sh run.sh (from the repository root; no Wine, no game).
set -e
H=$(mktemp -d)
mkdir -p "$H/src/proxy"
for f in ship_steering_core.h ship_avoidance_core.h ship_gate_ctl_core.h ship_gate_ctl_live_core.h ship_capital_lane_core.h ship_avoidance_walk_core.h; do
    git show 7fb54627:src/proxy/$f > "$H/src/proxy/$f" 2>/dev/null || true
done
D=verification/results/ship-steering/convoy-gate-transit/review_head
for t in item3 item4 item5 item6; do
    g++ -std=c++17 -O1 -I"$H/src/proxy" $D/$t.cpp -o "$H/${t}_head" && echo "$t HEAD:" && "$H/${t}_head"
    g++ -std=c++17 -O1 -Isrc/proxy $D/$t.cpp -o "$H/${t}_new" && echo "$t new:" && "$H/${t}_new"
done
rm -rf "$H"
