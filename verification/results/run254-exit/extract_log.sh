#!/bin/sh
# Pre-filter the 1.0 GB run254 session log to the three bursts' per-frame lines (input of burst_log.py).
LC_ALL=C grep -E "^(motion_output_frame|camera_state|motion_unmatched_static_frame|capture_armed|motion_output_cut|taa_invalidate)[a-z_]* .*frame=(549[0-9]|55[0-2][0-9]|68[7-9][0-9]|690[0-9]|111[7-9][0-9]|1120[0-9]) " \
  /tmp/x3-bottleX3-run254/session-20260923-021028-212.log > "${1:-/tmp/run254_burst_lines.txt}"
