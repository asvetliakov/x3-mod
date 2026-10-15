#!/bin/sh
# Lane fix round 2 review fixes (convoy-gate-transit.md section 11 "Lane fix round 2", review fixes): the working tree's fixture built against
# the core of the reviewed 63b181c4 (-DX3M_LANE_63B1 leaves out the units of the core's new functions: items 3 and 7) and run; the failing
# checks grouped. Usage: sh run.sh [fixture argument, default --lane] (from the repository root; no Wine, no game).
set -e
ARGS=${1:---lane}
H=$(mktemp -d)
mkdir -p "$H/src/proxy" "$H/verification/probe"
for f in ship_steering_core.h ship_avoidance_core.h ship_gate_ctl_core.h ship_gate_ctl_live_core.h ship_capital_lane_core.h ship_avoidance_walk_core.h; do
    git show 63b181c4:src/proxy/$f > "$H/src/proxy/$f" 2>/dev/null || true
done
cp verification/probe/ship_steering_host.cpp "$H/verification/probe/"
c++ -std=c++17 -O2 -DX3M_LANE_63B1 "$H/verification/probe/ship_steering_host.cpp" -o "$H/head"
echo "== 63b181c4 core, the review-fix fixture ($ARGS)"
"$H/head" $ARGS > "$H/head.txt" 2> "$H/head.err" || true
sed -E 's/^FAIL scenario C ([a-z0-9_]+) tf ([0-9]+) seed [0-9]+: /\1 tf\2: /' "$H/head.err" | sed -E 's/^(FAIL lane round 2 review units \(item [0-9]+\)).*/\1/; t; s/ \(.*//' | sort | uniq -c
grep -E '^lane round 2 review units' "$H/head.txt" | cut -c1-220
tail -2 "$H/head.txt"
rm -rf "$H"
