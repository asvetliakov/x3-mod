#!/bin/sh
# Lane fix round 2 item 3c (convoy-gate-transit.md section 11 "Lane fix round 2"): the overshoot's out-leg, its turn point d_end + k x the
# settling distance + R, at k = 1 (the tree), 0.5 and 0: the casts overshoot and overshoot_near (3 seeds x tf 1/6/10), P2's nose and the
# failed checks per k. Usage: sh out_leg.sh (from the repository root; no Wine, no game).
set -e
H=$(mktemp -d)
mkdir -p "$H/src/proxy" "$H/verification/probe"
cp src/proxy/*.h "$H/src/proxy/"
cp verification/probe/ship_steering_host.cpp "$H/verification/probe/"
for k in 1.0f 0.5f 0.0f; do
    sed "s|? lane_blend_settle(g, m) + m.redep_r : kLaneRedepM;|? $k * lane_blend_settle(g, m) + m.redep_r : kLaneRedepM;|" \
        src/proxy/ship_gate_ctl_core.h > "$H/src/proxy/ship_gate_ctl_core.h"
    c++ -std=c++17 -O2 "$H/verification/probe/ship_steering_host.cpp" -o "$H/h"
    for c in overshoot overshoot_near; do
        X3M_C_ONLY=$c X3M_C_ALL=1 "$H/h" --lane > "$H/o.txt" 2> "$H/e.txt" || true
        printf 'settle x %s %s: p2_nose ' "$k" "$c"
        grep "^scenario C $c lane" "$H/o.txt" | sed -E 's/.*p2_nose ([0-9.]+) deg.*/\1/' | sort -n | sed -n '1p;$p' | paste -sd'-' | tr -d '\n'
        printf ' deg, failed checks %s\n' "$(grep -c "^FAIL scenario C $c " "$H/e.txt" || true)"
    done
done
rm -rf "$H"
