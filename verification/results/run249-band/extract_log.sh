#!/bin/sh
# Pre-filter the 1.3 GB run249 session log to the four bursts' per-frame lines (input of burst_log.py).
LC_ALL=C grep -E "^(motion_output_frame|camera_state|motion_unmatched_static_frame|capture_armed)[a-z_]* .*frame=(3[45][0-9][0-9]|41[4-9][0-9]|55[3-7][0-9]|92[6-9][0-9]) " \
  /tmp/x3-bottleX3-run249/session-20260922-225116-212.log > "${1:-burst_lines.txt}"
