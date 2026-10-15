#!/bin/sh
# The minimal arrival mode against its level-0 twin over 20 seeds x tf 1 / 6 / 10 (convoy-gate-transit.md section 13 "Implementation"):
# every mode cast with X3M_C_SEEDN=20 (one process per cast and tf), the fixture's mode lines into a temporary file, then stat_compare.py
# (stat_compare.txt). Usage, from the repository root: sh verification/results/ship-steering/convoy-gate-transit/arr_mode/stat.sh
# (no Wine, no game; about 1 minute on 16 cores)
set -e
OUT=$(dirname "$0")
H=$(mktemp -d)
c++ -std=c++17 -O2 -Wall -Wextra -Werror verification/probe/ship_steering_host.cpp -o "$H/host"
for c in rt_titan return_trip rt_titan_rest rt_hub return_pass; do for tf in 1 6 10; do
  X3M_C_ONLY=$c X3M_C_TF=$tf X3M_C_SEEDN=20 X3M_C_ALL=1 "$H/host" --lane 2>/dev/null | grep ' mode tf ' > "$H/st_${c}_$tf.txt" &
done; done
wait
cat "$H"/st_*.txt > "$H/stat.txt"
python3 -I "$OUT/stat_compare.py" "$H/stat.txt" > "$OUT/stat_compare.txt"
rm -rf "$H"
