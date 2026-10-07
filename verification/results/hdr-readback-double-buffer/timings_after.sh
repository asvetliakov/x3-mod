#!/bin/sh
# After-change timings, from the worktree root (oldest case directory = the wined3d runner pass, newest = the DXVK run):
#   sh verification/results/hdr-readback-double-buffer/timings_after.sh
# writes after_wined3d.txt and after_dxvk.txt beside this script. The before_*.txt files came from the same
# readback_timing.py on the runs of the f67f536a binaries (identical commands with --dll/--seam/--fixture of that build).
B=verification/probe/build
R=verification/results/hdr-readback-double-buffer
: > "$R/after_wined3d.txt"; : > "$R/after_dxvk.txt"
for c in seam-hdr-exposure bench-5120x1440-hdr-tonemap-taa-on; do
  w=$(ls -d "$B/motion-output-$c-2026"* | head -n 1); x=$(ls -d "$B/motion-output-$c-2026"* | tail -n 1)
  python3 "$R/readback_timing.py" "after-wined3d-$c" "$w"/x3-modern-captures/session-*.log >> "$R/after_wined3d.txt"
  python3 "$R/readback_timing.py" "after-dxvk-$c" "$x"/x3-modern-captures/session-*.log >> "$R/after_dxvk.txt"
done
