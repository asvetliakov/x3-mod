#!/bin/sh
# Run158 triage. Usage: sh rows.sh [/mnt/c/X3/x3m.log] > rows_out.txt
# One grep extracts the rows to a temp file; the log is never read whole. Run157/156 comparisons: ../run157-triage/rows_out.txt, ../run156-triage/rows_out.txt
L=${1:-/mnt/c/X3/x3m.log}; D=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp); trap 'rm -f "$T"' EXIT
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end |engine_stage )' "$L" > "$T"
echo "== 1 identity"
grep -a '^proxy_identity' "$T" | grep -oE 'sha256=[0-9a-f]+ bytes=[0-9]+|source_commit=[0-9a-f]+'
grep -a '^log_open' "$T"; grep -a '^session_end' "$T"
echo "exception rows in the whole log: $(LC_ALL=C grep -acE 'exception=[1-9]|^exception|unhandled' "$L")"
echo "crash files newer than x3m.prev.log:"; find /mnt/c/X3 -maxdepth 2 -iname '*crash*' -newer /mnt/c/X3/x3m.prev.log 2>/dev/null
grep -aE '^(ship_steering_site|ship_steering_gate_site)' "$T" | grep -oE 'name=\S+ status=\S+' | tr '\n' ' '; echo
echo "seta field: $(grep -a '^engine_stage .*seta=' "$T" | grep -oE 'seta=[01]' | sort | uniq -c | tr '\n' ' ')"
python3 -I "$D/summary.py" "$T"
echo "== 3 ship_avoid_engine_warn_pre"; python3 -I "$D/warn_pre.py" "$T"
echo "== 2 ship_gate_capital_join crossing rows"; grep -a '^ship_gate_capital_join.*cross_nose_deg' "$T" | grep -oE '(frame|obj|class|slot|mode|reason|v_cmd|cross_nose_deg|cross_vel_deg|cross_v|kept|why_not|window_s)=\S+' | paste -sd' ' | sed 's/ frame=/\nframe=/g'
echo "== 7 second gate"; python3 -I "$D/gate2.py" "$T"
python3 -I "$D/capital_timeline.py" "$T" --legs > "$D/capital_timeline.txt"; grep -E '^==|total|0x1f at' "$D/capital_timeline.txt" | cut -c1-240
