#!/bin/sh
# Pre-filter the run262 session log to the three bursts' per-frame lines (input of burst_log.py).
LC_ALL=C grep -E "^(motion_output_frame|camera_state|motion_unmatched_static_frame|capture_armed|motion_output_cut|taa_invalidate)[a-z_]* .*frame=(58[2-5][0-9]|70[6-9][0-9]|125[5-8][0-9]) " \
  /tmp/x3-bottleX3-run262/session-20260923-050141-212.log > "${1:-/tmp/run262_burst_lines.txt}"
