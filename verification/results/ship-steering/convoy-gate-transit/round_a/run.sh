#!/bin/sh
# Round A (convoy-gate-transit.md section 11 "Round A (rank 4 + arrival pair)"): the working tree's scenario C fixture on the working
# tree's core, every seed printed; the parked arrival_parked cast re-enabled and run alone; the controller cost bench (--traffic).
# Outputs (summaries only, no raw logs): lane.txt (the checks line, the failures, the spans, the cost rows), arrival_pass.txt and
# arrival_parked.txt (the engine-path rows of every seed, the failures), bench.txt (the ctl cost bench's decision rows), rows.txt (the
# per-seed figures of the casts the round touched). The before/after spans table (spans_diff.py) needs a base run's stdout:
# sh run.sh [<base --lane stdout>]. Usage from the repository root; no Wine, no game; about 2 min on the host.
set -e
OUT=$(dirname "$0")
H=$(mktemp -d)
mkdir -p "$H/src/proxy" "$H/verification/probe"
cp src/proxy/*.h "$H/src/proxy/"
cp verification/probe/ship_steering_host.cpp "$H/verification/probe/host.cpp"
sed -E 's#^    // PARKED: (\{"[a-z0-9_]+".*\},)$#    \1#' verification/probe/ship_steering_host.cpp > "$H/verification/probe/parked.cpp"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$H/verification/probe/host.cpp" -o "$H/host"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$H/verification/probe/parked.cpp" -o "$H/parked"
X3M_C_ALL=1 "$H/host" --lane > "$H/lane.txt" 2> "$H/lane.err" || true
X3M_C_ONLY=arrival_parked X3M_C_ENGALL=1 "$H/parked" --lane > "$H/arrival_parked.txt" 2> "$H/arrival_parked.err" || true
X3M_C_ONLY=arrival_pass X3M_C_ENGALL=1 "$H/host" --lane > "$H/arrival_pass.txt" 2> "$H/arrival_pass.err" || true
"$H/host" --traffic > "$H/traffic.txt" 2> "$H/traffic.err" || true
python3 -I "$OUT/summarise.py" "$H/lane.txt" "$H/lane.err" > "$OUT/lane.txt"
python3 -I "$OUT/summarise.py" "$H/arrival_pass.txt" "$H/arrival_pass.err" arrival_pass > "$OUT/arrival_pass.txt"
python3 -I "$OUT/summarise.py" "$H/arrival_parked.txt" "$H/arrival_parked.err" arrival_parked > "$OUT/arrival_parked.txt"
python3 -I "$OUT/rows.py" "$H/lane.txt" abreast3 abreast5 terran_abreast slow_hull redeparture p2_miss p2_miss_near overshoot_near parked > "$OUT/rows.txt"
grep -E '^ctl cost \(host\)' "$H/traffic.txt" > "$OUT/bench.txt"
if [ -n "$1" ]; then python3 -I "$OUT/spans_diff.py" "$1" "$H/lane.txt" --md > "$OUT/spans_diff.md"; fi
rm -rf "$H"
