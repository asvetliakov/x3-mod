#!/bin/sh
# Run 225 black main menu after exit: queries used (log is untracked, 460 MB).
L=${1:-/mnt/c/X3/x3m.log}
# rare row inventory (first/last line)
awk '{n=$1;c[n]++;if(!(n in f))f[n]=NR;l[n]=NR}END{for(n in c)if(c[n]<200)print c[n],f[n],l[n],n}' "$L" | sort -k2 -n | tail -80
# transition: loading, window, reset rows
grep -a -n -E '^(loading_phase|reset_begin|reset_end|telemetry_presentation|window_mode|own_sun_frame|telemetry_first_present)|WM_ACTIVATEAPP|WM_DISPLAYCHANGE|iconic=' "$L" | sed -n '1,400p' | cut -c1-220 | tail -40
# present result runs from frame_end
awk '$1=="frame_end"{match($0,/ frame=[0-9]+/);f=substr($0,RSTART+7,RLENGTH-7);match($0,/present=[0-9a-f]+/);p=substr($0,RSTART+8,RLENGTH-8);if(p!=pp){if(pp!="")print pp,s,e,n;pp=p;s=f;n=0};e=f;n++}END{print pp,s,e,n}' "$L" | tail -5
