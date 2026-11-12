#!/bin/sh
# FL run 67 bolt dimming: log rows at the capture groups (grep only; x3m.log is 1.1 GB).
LOG=${1:-/mnt/c/X3FL/x3m.log}
F='38187|38194|39599|39606|60502|60509|61527|61534|74759|74766|74968|74975'
grep -a -m3 "^motion_output_taa device\|^motion_output_taa_luma_lock\|^bolt_far_composite " "$LOG" | grep -oE "^[a-z_]+|far_f0=[^ ]+|far_f1=[^ ]+|far_weight=[^ ]+|requested=[^ ]+|release=[^ ]+|configured=[^ ]+|show=[^ ]+" | paste -sd' '
grep -a -m1 "^motion_output_mode" "$LOG" | grep -oE "taa_debug=[^ ]+"
grep -a -E "^screen_emission_additive_frame .*frame=($F) " "$LOG" | grep -oE " frame=[0-9]+| admitted=[0-9]+| bolt_flag=[0-9]+" | paste -d' ' - - - 
grep -a -E "^motion_output_frame .*frame=($F) " "$LOG" | grep -oE " frame=[0-9]+| taa_k=[^ ]+| camera_policy=[^ ]+" | paste -d' ' - - -
grep -a -E "^camera_state .*frame=($F) " "$LOG" | grep -oE " frame=[0-9]+| p00=[^ ]+| p22=[^ ]+| p32=[^ ]+" | paste -d' ' - - - -
grep -a "^bullet_reshade_frame" "$LOG" | awk '{split($2,a,"=");f=a[2]; if((f>=37800&&f<=39900)||(f>=60300&&f<=61800)||(f>=74400&&f<=75000)) print $2,$5}'
