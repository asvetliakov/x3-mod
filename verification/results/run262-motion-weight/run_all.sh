#!/bin/sh
# Run 262 (Run 70 A, --taa-motion-weight 0.8,2,8) triage; reproduces every *_out*.txt here. Bursts: 5823 SETA 1
# (yaw 0.285 deg/frame), 7062 normal speed (pan frames 7062-7068), 12554 SETA 2 (yaw 0.292 deg/frame).
# taa_age / taa_mask readbacks are read from the bottle capture dir (mtime 05:03-05:06 on 2026-09-23, colour bytes identical).
R=/tmp/x3-bottleX3-run262
AD="$HOME/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures"
cd "$(dirname "$0")"
sh extract_log.sh /tmp/run262_burst_lines.txt && python3 burst_log.py /tmp/run262_burst_lines.txt 5823 7062 12554 > burst_log_out.txt
: > dark_vs_own_mean_out.txt
for s in 5823 7062 12554; do
  python3 dark_vs_own_mean.py $R $s >> dark_vs_own_mean_out.txt
  python3 age_census.py $R "$AD" $s > age_census_out_$s.txt
  python3 hull_sharp.py $R "$AD" $s > hull_sharp_out_$s.txt; python3 hull_blurfit.py $R "$AD" $s > hull_blurfit_out_$s.txt
  python3 hull_region_split.py $R "$AD" $s > hull_region_split_out_$s.txt
  python3 hull_ripple.py $R "$AD" $s > hull_ripple_out_$s.txt
done
python3 hull_ripple.py /tmp/x3-bottleX3-run254 "$AD" 5496 > hull_ripple_run254_5496.txt
python3 hull_ripple.py /tmp/x3-bottleX3-run254 "$AD" 6875 > hull_ripple_run254_6875.txt
python3 hull_ripple.py /tmp/x3-bottleX3-run254 "$AD" 11177 > hull_ripple_run254_11177.txt
