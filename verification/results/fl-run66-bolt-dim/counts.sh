#!/bin/sh
# FL run 66 reports 3/4 (bolt dimming): session aggregates from the 308 MB x3m.log, rows only.
LOG=${1:-/mnt/c/X3FL/x3m.log}
grep -a -m3 "bolt_far_composite\|motion_output_mrt_blend\|motion_output_taa_luma_lock\|^motion_output_taa device" "$LOG" | cut -c1-400
grep -a "^screen_emission_additive_frame" "$LOG" | awk '{for(i=2;i<=NF;i++){split($i,a,"=");s[a[1]]+=a[2]} n++} END{print "sea_frames",n,"admitted",s["admitted"],"refused",s["refused"],"bolt_flag",s["bolt_flag"],"bolt_flag_refused",s["bolt_flag_refused"]}'
grep -a "^bullet_reshade_frame" "$LOG" | awk '{for(i=2;i<=NF;i++){split($i,a,"=");s[a[1]]+=a[2]}} END{print "reshade draws",s["draws"],"flagged",s["flagged"],"failures",s["failures"]}'
grep -a "^bolt_copies" "$LOG" | awk '{for(i=2;i<=NF;i++){split($i,a,"=");s[a[1]]+=a[2]}} END{print "early_dropped",s["early_dropped"],"late",s["late"]}'
grep -a "^bolt_footprint " "$LOG" | awk '{for(i=2;i<=NF;i++){split($i,a,"=");s[a[1]]+=a[2]}} END{print "footprint draws",s["draws"],"written",s["written"],"instances",s["instances"],"lengthened",s["lengthened"],"widened",s["widened"]}'
grep -a "^bolt_footprint_hist" "$LOG" | awk '{print $5}' | sort | uniq -c
grep -a -o "m00=[0-9.]*" "$LOG" | cut -c1-9 | sort | uniq -c
