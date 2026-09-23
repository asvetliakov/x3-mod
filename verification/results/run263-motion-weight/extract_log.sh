#!/bin/sh
# Pre-filter the run263 session log to the two bursts' per-frame lines (input of burst_log.py).
LC_ALL=C grep -E "^(motion_output_frame|camera_state|motion_unmatched_static_frame|capture_armed|motion_output_cut|taa_invalidate)[a-z_]* .*frame=(6[01][0-9][0-9]|109[0-9][0-9]|110[0-1][0-9]) " \
  /tmp/x3-bottleX3-run263/session-20260923-050922-212.log > "${1:-/tmp/run263_burst_lines.txt}"
