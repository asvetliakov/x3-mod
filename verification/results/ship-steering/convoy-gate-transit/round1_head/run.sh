#!/bin/sh
# Lane fix round 1 (Run173; convoy-gate-transit.md section 11): the working tree's scenario C fixture built against the core of the
# reviewed HEAD dd07e95e (-DX3M_LANE_HEAD leaves out the few fixture lines that touch the new core fields) and run; the failing checks
# grouped by cast, time factor and check, and the round-1 measurements of four casts at tf 1. Usage: sh run.sh [fixture argument,
# default --lane] (from the repository root; no Wine, no game).
set -e
ARGS=${1:---lane}
H=$(mktemp -d)
mkdir -p "$H/src/proxy" "$H/verification/probe"
for f in ship_steering_core.h ship_avoidance_core.h ship_gate_ctl_core.h ship_gate_ctl_live_core.h ship_capital_lane_core.h ship_avoidance_walk_core.h; do
    git show dd07e95e:src/proxy/$f > "$H/src/proxy/$f" 2>/dev/null || true
done
cp verification/probe/ship_steering_host.cpp "$H/verification/probe/"
c++ -std=c++17 -O2 -DX3M_LANE_HEAD "$H/verification/probe/ship_steering_host.cpp" -o "$H/head"
echo "== HEAD dd07e95e core, the round-1 fixture ($ARGS)"
"$H/head" $ARGS > "$H/head.txt" 2> "$H/head.err" || true
sed -E 's/^FAIL scenario C ([a-z0-9_]+) tf ([0-9]+) seed [0-9]+: /\1 tf\2: /' "$H/head.err" | sed -E 's/ \(.*//' | sort | uniq -c
tail -2 "$H/head.txt"
grep -E '^scenario C (run173|run173_15km|formed|crossing) lane tf 1 ' "$H/head.txt" | sed -E 's/^scenario C ([a-z0-9_]+) .*(gate warns [^|]*\| nose7 [^|]*)\|.*/\1: \2/'
rm -rf "$H"
