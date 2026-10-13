#!/bin/sh
# Run161 triage (stage A + docking amendment 18), copied from ../run160b-triage/rows.sh. Optional 2nd arg: pre-extracted rows file.
# Run160b (flight 2, capital crowd) triage, copied from ../run160-triage/rows.sh and extended. Usage: sh rows.sh [log] > rows_out.txt
# One grep extracts the rows to a temp file; the log is never read whole. Writes capital_timeline.txt, capital_phases.txt,
# chain_*.txt, gate_sequence.txt, floor_windows.txt, capcap.txt, circling_*.txt, small_gate_warn.txt, width_effect.txt here.
D=$(cd "$(dirname "$0")" && pwd); L=${1:-$D/x3m.log}
if [ -n "$2" ]; then T=$2; else T=$(mktemp); trap 'rm -f "$T"' EXIT
LC_ALL=C grep -aE '^(ship_|proxy_identity|log_open|session_end|frame_end |engine_stage )' "$L" > "$T"; fi
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
python3 -I "$D/gates.py" "$T" > "$D/gates.txt"; echo "== gates"; cat "$D/gates.txt"
python3 -I "$D/small_gate_warn.py" "$T" "$D/gates.txt" > "$D/small_gate_warn.txt"
python3 -I "$D/caps.py" "$T" > "$D/caps.txt"; echo "== capitals"; cat "$D/caps.txt"
python3 -I "$D/counters.py" "$T" > "$D/counters.txt"; echo "== counters"; cat "$D/counters.txt"
(cd "$D" && python3 -I cap_orders.py "$T" --min-s 3 > cap_orders.txt && python3 -I episodes12.py "$T" > episodes12.txt && python3 -I crowd.py "$T" > crowd.txt \
  && python3 -I arrival.py "$T" > arrival.txt && python3 -I near.py "$T" gates.txt caps.txt > near.txt && python3 -I waits.py "$T" gates.txt > waits.txt \ && python3 -I circ_cause.py "$T" circling_small.txt > circ_cause.txt \ && python3 -I seta.py "$T" > seta.txt)
echo "== crowd"; cat "$D/crowd.txt"; echo "== 0x12 episodes"; tail -1 "$D/episodes12.txt"
python3 -I "$D/capital_phases.py" "$T" > "$D/capital_phases.txt"; grep -E '^==|totals' "$D/capital_phases.txt" | cut -c1-240
python3 -I "$D/capital_timeline.py" "$T" --legs > "$D/capital_timeline.txt"; grep -E '^==|total|0x1f at' "$D/capital_timeline.txt" | cut -c1-240
# Run161 additions (stage A controller, D.4 arrivals, docking amendment 18)
(cd "$D" && python3 -I capcap_warns.py "$T" > capcap_warns.txt && python3 -I capcap_e.py "$T" > capcap_e.txt \
  && python3 -I ep12_where.py "$T" gates.txt episodes12.txt > ep12_where.txt && (python3 -I ep12_cause.py episodes12.txt; python3 -I ep12_cause.py ../run160b-triage/episodes12.txt) > ep12_cause.txt \
  && python3 -I clunk.py "$T" gates.txt > clunk_capitals.txt && python3 -I clunk.py "$T" gates.txt --small > clunk_small.txt \
  && (python3 -I cap_speed6r.py "$T" gates.txt; python3 -I cap_speed6r.py "$T" gates.txt --r 10) > cap_speed6r.txt \
  && python3 -I warn_subject_order.py "$T" > warn_subject_order.txt && python3 -I dock.py "$T" > dock.txt \
  && python3 -I ctl_churn.py "$T" > ctl_churn.txt && python3 -I ctl_counters.py "$T" > ctl_counters.txt \
  && (python3 -I holds.py cap_orders.txt; python3 -I holds.py ../run160b-triage/cap_orders.txt) > holds.txt \
  && (python3 -I circ_tally.py circ_cause.txt; python3 -I circ_tally.py ../run160b-triage/circ_cause.txt) > circ_tally.txt \
  && python3 -I small_o4_gate.py "$T" gates.txt > small_o4_gate.txt \
  && python3 -I frame_time.py "$T" 12517 40739 50028 61440 63456 63697 64157 64921 65757 66195 66990 69039 > frame_times.txt)
echo "== Run161 additions"; cat "$D/ep12_cause.txt" "$D/warn_subject_order.txt" "$D/ctl_churn.txt" "$D/holds.txt" "$D/circ_tally.txt" "$D/small_o4_gate.txt"
grep -E 'distinct|order before' "$D/clunk_capitals.txt" "$D/clunk_small.txt"
# Run160b comparison for clunk.py (needs the Run160b rows): clunk_small_run160b.txt / clunk_capitals_run160b.txt were produced with
#   (cd ../run160b-triage && python3 -I ../run161-triage/clunk.py <run160b rows> gates.txt [--small])
