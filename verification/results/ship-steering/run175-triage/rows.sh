#!/bin/sh
# Run175 A triage (main 6c3b18f9, DLL a7241137..., the arrival round). Extract the ship rows once (~306 MB, keep outside the tree), then run
# the scripts on it. Usage: sh rows.sh [log] [rows file]
D=$(cd "$(dirname "$0")" && pwd); L=${1:-$D/x3m-run175a.log}; T=${2:-$(mktemp)}
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end |engine_stage |exception|telemetry_summary|scene_graph_census)' "$L" > "$T"
cd "$D" || exit 1
python3 -I convoy_spans175.py "$T" > convoy_spans175.txt          # Q1
python3 -I health175.py "$T" > health175.txt                      # Q5
python3 -I ring_player_grid.py "$T" > ring_player_grid.txt        # Q2 (grid rewrites), Q3, Q4
for o in 0x3a40dde0 0x9171ba50 0x91292280; do python3 -I order_timeline.py "$T" $o > order_$o.txt; done   # Q2
python3 -I member_lane.py "$T" 0x3a40dde0 0x916fd518 15 > member_lane_3a40dde0.txt                       # Q2: the Cerberus at fd518
python3 -I member_lane.py "$T" 0x91292280 0x916fcb58 10 > member_lane_91292280.txt                       # convoy 3's leader
python3 -I order4_point.py "$T" 0x3a40dde0 0x916fd518 5 > order4_point_3a40dde0.txt                      # Q2: d to the order point A
python3 -I warn_chain.py "$T" 0x91279188 0x91281db0 0x916f6018 0x91293870 0x9171ba50 0x3a40dde0 0x91292280 0x9170c2e8 0x916f9348 > warn_chain.txt
