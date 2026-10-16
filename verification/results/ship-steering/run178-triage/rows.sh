#!/bin/sh
# Run178 triage (main 6a1ad525). Extract the ship rows once (~244 MB, keep outside the tree), then run the scripts.
# Usage: sh rows.sh [log] [rows file]
D=$(cd "$(dirname "$0")" && pwd); P=$D/../run175-triage; Q=$D/../run176-triage; V=$D/../run177-triage; L=${1:-$D/x3m-run178.log}; T=${2:-$(mktemp)}
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end |engine_stage |exception|telemetry_summary|scene_graph_census)' "$L" > "$T"
cd "$D" || exit 1
python3 -I $D/../convoy-gate-transit/arr_mode/kill_criterion.py "$T" > kill_criterion178.txt 2>&1                        # Q1 (its "last launch open" is jump()'s board-only test)
for o in 0x8fe612b8 0x8fe58420 0x39cfa520 0x39cf2180; do python3 -I $P/order_timeline.py "$T" $o 32000 70000 > order_$o.txt; done  # Q1 orders
python3 -I rt_x12_spans178.py "$T" > rt_x12_spans178.txt          # Q1/Q2 the 10 episodes, spans
python3 -I rt_verdicts178.py "$T" > rt_verdicts178.txt            # Q1 site E per episode
python3 -I rt_writes178.py "$T" > rt_writes178.txt                # Q1 our avoidance on the four (trace point/obst, detour rows, site E verdicts)
python3 -I rt_detour_runs178.py "$T" > rt_detour_runs178.txt      # Q1 detour runs and the next 0x12
python3 -I rt_writes_before178.py "$T" > rt_writes_before178.txt  # Q1 writes of ours on A/B in the 32 s before each episode
python3 -I rt_escort_legs178.py "$T" > rt_escort_legs178.txt      # Q1 the escorts' 0x14 legs toward the leader
python3 -I rt_coverage178.py "$T" > rt_coverage178.txt            # Q3 trace coverage at each warn
python3 -I rt_sweep178.py "$T" > rt_sweep178.txt                  # Q2/Q3 the capsule sweep at each warn, closest approach
python3 -I rt_range178.py "$T" > rt_range178.txt                  # Q3 the range-rate bound per episode
python3 -I $V/convoy_spans177.py "$T" "C1 departure 09d0|0x902809d0|0x8fe65bd8,0x8fe6d0d8,0x9027b5b8" > convoy_spans178.txt 2>&1   # Q4
python3 -I open_convoys178.py "$T" 0x902803b8:0x8fe719f8,0x902a0018,0xaca45c70 0x90281258:0x9027ae68,0x36e24428,0xaca4b568 > open_convoys178.txt  # Q4
python3 -I health178.py "$T" > health178.txt                      # Q4
