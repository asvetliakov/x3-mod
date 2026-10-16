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
# Fourth flight (Run181 DLL; taa_far_stabiliser 6th value 0.25 -> 0.5): log x3m.log; Run 180 A is now x3m.prev.log.
LOG4=/mnt/c/X3/x3m.log; LOG3P=/mnt/c/X3/x3m.prev.log
python3 log_rows.py $LOG4 6149 11893 17599 > flight4_log_rows_out.txt
python3 flicker.py $CAP $LOG4 6149 3 plant=3c2b2e60,titan=48d4e998,factoryA=3c284630 > flight4_flicker_6149_out.txt
python3 flicker.py $CAP $LOG4 11893 3 titan=48d4e998,factoryA=3c284630 > flight4_flicker_11893_out.txt
python3 flicker.py $CAP $LOG4 17599 3 titan=48d4e998 > flight4_flicker_17599_out.txt
python3 titan_lightmap.py $LOG4 48d4e998 11893 11908 17599 17614 > flight4_titan_lightmap_out.txt
{ python3 gate_first_frame.py $CAP $LOG4 11893 48d4e998; python3 gate_first_frame.py $CAP $LOG4 17599 48d4e998
  python3 gate_first_frame.py $CAP $LOG3P 6323 44228190; python3 gate_first_frame.py $CAP $LOG3P 11441 44228190; } > flight4_gate_first_frame_out.txt
{ echo "== Run181 6149 plant 3c2b2e60 (gate hi 0.5)"; python3 object_sharpness.py $CAP $LOG4 6149 3c2b2e60
  echo "== Run181 6149 factoryA 3c284630"; python3 object_sharpness.py $CAP $LOG4 6149 3c284630
  echo "== Run181 6149 plant by view z 80000-92000 (gate hi 0.5)"; python3 object_sharpness.py $CAP $LOG4 6149 z:80000:92000
  echo "== Run179 flight 2 2927 plant by view z 80000-89000 (gate hi 0.25; log gone, z mode)"; python3 object_sharpness.py $CAP $LOG4 2927 z:80000:89000; } > flight4_sharpness_out.txt
# Fifth flight (Run 182 A: widening on alpha-tested draws; gate hi still 0.5): log x3m.log; Run 180 B is x3m.prev.log.
LOG5=/mnt/c/X3/x3m.log; LOG4P=/mnt/c/X3/x3m.prev.log
python3 log_rows.py $LOG5 7711 8486 9162 15986 30162 > flight5_log_rows_out.txt
python3 flicker.py $CAP $LOG5 7711 3 titan=4ce60e78,plant=3f6e3060,factoryA=3f6b29d0 > flight5_flicker_7711_out.txt
python3 flicker.py $CAP $LOG5 8486 3 titan=4ce60e78,factoryA=3f6b29d0 > flight5_flicker_8486_out.txt
python3 flicker.py $CAP $LOG5 9162 3 titan=4ce60e78,factoryA=3f6b29d0 > flight5_flicker_9162_out.txt
python3 flicker.py $CAP $LOG5 15986 3 colossus=3e01c088,titan=4d630150,factoryA=3f6b29d0 > flight5_flicker_15986_out.txt
python3 flicker.py $CAP $LOG5 30162 3 plant=3f6e3060 > flight5_flicker_30162_out.txt
python3 flicker.py $CAP $LOG5 9162 3 titan=4ce60e78 > flight5_flicker_9162_draws_out.txt     # per-ps draw attribution rows
python3 flicker.py $CAP $LOG5 8486 3 titan=4ce60e78 > flight5_flicker_8486_draws_out.txt
python3 flicker.py $CAP $LOG4P 11893 3 titan=48d4e998 > flight4_flicker_11893_draws_out.txt
{ echo "== Run 180 A 4905 Titan by view z 39000-46500 (gate 0.25, no alpha widening; log gone)"; python3 object_sharpness.py $CAP $LOG5 4905 z:39000:46500
  echo "== Run 180 B 11893 Titan node 48d4e998 (gate 0.5, no alpha widening)"; python3 object_sharpness.py $CAP $LOG4P 11893 48d4e998
  echo "== Run 182 A 9162 Titan node 4ce60e78 (gate 0.5, alpha widening)"; python3 object_sharpness.py $CAP $LOG5 9162 4ce60e78
  echo "== Run 182 A 9162 Titan by view z 33500-40500 (method check)"; python3 object_sharpness.py $CAP $LOG5 9162 z:33500:40500; } > flight5_sharpness_out.txt
{ echo "== Run 182 A 30162 plant node 3f6e3060"; python3 cutout_bloom.py $CAP $LOG5 30162 3f6e3060
  echo "== Run179 flight 1 13089 plant by view z 50000-65000 box 128,896-768,1152 (log gone)"; python3 cutout_bloom.py $CAP $LOG5 13089 z:50000:65000:128:896:768:1152
  echo "== Run 180 B 6149 plant node 3c2b2e60"; python3 cutout_bloom.py $CAP $LOG4P 6149 3c2b2e60
  echo "== Run 182 A 9162 Titan node 4ce60e78"; python3 cutout_bloom.py $CAP $LOG5 9162 4ce60e78
  echo "== Run 180 B 11893 Titan node 48d4e998"; python3 cutout_bloom.py $CAP $LOG4P 11893 48d4e998; } > flight5_cutout_bloom_out.txt
