#!/bin/sh
# Run175 B triage (second session, same build: main 6c3b18f9, DLL a7241137...). Extract the ship rows once (~430 MB, keep outside the tree),
# then run the session-a scripts (parametrised by the rows file) and the b_ scripts on it. Usage: sh b_rows.sh [log] [rows file]
D=$(cd "$(dirname "$0")" && pwd); L=${1:-$D/x3m-run175b.log}; T=${2:-$(mktemp)}
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end |engine_stage |exception|telemetry_summary|scene_graph_census)' "$L" > "$T"
cd "$D" || exit 1
python3 -I b_convoy_spans.py "$T" "C1 departure f0e8|0x9048f0e8|0x9045a080,0x90462558,0x90488bc0" \
  "C2 arrival f0e8 -> dd68|0x9048dd68|0x39d49068,0x904af840,0x9048a420" "C3 arrival f0e8 -> 0948|0x90490948|0x9046d268,0x9045f498,0x90468468" \
  "C3 return 0948 -> f0e8 (leader 0xab8431e8)|0x9048f0e8|0xab8431e8,0x9045f498,0x90468468" \
  "C4 arrival f0e8 -> dd68|0x9048dd68|0x39d549a8,0xab849ab8,0xab84e8b8" > b_convoy_spans.txt                     # Q1
python3 -I health175.py "$T" | grep -v '^convoy\|^Cerberus' > b_health.txt                                       # Q5 (session-a windows dropped)
python3 -I spike_corr.py "$L" > b_spike_corr.txt; python3 -I spike_window.py "$L" 48121-48253 > b_spike_window.txt # Q5
python3 -I ring_player_grid.py "$T" > b_ring_player_grid.txt                                                     # Q1 grid, Q4, Q5
python3 -I warn_chain.py "$T" 0x9045a080 0x90462558 0x90488bc0 0x39d49068 0x904af840 0x9048a420 0x9046d268 0x9045f498 0x90468468 \
  0xab8431e8 0x39d549a8 0xab849ab8 0xab84e8b8 > b_warn_chain.txt                                                 # Q1/Q2 0x12
for o in 0x9046d268 0x9045f498 0x90468468 0xab8431e8 0x39d549a8 0xab849ab8 0xab84e8b8; do python3 -I order_timeline.py "$T" $o > b_order_$o.txt; done
for a in 0x39d549a8:0x9048dd68 0xab849ab8:0x9048dd68 0xab84e8b8:0x9048dd68 0x9045f498:0x90490948 0x90468468:0x90490948 0x9046d268:0x90490948; do
  python3 -I member_lane.py "$T" ${a%%:*} ${a##*:} 20 > b_member_lane_${a%%:*}.txt; done                        # Q2 (dd68 = C4, 0948 = C3)
python3 -I member_lane.py "$T" 0x9045f498 0x9048f0e8 30 > b_member_lane_9045f498_f0e8.txt                         # Q2 C3 return
python3 -I member_lane.py "$T" 0x90468468 0x9048f0e8 30 > b_member_lane_90468468_f0e8.txt
python3 -I member_lane.py "$T" 0xab8431e8 0x9048f0e8 30 > b_member_lane_ab8431e8.txt
python3 -I b_arrivals.py "$T" > b_arrivals.txt                                                                    # Q3
python3 -I b_plane_cross.py "$T" > b_plane_cross.txt                                                              # Q3/Q4
python3 -I b_field_check.py "$T" 0x9045a080,0x90462558,0x90488bc0,0x39d49068,0x904af840,0x9048a420,0x9046d268,0x9045f498,0x90468468,0xab8431e8,0x39d549a8,0xab849ab8,0xab84e8b8 > b_field_check.txt  # Q4
grep -a '^ship_gate_ctl_lane' "$T" | grep -a 'ship=0x9045f498' | grep -a 'gate=0x90490948' | awk '{split($3,a,"="); t=a[2]/1000; if (t>=26085 && t<=26272) print}' | awk 'NR%3==1' > b_f498_0948_lane.txt  # Q2: escort-gate stop at s=-2350, lat 18.6 km
