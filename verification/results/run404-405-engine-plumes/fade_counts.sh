#!/bin/sh
# Run 120 A (run404 nozzle 0.25, run405 nozzle 0.5): engine_stage rows with plumes drawn (nozzles > 0) and those whose
# near-camera fade (faded > 0) or cap (capped > 0) bit. Streams the session logs with grep/awk; prints counts only.
# Usage: sh verification/results/run404-405-engine-plumes/fade_counts.sh > .../fade_counts_out.txt
for r in run404 run405; do
  f=$(ls /tmp/x3-bottleX3-$r/session-*.log)
  grep '^engine_stage ' "$f" | awk -v run="$r" '{
    n = 0; fd = 0; cp = 0
    for (i = 2; i <= NF; i++) {
      split($i, a, "=")
      if (a[1] == "nozzles") n = a[2] + 0
      if (a[1] == "faded") fd = a[2] + 0
      if (a[1] == "capped") cp = a[2] + 0
    }
    t++
    if (n > 0) { p++; if (fd > 0) f++; if (cp > 0) c++ }
  } END { printf "%s stage_rows=%d plume_frames=%d faded_frames=%d capped_frames=%d\n", run, t, p, f, c }'
done
