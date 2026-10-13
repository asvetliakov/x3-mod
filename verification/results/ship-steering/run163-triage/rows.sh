#!/bin/sh
# Run163 triage (controller B.2, main 7fffea7c): the escort stall at 0x8e7ff4f0 after the leader's launch. Copied from ../run162-triage/rows.sh,
# reduced to the scripts copied here. Usage: sh rows.sh [log] [pre-extracted rows file]. One grep extracts the rows; the log is never read whole.
D=$(cd "$(dirname "$0")" && pwd); L=${1:-$D/x3m.log}
if [ -n "$2" ]; then T=$2; else T=$(mktemp); trap 'rm -f "$T"' EXIT
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end |engine_stage )' "$L" > "$T"; fi
cd "$D" || exit 1
python3 -I caps.py "$T" > caps.txt && python3 -I gates.py "$T" > gates.txt
python3 -I ctl_rows.py "$T" > ctl_rows.txt                        # Q1/Q3/Q5 membership transitions
python3 -I cap_orders.py "$T" --min-s 3 > cap_orders.txt          # Q1 order timelines (pointer 0x8e3ad220 is reused by an M6 after f 48700)
python3 -I sb_timeline.py "$T" > sb_timeline.txt                  # Q1 ctl rows + 0x12 warns + arrivals per capital
python3 -I sb_seq.py "$T" f4f0 > sb_seq_f4f0.txt                  # Q2/Q4 the sequencer per game second, run-length grouped
python3 -I sb_seq.py "$T" f9d0 > sb_seq_f9d0.txt
python3 -I sb_wait.py "$T" 0x8e7ff4f0 0x37385fc8 0x8e3b8068 0x8e81c918 > sb_wait.txt   # Q2 per-reason split after the leader's launch
python3 -I sb_gate_e.py "$T" 0x8e7ff4f0 > sb_gate_e.txt           # Q5/Q6 site E vs the own gate per funnel window
python3 -I game_steps.py "$T" > game_steps.txt                    # Q2/Q5 game-time steps > kStateMaxAgeMs (1000 ms)
(for o in 0x37385fc8 0x8e3b8068 0x8e81c918; do python3 -I sb_laps.py "$T" $o 0x8e7ff4f0; done) > sb_laps.txt
python3 -I sb_track.py "$T" 0x8e3b8068 --step 15 --from 65700 > sb_track_4510.txt
python3 -I sb_track.py "$T" 0x8e81c918 --step 15 --from 65700 > sb_track_4956.txt
python3 -I sb_track.py "$T" 0x37385fc8 --step 15 --from 60000 > sb_track_4202.txt
python3 -I sb_counters.py "$T" > sb_counters.txt && python3 -I crowd.py "$T" > crowd.txt && python3 -I seta.py "$T" > seta.txt
cat sb_wait.txt game_steps.txt sb_gate_e.txt
