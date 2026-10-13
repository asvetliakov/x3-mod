#!/bin/sh
# Gate traffic controller design, Q7: per-frame demand of the diagnostic rings in the Run160b crowd window (frames
# 52900-61300, where gate_rows_dropped rose by 4085). Rows per frame by kind and the histogram for the E verdict rows,
# whose ring writes at most 8 per frame (kERows). Usage: sh ring_demand.sh [log] > ring_demand.txt
L=${1:-$(dirname "$0")/../run160b-triage/x3m.log}
LC_ALL=C grep -a -oE '^(ship_avoid_e_verdict|ship_avoid_engine_warn_pre|ship_avoid_engine_warn|ship_capital_lane|ship_gate_[a-z_]+|ship_trace) frame=[0-9]+' "$L" |
awk '{split($2,a,"="); f=a[2]; if (f>=52900 && f<=61300) {k=$1" "f; c[k]++}}
END {for (k in c) {split(k,b," "); n[b[1]]++; s[b[1]]+=c[k]; if (c[k]>m[b[1]]) m[b[1]]=c[k]; if (b[1]=="ship_avoid_e_verdict") h[c[k]]++}
     for (t in n) printf "%s frames %d rows %d max_per_frame %d\n", t, n[t], s[t], m[t];
     for (i=1;i<=8;i++) printf "e_verdict frames with %d rows: %d\n", i, h[i]}' | sort
