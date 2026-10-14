#!/bin/sh
# Final review fixes (convoy-gate-transit.md section 11, 2026-10-14): items 1, 2 and 4 built against the reviewed core (69d5b2ee) and
# the working tree. Usage: sh run.sh (from the repository root; no Wine, no game).
set -e
H=$(mktemp -d)
mkdir -p "$H/src/proxy"
for f in ship_steering_core.h ship_avoidance_core.h ship_gate_ctl_core.h ship_gate_ctl_live_core.h ship_capital_lane_core.h ship_avoidance_walk_core.h; do
    git show 69d5b2ee:src/proxy/$f > "$H/src/proxy/$f" 2>/dev/null || true
done
D=verification/results/ship-steering/convoy-gate-transit/final_head
for t in final1 final2 final4; do
    g++ -std=c++17 -O1 -DX3M_HEAD -I"$H/src/proxy" $D/$t.cpp -o "$H/${t}_head" && echo "$t HEAD:" && "$H/${t}_head"
    g++ -std=c++17 -O1 -Isrc/proxy $D/$t.cpp -o "$H/${t}_new" && echo "$t new:" && "$H/${t}_new"
done
rm -rf "$H"
