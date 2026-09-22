#!/bin/sh
# Run 254 (Run 68 session A) triage: reproduces every *_out*.txt here. Bursts: 5496 SETA 1 (logged yaw 0.2549 deg/frame
# constant), 6875 normal speed, 11177 SETA 2 (logged yaw 0). taa_age / taa_mask readbacks were not copied into the
# preserved dir; they are read from the bottle capture dir (mtime 02:12-02:15 on 2026-09-23, colour bytes identical).
R=/tmp/x3-bottleX3-run254
AD="$HOME/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures"
cd "$(dirname "$0")"
sh extract_log.sh /tmp/run254_burst_lines.txt && python3 burst_log.py /tmp/run254_burst_lines.txt > burst_log_out.txt
python3 mid_band_chain.py $R 5505 5515 5525 6885 6900 11185 11195 11205 > mid_band_chain_out.txt
: > darksky_out.txt; : > dark_vs_own_mean_out.txt
for s in 5496 6875 11177; do
  python3 darksky.py $R $s >> darksky_out.txt; python3 dark_vs_own_mean.py $R $s >> dark_vs_own_mean_out.txt
  python3 band.py $R $s > band_out_$s.txt; python3 band_parallax.py $R $s > band_parallax_out_$s.txt
  python3 ring_flicker.py $R $s > ring_out_$s.txt; python3 far_tail.py $R $s > far_tail_out_$s.txt
  python3 age_census.py $R "$AD" $s > age_census_out_$s.txt; python3 age_takers.py $R "$AD" $s > age_takers_out_$s.txt
  python3 hull_sharp.py $R "$AD" $s > hull_sharp_out_$s.txt; python3 hull_blurfit.py $R "$AD" $s > hull_blurfit_out_$s.txt
  python3 hull_region_split.py $R "$AD" $s > hull_region_split_out_$s.txt
done
python3 log_facts.py > log_facts_out.txt
