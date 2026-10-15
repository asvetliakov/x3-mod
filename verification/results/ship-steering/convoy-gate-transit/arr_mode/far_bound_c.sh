#!/bin/sh
# Decision C (2026-10-16; convoy-gate-transit.md section 13 "Implementation"): the funnel's far bound tried as the simple rule (the lane's
# laws, steer and rewrites only within kLaneOwnM = 20 km along F; outside, rank only) on third_gate and side_commit: the fixture built as
# committed (X3M_LANE_FAR 0, the bound parked) and with -DX3M_LANE_FAR=1, every seed's row (X3M_C_ALL=1), then far_bound_c.py.
# Usage, from the repository root: sh verification/results/ship-steering/convoy-gate-transit/arr_mode/far_bound_c.sh (no Wine; ~1 min)
set -e
OUT=$(dirname "$0")
H=$(mktemp -d)
c++ -std=c++17 -O2 -Wall -Wextra -Werror verification/probe/ship_steering_host.cpp -o "$H/off" &
c++ -std=c++17 -O2 -Wall -Wextra -Werror -DX3M_LANE_FAR=1 verification/probe/ship_steering_host.cpp -o "$H/on" &
wait
for b in off on; do for c in third_gate side_commit; do
  X3M_C_ONLY=$c X3M_C_ALL=1 "$H/$b" --lane > "$H/${b}_$c.txt" 2> "$H/${b}_$c.err" &
done; done
wait
python3 -I "$OUT/far_bound_c.py" "$H" > "$OUT/far_bound_c.txt"
rm -rf "$H"
