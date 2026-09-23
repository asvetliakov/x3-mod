#!/bin/sh
# Run 263 (Run 70 A with --taa-motion-weight 0.7,2,8, Run69 DLL) triage; reproduces every *_out*.txt here.
# Bursts: 6104 and 10979 (32 frames each). taa_age/taa_mask readbacks from the bottle capture dir (mtime 05:11/05:13
# on 2026-09-23; colour bytes identical to /tmp/x3-bottleX3-run263). Scripts copied unchanged from run262-motion-weight
# (hull_ripple.py import path repointed).
R=/tmp/x3-bottleX3-run263
AD="$HOME/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures"
cd "$(dirname "$0")"
sh extract_log.sh /tmp/run263_burst_lines.txt && python3 burst_log.py /tmp/run263_burst_lines.txt 6104 10979 > burst_log_out.txt
: > dark_vs_own_mean_out.txt
for s in 6104 10979; do
  python3 dark_vs_own_mean.py $R $s >> dark_vs_own_mean_out.txt
  python3 age_census.py $R "$AD" $s > age_census_out_$s.txt
  python3 hull_sharp.py $R "$AD" $s > hull_sharp_out_$s.txt; python3 hull_blurfit.py $R "$AD" $s > hull_blurfit_out_$s.txt
  python3 hull_region_split.py $R "$AD" $s > hull_region_split_out_$s.txt
  python3 hull_ripple.py $R "$AD" $s > hull_ripple_out_$s.txt
done
