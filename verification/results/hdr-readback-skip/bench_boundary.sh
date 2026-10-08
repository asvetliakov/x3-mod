#!/bin/sh
# The 5120x1440 bench boundary (20 timed frames, EVENT-synchronized CPU-inclusive ms) per run, from the worktree root:
#   sh verification/results/hdr-readback-skip/bench_boundary.sh > verification/results/hdr-readback-skip/bench_boundary.txt
# Order of execution as listed; the DXVK pairs were repeated twice in alternating order after the first pair.
B=verification/probe/build/motion-output-bench-5120x1440-hdr-tonemap-taa-on-20261008
for pair in after-wined3d:041610-783103 after-dxvk:041703-142567 before-wined3d:041725-080372 before-dxvk:041732-373726 \
            after-dxvk:041843-917209 before-dxvk:041850-722136 before-dxvk:041902-692875 after-dxvk:041908-939922; do
  d=$B-${pair#*:}
  echo "${pair%%:*} ${pair#*:} $(grep -h '^loaded_module name=d3d9.dll' "$d"/x3-modern-captures/session-*.log | grep -o 'image_size=[0-9]*' | head -n 1) $(grep -h '^BENCH_SUMMARY' "$d"/fixture-stdout.txt | grep -o 'min_ms=[^ ]* median_ms=[^ ]* max_ms=[^ ]*')"
done
