#!/bin/sh
# Run174 triage (main 564b3df8, convoy lane fix rounds 1-2). Extract the ship rows once (232 MB, keep outside the tree), then run the
# scripts on it. Usage: sh rows.sh [log] [rows file]
D=$(cd "$(dirname "$0")" && pwd); L=${1:-$D/x3m-run174.log}; T=${2:-$(mktemp)}
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end |engine_stage |exception|telemetry_summary|scene_graph_census)' "$L" > "$T"
cd "$D" || exit 1
python3 -I ctl_rows.py "$T" > ctl_rows.txt
python3 -I order_timeline.py "$T" 0x39e43950 > order_leader2.txt
for o in 0x90448e28 0x9045d390; do python3 -I order_timeline.py "$T" $o > order_$o.txt; done
python3 -I warn_chain.py "$T" 0x39e43950 0x90448e28 0x9045d390 0x39e2f3e8 0x2f1a9290 0xa9f90b18 0x8ffc7c30 0x8ffced88 0x904238d0 > warn_chain.txt
python3 -I target_leaves.py "$T" > target_leaves.txt
python3 -I convoy_spans174.py "$T" > convoy_spans174.txt
python3 -I health174.py "$T" > health174.txt
python3 -I leader_alone.py "$T" > leader_alone.txt && python3 -I leader_alone_points.py "$T" > leader_alone_points.txt
python3 -I leader_redep.py "$T" > leader_redep.txt
