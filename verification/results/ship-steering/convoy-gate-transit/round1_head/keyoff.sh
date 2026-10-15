#!/bin/sh
# Lane fix round 1: key-off identity against the pre-lane fixture 9f764d69 (keyoff_diff.py). Usage: sh keyoff.sh <dir> [label] where <dir>
# holds the tree binary's outputs with X3M_LANE_OFF=1: full_off.txt, cap_off.txt (--capital), seqoff_off.txt (X3M_SEQ_OFF=1), b9_off.txt
# (--b9); label names the change in the header (default "lane fix round 1, 2026-10-15"). Builds 9f764d69's fixture and headers in a
# temporary tree (from the repository root; no Wine, no game).
set -e
A=$1
LABEL=${2:-lane fix round 1, 2026-10-15}
H=$(mktemp -d)
mkdir -p "$H/src/proxy" "$H/verification/probe"
for f in ship_steering_core.h ship_avoidance_core.h ship_gate_ctl_core.h ship_gate_ctl_live_core.h ship_capital_lane_core.h ship_avoidance_walk_core.h; do
    git show 9f764d69:src/proxy/$f > "$H/src/proxy/$f" 2>/dev/null || true
done
git show 9f764d69:verification/probe/ship_steering_host.cpp > "$H/verification/probe/ship_steering_host.cpp"
c++ -std=c++17 -O2 "$H/verification/probe/ship_steering_host.cpp" -o "$H/base"
"$H/base" > "$H/full.txt" 2>/dev/null || true
"$H/base" --capital > "$H/cap.txt" 2>/dev/null || true
X3M_SEQ_OFF=1 "$H/base" > "$H/seqoff.txt" 2>/dev/null || true
"$H/base" --b9 > "$H/b9.txt" 2>/dev/null || true
D=verification/results/ship-steering/convoy-gate-transit
echo "# keyoff_diff.py vs 9f764d69 (the pre-lane fixture), after binary with X3M_LANE_OFF=1 ($LABEL)"
for k in full cap seqoff b9; do
    printf '## %s: ' "$k"
    python3 $D/keyoff_diff.py "$H/$k.txt" "$A/${k}_off.txt"
done
rm -rf "$H"
