#!/bin/sh
# Run176 cost triage (main 88eca37a). Extract every row type with fewer than 40000 rows (plus frame_end, ship_trace, engine_stage) once
# (~253 MB, keep outside the tree), then run the cost_ scripts. Usage: sh cost_rows.sh [log] [rows file]
D=$(cd "$(dirname "$0")" && pwd); L=${1:-$D/x3m-run176.log}; T=${2:-$(mktemp)}; C=$(mktemp)
LC_ALL=C awk '{c[$1]++} END{for(k in c) print c[k], k}' "$L" > "$C"
LC_ALL=C awk 'NR==FNR{if($1>=40000 && $2!="frame_end" && $2!="ship_trace" && $2!="engine_stage") skip[$2]=1; next} !($1 in skip)' "$C" "$L" > "$T"
cd "$D" || exit 1
python3 -I cost_frames176.py "$T" > cost_frames176.txt          # Q1/Q3: slow-row context, intervals, controller-max window
python3 -I cost_launch176.py "$T" > cost_launch176.txt          # Q1: launches against slow rows
python3 -I cost_windows176.py "$T" > cost_windows176.txt        # Q1: summary counters around each slow row
python3 -I cost_ring176.py "$T" > cost_ring176.txt              # Q2: ring_node tallies, re-read history
python3 -I cost_ring_player176.py "$T" > cost_ring_player176.txt # Q2: node count vs player-gate distance
