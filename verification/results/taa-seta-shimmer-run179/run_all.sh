#!/bin/bash
# Run179 flight 2026-10-16 (native C:\X3 = /mnt/c/X3), four F8 bursts at 5120x1440: shimmer of bright hull detail vs the
# luma lock. Reads the captures and the 566 MB log by grep / per-file numpy only. ~3 min per flicker burst.
cd "$(dirname "$0")"
CAP=/mnt/c/X3/x3-modern-captures; LOG=/mnt/c/X3/x3m.log
python3 log_rows.py $LOG > log_rows_out.txt
for f in 11400 11406; do python3 rotation_path.py $CAP $LOG $f; done > rotation_path_out.txt
for b in 4707 7631 11391 13089; do echo "== burst $b"; python3 frame_step.py $CAP $LOG $b; done > frame_step_out.txt
for b in 4707 7631 11391 13089; do python3 flicker.py $CAP $LOG $b > flicker_${b}_out.txt; done
# Target identity: cull_census rows (engine distance d) of the Titan and the solar plant in one frame per burst.
grep -E '^cull_census device=1 frame=(4715|7640|11400|13095) ' $LOG | grep 'verdict=kept' | grep -E 'titan|solarpowerplant' \
  | grep -oE 'frame=\S+| d=\S+|body=\S+' | paste - - - > targets_out.txt
# Second flight (same DLL, taa_far_stabiliser = 0.985,0,30,34,0.03,0.25): log x3m.log, the first flight is now x3m.prev.log.
LOG2=/mnt/c/X3/x3m.log; LOG1=/mnt/c/X3/x3m.prev.log
python3 log_rows.py $LOG2 2927 6560 9819 10280 > flight2_log_rows_out.txt
for b in 2927 6560 9819 10280; do echo "== burst $b"; python3 frame_step.py $CAP $LOG2 $b 499e5b30; done > flight2_frame_step_out.txt
for b in 2927 6560 9819 10280; do python3 flicker.py $CAP $LOG2 $b 3 titan=499e5b30,plant=401fc1d8 > flight2_flicker_${b}_out.txt; done
python3 flicker.py $CAP $LOG1 4707 3 titan=49acf8c0,plant=3ba44c98 > flicker_4707_objects_out.txt
python3 titan_lightmap.py $LOG2 499e5b30 2942 6568 6575 9834 10295 > flight2_titan_lightmap_out.txt
# Third flight (Run180 DLL; widened light-map variants created): log x3m.log; the Run179 second flight is now x3m.prev.log.
# flicker.py now also reports the in-flight step (motion of the burst's first frame at the chain's world point).
LOG3=/mnt/c/X3/x3m.log
python3 log_rows.py $LOG3 4905 6323 11441 24922 > flight3_log_rows_out.txt
for b in 4905 6323 11441; do python3 flicker.py $CAP $LOG3 $b 3 titan=44228190,station=39ff7620 > flight3_flicker_${b}_out.txt; done
python3 flicker.py $CAP $LOG3 24922 3 plant=3a024500 > flight3_flicker_24922_out.txt
python3 titan_lightmap.py $LOG3 44228190 4905 4920 6323 6338 11441 11456 > flight3_titan_lightmap_out.txt
python3 titan_lightmap.py $LOG3 3a024500 24922 24937 > flight3_plant_lightmap_out.txt
