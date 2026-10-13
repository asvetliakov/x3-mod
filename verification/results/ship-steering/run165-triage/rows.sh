#!/bin/sh
# Run165 triage (controller B.4, main 3c627a55): "circling with 5 capitals" + the crash at the log end. Copied from ../run164-triage and
# extended (members.py, hold_wait.py, funnel_idle.py, small_gap.py, hold_funnel.py; window_runs inline in the report). Gates this run:
# 0x8f9a4e08 (G -7498,10774,-57569 F -z), 0x8f9a5f18 (4259,-18037,31134 F +z). Usage: sh rows.sh [log] [pre-extracted rows file].
D=$(cd "$(dirname "$0")" && pwd); L=${1:-$D/x3m.log}
if [ -n "$2" ]; then T=$2; else T=$(mktemp); trap 'rm -f "$T"' EXIT
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end |engine_stage )' "$L" > "$T"; fi
cd "$D" || exit 1
python3 -I caps.py "$T" > caps.txt && python3 -I gates.py "$T" > gates.txt
python3 -I ctl_rows.py "$T" > ctl_rows.txt                         # Q1 membership transitions
python3 -I cap_orders.py "$T" --min-s 3 > cap_orders.txt           # Q1/Q3 order timelines, 0x12 episodes
python3 -I crowd.py "$T" > crowd.txt && python3 -I transits.py "$T" > transits.txt   # Q1/Q4 crossings, commit->launch, span
python3 -I sb_timeline.py "$T" > sb_timeline.txt
python3 -I sb_seq.py "$T" 5f18 > sb_seq_5f18.txt; python3 -I sb_seq.py "$T" 4e08 > sb_seq_4e08.txt   # Q2 sequencer per game second
python3 -I sb_laps_all.py "$T" > sb_laps_all.txt
python3 -I members.py "$T" > members.txt && python3 -I hold_funnel.py "$T" >> members.txt   # Q1 per-member stages, petal laps/radius, board counts
python3 -I hold_wait.py "$T" > hold_wait.txt                       # Q2 every hold second by reason; launch -> next release
python3 -I funnel_idle.py "$T" > funnel_idle.txt                   # Q2 funnel free with holders, by reason
python3 -I window_runs.py "$T" > window_runs.txt                   # Q2 funnel free, head out of window: runs
python3 -I e_evict.py "$T" > e_evict.txt && python3 -I x12_real.py "$T" > x12_real.txt   # Q3 evictions, own-gate E rows, 0x12
python3 -I small_gap.py "$T" > small_gap.txt                       # Q3 e_small_gap
python3 -I game_steps.py "$T" > game_steps.txt && python3 -I seta.py "$T" > seta.txt && python3 -I sb_counters.py "$T" > sb_counters.txt
# Q3 crash: tail of the log (exception row), symbolised against the installed unstripped DLL (ImageBase 0x6fb40000 + offset):
#   tail -c 1500 x3m.log; i686-w64-mingw32-addr2line -f -i -C -e /mnt/c/X3/d3d9.dll 0x6fbe5b03
