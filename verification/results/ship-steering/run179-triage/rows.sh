#!/bin/sh
# Run179 triage (main 43c9ea11: Run178's behaviour + defaults on + cleanup). Extract the ship rows once (~508 MB, keep outside the tree),
# a per-ship subset and a combined subset, then run the scripts. Usage: sh rows.sh [log] [scratch dir]
D=$(cd "$(dirname "$0")" && pwd); P=$D/../run175-triage; Q=$D/../run176-triage; E=$D/../run178-triage; L=${1:-$D/x3m-run179.log}; T=${2:-$(mktemp -d)}
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end |engine_stage |exception|telemetry_summary|scene_graph_census)' "$L" > $T/rows179.txt
ID='0x(b71ab120|1f8a8c08|1f8a7fd8|90c4c7d0|910f2fb8|910d5578|90c626d0|38a08400|90c63b88|91102378)'
LC_ALL=C grep -aE "$ID" $T/rows179.txt | LC_ALL=C grep -av '^ship_trace ' > $T/sub.txt
LC_ALL=C grep -aE '0x(b71ab120|1f8a8c08|1f8a7fd8|90c4c7d0|910f2fb8|910d5578|90c626d0|38a08400|90c63b88|91102378|90c64068|90c59490)|^(ship_steering_summary|ship_steering_lane_summary|session_end|log_open)' $T/rows179.txt > $T/comb.txt
cd "$D" || exit 1
python3 -I $D/../convoy-gate-transit/arr_mode/kill_criterion.py $T/rows179.txt > kill_criterion179.txt 2>&1      # the brief's numbers
python3 -I stops179.py $T/sub.txt 0x910d9130 27150 28400 0xb71ab120,0x1f8a8c08,0x1f8a7fd8 > stops179_d9130.txt       # Q1 convoy d9130 27156
python3 -I stops179.py $T/sub.txt 0x910dbf80 27280 28200 0x90c4c7d0,0x910f2fb8,0x910d5578 > stops179_dbf80.txt       # Q1 convoy dbf80 27284.9
python3 -I stops179.py $T/sub.txt 0x910dbf80 25950 27160 0x90c626d0,0x1f8a8c08,0x1f8a7fd8 > stops179_dbf80_open.txt  # Q3
for s in 0x1f8a7fd8 0x1f8a8c08 0xb71ab120; do python3 -I $Q/member_lane176.py $T/sub.txt $s 0x910d9130 30 27150 28400 > member_lane_$s.txt; done
for s in 0x90c4c7d0 0x910f2fb8 0x910d5578; do python3 -I $Q/member_lane176.py $T/sub.txt $s 0x910dbf80 30 27280 28200 > member_lane_$s.txt; done
M=0x90c4c7d0,0x1f8a7fd8; R=0x38a08400,0x90c63b88,0x91102378
(echo "## member pair"; python3 -I rt_x12_spans179.py $T/comb.txt $M; echo "## return trip"; python3 -I rt_x12_spans179.py $T/comb.txt $R) > x12_spans179.txt 2>&1   # Q1/Q2
(echo "## member pair"; python3 -I rt_verdicts179.py $T/comb.txt $M; echo "## return trip"; python3 -I rt_verdicts179.py $T/comb.txt $R) > verdicts179.txt 2>&1      # Q1/Q2
(echo "## member pair (window 30 s + 2.0 s step)"; python3 -I rt_writes_before179.py $T/comb.txt 0x90c4c7d0,0x1f8a7fd8,0xb71ab120 32; echo "## return trip"; python3 -I rt_writes_before179.py $T/comb.txt $R 32) > writes_before179.txt 2>&1
python3 -I rt_writes179.py $T/comb.txt $R > rt_writes179.txt 2>&1                                                    # Q2 our avoidance on the three
for o in 0x38a08400 0x90c63b88 0x91102378; do python3 -I $P/order_timeline.py $T/comb.txt $o 25400 118404 > order_$o.txt; done   # Q2
(echo "## return trip frames 25451..30400"; python3 -I closest179.py $T/rows179.txt $R 25451 30400; echo "## flagged convoys frames 79399..99394"; python3 -I closest179.py $T/rows179.txt 0xb71ab120,0x1f8a8c08,0x1f8a7fd8,0x90c4c7d0,0x910f2fb8,0x910d5578 79399 99394) > closest179.txt
python3 -I $E/open_convoys178.py $T/comb.txt 0x910dbf80:0x90c626d0,0x1f8a8c08,0x1f8a7fd8 > open_convoys179.txt 2>&1  # Q3
python3 -I health179.py $T/rows179.txt > health179.txt 2>&1                                                           # Q4
python3 -I floor179.py $T/sub.txt 0x1f8a7fd8:0x910d9130 0x1f8a8c08:0x910d9130 0x910d5578:0x910dbf80 0x910f2fb8:0x910dbf80 > floor179.txt   # Q1 floor crawls
