#!/bin/sh
# Per bottle-X3 run: DXVK present, capture frames, readback rows, DXVK alloc failures, terminate, last session_end row,
# exit-path marker rows (music_trace_stop) in the last 3000 session rows.
for d in /tmp/x3-bottleX3-run*; do
  s=$d/launcher-stderr.log; L=$(ls $d/session-*.log 2>/dev/null | head -1); [ -z "$L" ] && continue
  printf '%s dxvk=%s capture_frames=%s readback_rows=%s alloc_failed=%s terminate=%s exit_marker=%s | %s\n' "$(basename $d)" \
    "$(grep -c 'info:  DXVK: v' $s 2>/dev/null)" "$(grep -c -E '^frame_end .* capture=1 ' $L)" \
    "$(grep -c -E '^(hdr_readback|motion_output_readback|motion_output_depth_readback|shadow_replay_map_readback) ' $L)" \
    "$(grep -c 'Memory allocation failed' $s 2>/dev/null)" "$(grep -c 'terminate called' $s 2>/dev/null)" \
    "$(tail -3000 $L | grep -c '^music_trace_stop')" "$(grep '^session_end' $L)"
done
