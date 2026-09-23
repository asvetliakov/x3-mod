#!/bin/sh
# run280 F8 bursts: capture_armed rows, per-frame capture span from capture_event qpc, frame_timing_slow rows near the bursts.
L=${1:-/tmp/x3-bottleX3-run280/session-20260923-224046-212.log}
grep '^capture_armed' "$L"
grep '^capture_event' "$L" | awk '{split($3,a,"=");f=a[2]; for(i=1;i<=NF;i++) if($i~/^qpc=/){split($i,q,"=");if(!(f in lo))lo[f]=q[2];hi[f]=q[2]}} END{for(f in lo) printf "%s %.1f ms\n",f,(hi[f]-lo[f])/1e4}' | sort -n
grep '^frame_timing_slow' "$L" | awk '{split($2,a,"=");f=a[2]; if((f>=3470&&f<=3490)||(f>=5680&&f<=5700)) print $2,$3}'
