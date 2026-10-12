#!/bin/sh
# Run159 triage (extends ../run158-triage/rows.sh). Usage: sh rows.sh [log, default: the x3m.log beside this script] > rows_out.txt
# One grep extracts the rows to a temp file; the log is never read whole. Writes capital_timeline.txt, capital_phases.txt,
# chain_*.txt, gate_sequence.txt, floor_windows.txt, capcap.txt, circling_*.txt, small_gate_warn.txt, width_effect.txt here.
D=$(cd "$(dirname "$0")" && pwd); L=${1:-$D/x3m.log}
T=$(mktemp); trap 'rm -f "$T"' EXIT
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end |engine_stage )' "$L" > "$T"
echo "== 1 identity"
grep -a '^proxy_identity' "$T" | grep -oE 'sha256=[0-9a-f]+ bytes=[0-9]+|source_commit=[0-9a-f]+'
grep -a '^log_open' "$T"; grep -a '^session_end' "$T"
echo "exception rows in the whole log: $(LC_ALL=C grep -acE 'exception=[1-9]|^exception|unhandled' "$L")"
echo "nonzero overflow fields in the whole log: $(LC_ALL=C grep -acE 'overflow[a-z_]*=[1-9]' "$L")"
grep -aE '^(ship_steering_site|ship_steering_gate_site)' "$T" | grep -oE 'name=\S+ status=\S+' | tr '\n' ' '; echo
grep -a '^ship_steering_gates_capital ' "$T"
echo "seta field: $(grep -a '^engine_stage .*seta=' "$T" | grep -oE 'seta=[01]' | sort | uniq -c | tr '\n' ' ')"
python3 -I "$D/summary.py" "$T"
echo "== census"; grep -a '^ship_capital_census' "$T" | cut -d' ' -f2-
echo "== station rows (root_r_m)"; grep -a '^ship_avoid_station' "$T" | grep -oE 'frame=[0-9]+|obj=\S+|root_r_m=\S+' | paste -sd' '
echo "== latch rows"; grep -a '^ship_gate_capital_latch' "$T" | cut -d' ' -f2-
echo "== 3 ship_avoid_engine_warn_pre"; python3 -I "$D/warn_pre.py" "$T"
echo "== 2 ship_gate_capital_join crossing rows"; grep -a '^ship_gate_capital_join.*cross_nose_deg' "$T" | grep -oE '(frame|obj|class|slot|mode|reason|v_cmd|cross_nose_deg|cross_vel_deg|cross_v|kept|why_not|window_s)=\S+' | paste -sd' ' | sed 's/ frame=/\nframe=/g'
echo "== 7 second gate"; python3 -I "$D/gate2.py" "$T"
python3 -I "$D/gate_sequence.py" "$T" > "$D/gate_sequence.txt"; echo "== gate sequence"; cat "$D/gate_sequence.txt"
python3 -I "$D/floor_windows.py" "$T" > "$D/floor_windows.txt"; echo "== floor windows"; cat "$D/floor_windows.txt"
python3 -I "$D/capcap.py" "$T" > "$D/capcap.txt"; echo "== capital pairs (capcap.txt, head)"; head -4 "$D/capcap.txt" | cut -c1-300
python3 -I "$D/width_effect.py" "$T" > "$D/width_effect.txt"; cat "$D/width_effect.txt"
python3 -I "$D/circling.py" "$T" > "$D/circling_capitals.txt"; python3 -I "$D/circling.py" "$T" --small > "$D/circling_small.txt"
python3 -I "$D/small_gate_warn.py" "$T" 4259,-18037,31134 0,0,1 1105 > "$D/small_gate_warn.txt"
python3 -I "$D/chain.py" "$T" 0xab6c21a0 68500 72800 100 > "$D/chain_ab6c21a0.txt"
python3 -I "$D/chain.py" "$T" 0x91bd58c0 75500 79480 120 > "$D/chain_91bd58c0.txt"
python3 -I "$D/capital_phases.py" "$T" > "$D/capital_phases.txt"; grep -E '^==|totals' "$D/capital_phases.txt" | cut -c1-240
python3 -I "$D/capital_timeline.py" "$T" --legs > "$D/capital_timeline.txt"; grep -E '^==|total|0x1f at' "$D/capital_timeline.txt" | cut -c1-240
