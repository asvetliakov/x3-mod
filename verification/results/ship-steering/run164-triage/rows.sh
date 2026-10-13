#!/bin/sh
# Run164 triage (controller B.3, main 42f7a422): residual "looping". Copied from ../run163-triage/rows.sh and extended (sb_laps_all, sb_wait_all,
# transits, e_evict, evict_fresh, x12_real; sb_track/sb_laps_all normalize fwd). Usage: sh rows.sh [log] [pre-extracted rows file].
# One grep extracts the rows; the log is never read whole.
D=$(cd "$(dirname "$0")" && pwd); L=${1:-$D/x3m.log}
if [ -n "$2" ]; then T=$2; else T=$(mktemp); trap 'rm -f "$T"' EXIT
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end |engine_stage )' "$L" > "$T"; fi
cd "$D" || exit 1
python3 -I caps.py "$T" > caps.txt && python3 -I gates.py "$T" > gates.txt   # gates 0x8e051030 (G -7498,10774,-57569 F -z), 0x8e04ff20 (4259,-18037,31134 F +z)
python3 -I ctl_rows.py "$T" > ctl_rows.txt                         # Q1/Q3 membership transitions
python3 -I cap_orders.py "$T" --min-s 3 > cap_orders.txt           # Q1/Q5 order timelines, 0x12 episodes
python3 -I crowd.py "$T" > crowd.txt                               # Q1/Q5 X crossings: heading error, cross v
python3 -I transits.py "$T" > transits.txt                         # Q1 per-member timeline, commit->launch, peak members per gate
python3 -I sb_timeline.py "$T" > sb_timeline.txt
python3 -I sb_seq.py "$T" 1030 > sb_seq_1030.txt; python3 -I sb_seq.py "$T" ff20 > sb_seq_ff20.txt   # Q2 sequencer per game second
python3 -I sb_laps_all.py "$T" > sb_laps_all.txt                   # Q1/Q2 laps per member stage interval (traced members)
python3 -I sb_wait_all.py "$T" > sb_wait_all.txt                   # Q2 per-reason wait split, EXCESS samples
python3 -I sb_track.py "$T" 0x8e049da0 --step 10 --gate 0x8e051030 > sb_track_25763.txt   # Q2 the one measured petal lap
python3 -I sb_track.py "$T" 0xaa68cfb0 --step 10 --gate 0x8e051030 > sb_track_5315.txt
python3 -I sb_track.py "$T" 0x8e0744a0 --step 10 --gate 0x8e051030 > sb_track_4849.txt
python3 -I sb_track.py "$T" 0xab18e1b0 --step 10 > sb_track_7319.txt
python3 -I e_evict.py "$T" > e_evict.txt && python3 -I evict_fresh.py "$T" > evict_fresh.txt   # Q3/Q4 evictions, own-gate rows, pass_not_fresh
python3 -I x12_real.py "$T" > x12_real.txt                         # Q5 0x12 episodes game/real
python3 -I game_steps.py "$T" > game_steps.txt && python3 -I seta.py "$T" > seta.txt && python3 -I sb_counters.py "$T" > sb_counters.txt
