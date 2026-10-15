#!/bin/sh
# Run177 triage (main 7ae40a13). Extract the ship rows once (~508 MB, keep outside the tree), then run the scripts.
# Usage: sh rows.sh [log] [rows file]
D=$(cd "$(dirname "$0")" && pwd); P=$D/../run175-triage; Q=$D/../run176-triage; L=${1:-$D/x3m-run177.log}; T=${2:-$(mktemp)}
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end |engine_stage |exception|telemetry_summary|scene_graph_census)' "$L" > "$T"
cd "$D" || exit 1
python3 -I $D/../convoy-gate-transit/arr_mode/kill_criterion.py "$T" > kill_criterion177.txt 2>&1                       # Q1/Q2
python3 -I return_trips177.py "$T" > return_trips177.txt                                                                   # Q1 (arrival mode exercised?)
python3 -I convoy_spans177.py "$T" "C1 departure e330|0x9253e330|0x9250bfe0,0x925123d0,0x925397a0" \
  "C2 departure 12b8 (escorts re-led at 25052)|0x925412b8|0x3d7bc3a8,0x9250cd48,0x92560ca8" "C3 departure f440|0x9253f440|0x92547f58,0x1f6a5d90,0xaa464338" \
  "C4 departure f440|0x9253f440|0x3d7b0588,0xaa78ac88,0xaa78bd98" "C5 departure e330 (re-led escorts)|0x9253e330|0xaa796490,0x92560ca8,0x9250cd48" \
  "C6 departure e330 (sector change 27608.5)|0x9253e330|0xaa44f2d8,0xaa796490,0xaa7a5370" "C7 departure 12b8 (sector change 27608.5)|0x925412b8|0xaa79d858,0xaa7939e8,0x9250d360" \
  "C8 departure 4ea0 (open at log end)|0xaa794ea0|0xaa451288,0xaa455320,0xaa44fdd0" > convoy_spans177.txt 2>&1   # Q1 (fails at C7: no launch by log end)
python3 -I $Q/member_fields176.py "$T" > member_fields177.txt                                                             # Q1 funnel/w_ok/own
python3 -I health177.py "$T" > health177.txt                                                                               # Q3
python3 -I $P/warn_chain.py "$T" 0x9250cd48 0x92560ca8 0xaa796490 0x3d7bc3a8 > warn_chain177.txt                          # Q2
python3 -I $Q/pair_changes176.py "$T" 0x9253e330 > pairs_e330.txt                                                         # Q2
for s in 0x9250cd48 0x92560ca8 0xaa796490; do python3 -I $Q/member_lane176.py "$T" $s 0x9253e330 30 25040 26660 > member_lane_$s.txt; done  # Q2
for o in 0x9250cd48 0x92560ca8 0xaa796490 0x3d7bc3a8; do python3 -I $P/order_timeline.py "$T" $o 38000 70000 > order_$o.txt; done          # Q2
python3 -I cast_escort_ahead177.py $D/../../../probe/ship_steering_host.cpp > cast_escort_ahead177.txt                    # Q4
