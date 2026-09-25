#!/bin/sh
# Q3: cursor/window state from launch to the first activation cycles (change-only rows)
L=${1:-/tmp/x3-bottleX3-run323/session-20260925-042913-216.log}
echo "# telemetry_cursor_poll frame flags cursor (changes, frame<700)"
grep -a '^telemetry_cursor_poll' $L | awk '{for(i=1;i<=NF;i++){split($i,a,"=");v[a[1]]=a[2]}; f=v["frame"]+0; if(f<700){k=v["flags"]" "v["cursor"]; if(k!=p){print f,k;p=k}}}'
echo "# telemetry_window frame foreground thread_focus (changes)"
grep -a '^telemetry_window' $L | awk '{for(i=1;i<=NF;i++){split($i,a,"=");v[a[1]]=a[2]}; k=v["foreground"]" "v["thread_focus"]; if(k!=p){print v["frame"],k,v["window_rect"],v["client_rect"];p=k}}' | head -12
echo "# first window_msg rows"; grep -a '^window_msg ' $L | head -12 | awk '{print $2,$4,$7,$8,$9}'
echo "# window_trace_flush"; grep -a '^window_trace_flush' $L | awk '{print $2,$4,$5}'
echo "# cursor_reassert"; grep -a '^cursor_reassert ' $L | awk '{print $2,$4,$5,$6,$7,$10,$11,$12,$13,$15,$20}'
echo "# first cursor_snapshot frame"; grep -a '^cursor_snapshot' $L | head -1 | awk '{print $3,$5,$12,$13,$17}'
