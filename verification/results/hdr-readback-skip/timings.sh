#!/bin/sh
# Before/after per-latch readback cost, from the worktree root:
#   sh verification/results/hdr-readback-skip/timings.sh > verification/results/hdr-readback-skip/timings.txt
# Case directories (local, untracked) of the runs listed in commands.txt; image_size tells the backend
# (13,193,216 B = the bundled DXVK d3d9, 180,224 B = wined3d).
B=verification/probe/build
R=verification/results/hdr-readback-skip
for pair in \
  before-wined3d:seam-hdr-exposure-20261008-041719-644085 after-wined3d:seam-hdr-exposure-20261008-041522-740723 \
  before-dxvk:seam-hdr-exposure-20261008-041727-839225 after-dxvk:seam-hdr-exposure-20261008-041654-412623 \
  before-wined3d:bench-5120x1440-hdr-tonemap-taa-on-20261008-041725-080372 after-wined3d:bench-5120x1440-hdr-tonemap-taa-on-20261008-041610-783103 \
  before-dxvk:bench-5120x1440-hdr-tonemap-taa-on-20261008-041732-373726 after-dxvk:bench-5120x1440-hdr-tonemap-taa-on-20261008-041703-142567 \
  wined3d:seam-hdr-meter-pending-20261008-041604-815288 dxvk:seam-hdr-meter-pending-20261008-041628-575517 \
  wined3d:seam-hdr-meter-pending-cap-20261008-041609-093256 dxvk:seam-hdr-meter-pending-cap-20261008-041647-292563; do
  label=${pair%%:*}; dir=$B/motion-output-${pair#*:}
  size=$(grep -h '^loaded_module name=d3d9.dll' "$dir"/x3-modern-captures/session-*.log | grep -o 'image_size=[0-9]*' | head -n 1)
  python3 "$R/readback_timing.py" "$label ${pair#*:} $size" "$dir"/x3-modern-captures/session-*.log
done
