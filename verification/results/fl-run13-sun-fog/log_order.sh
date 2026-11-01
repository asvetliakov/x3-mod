#!/bin/sh
# FL run 13, capture frame 26291: line order of the readbacks against the post-boundary draws, and the frame's
# sun / camera / emission rows. Usage: sh log_order.sh /mnt/c/X3FL/x3m.log
L=${1:-/mnt/c/X3FL/x3m.log}
grep -n -E "frame=26291 " "$L" | grep -E "^[0-9]+:(scene_end_marker|hdr_readback|motion_output_color_readback|motion_output_present_readback)" | cut -c1-90
for i in 541 542 546 547 556 557 567; do printf '%s draw %s\n' "$(grep -n -m1 -E "^draw device=1 frame=26291 index=$i " "$L" | cut -d: -f1)" $i; done
grep -E "frame=26291 " "$L" | grep -E "^(camera_state|shadow_replay_map_basis device=1 frame=26291 cascade=0|screen_emission_additive_frame|emission_source_gain_frame|hull_emission_frame)" | cut -c1-260
grep -E "^volumetric_fog_frame" "$L" | grep -E "frame=2(5800|6400) " | cut -c1-140
