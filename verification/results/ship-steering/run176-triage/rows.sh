#!/bin/sh
# Run176 triage (main 88eca37a, DLL ea328f11...). Extract the ship rows once (~248 MB, keep outside the tree), then run the scripts.
# Usage: sh rows.sh [log] [rows file]
D=$(cd "$(dirname "$0")" && pwd); P=$D/../run175-triage; L=${1:-$D/x3m-run176.log}; T=${2:-$(mktemp)}
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end |engine_stage |exception|telemetry_summary|scene_graph_census)' "$L" > "$T"
cd "$D" || exit 1
python3 -I $P/b_convoy_spans.py "$T" "C1 departure 178e8|0x90a178e8|0x905a50e0,0x905ac718,0x90a14e40" \
  "C2 arrival 178e8 -> 18038|0x90a18038|0x90598ca8,0x39fcdc10,0x90a39150" "C3 return 178e8 -> 178e8|0x90a178e8|0x905b2760,0x30edeff8,0xa89ccf60" \
  "C4 return 18038 -> 1c340 (no launch by log end)|0x90a1c340|0xa9042380,0x39fcdc10,0x90a39150" > convoy_spans176.txt 2>&1   # Q1 (the script's last line fails: C4 never launched)
python3 -I health176.py "$T" > health176.txt                                                                                  # Q5
python3 -I $P/ring_player_grid.py "$T" > ring_player_grid176.txt                                                              # Q1 grid, Q5
python3 -I $P/warn_chain.py "$T" 0x905a50e0 0x905ac718 0x90a14e40 0x90598ca8 0x39fcdc10 0x90a39150 0x905b2760 0x30edeff8 0xa89ccf60 0xa9042380 > warn_chain176.txt
python3 -I member_fields176.py "$T" > member_fields176.txt                                                                    # Q1/Q4
python3 -I pair_changes176.py "$T" 0x90a178e8 > pairs_178e8.txt                                                               # Q2
python3 -I c3_mouth176.py "$T" 33640 34560 > c3_mouth176.txt                                                                  # Q2 (eff8 at the exit point, cf60's arrival into it)
for s in 0x905b2760 0x30edeff8 0xa89ccf60; do python3 -I member_lane176.py "$T" $s 0x90a178e8 15 > member_lane_$s.txt; done  # Q2
for s in 0x39fcdc10 0x90a39150; do python3 -I member_lane176.py "$T" $s 0x90a18038 20 25100 25300 > member_lane_${s}_18038.txt
  python3 -I member_lane176.py "$T" $s 0x90a1c340 20 > member_lane_${s}_1c340.txt; done                                         # Q3
for o in 0xa9042380 0x39fcdc10 0x90a39150; do python3 -I $P/order_timeline.py "$T" $o 45800 47800 > order_$o.txt; done       # Q3
grep -aE '^(ship_gate_ctl |ship_gate_ctl_merge|ship_gate_ctl_rewrite|ship_avoid_engine_warn_pre|ship_steering_handler_slow|ship_gate_ring_read|ship_gate_ring_reread)' "$T" > ctl_rows176.txt  # Q1/Q2/Q5
