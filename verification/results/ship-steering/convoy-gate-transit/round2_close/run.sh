#!/bin/sh
# Lane fix round 2, the closing items (convoy-gate-transit.md section 11 "Lane fix round 2"): the working tree's scenario C fixture built
# against the core of the reviewed WIP HEAD 5d5c5e69 (-DX3M_LANE_5D5C leaves out the one fixture line that needs the new core) and run; the
# failing checks grouped by cast, time factor and check. Usage: sh run.sh [fixture argument, default --lane] (from the repository root; no
# Wine, no game).
set -e
ARGS=${1:---lane}
H=$(mktemp -d)
mkdir -p "$H/src/proxy" "$H/verification/probe"
for f in ship_steering_core.h ship_avoidance_core.h ship_gate_ctl_core.h ship_gate_ctl_live_core.h ship_capital_lane_core.h ship_avoidance_walk_core.h; do
    git show 5d5c5e69:src/proxy/$f > "$H/src/proxy/$f" 2>/dev/null || true
done
cp verification/probe/ship_steering_host.cpp "$H/verification/probe/"
c++ -std=c++17 -O2 -DX3M_LANE_5D5C "$H/verification/probe/ship_steering_host.cpp" -o "$H/head"
echo "== 5d5c5e69 core, the round-2 closing fixture ($ARGS)"
"$H/head" $ARGS > "$H/head.txt" 2> "$H/head.err" || true
sed -E 's/^FAIL scenario C ([a-z0-9_]+) tf ([0-9]+) seed [0-9]+: /\1 tf\2: /' "$H/head.err" | sed -E 's/ \(.*//' | sort | uniq -c
tail -2 "$H/head.txt"
rm -rf "$H"
