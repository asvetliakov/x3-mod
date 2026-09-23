#!/bin/sh
# Run 75 A (run278) docked-at-load card refusal: the rows behind fog-handover.md case C (run278 paragraph).
# Usage: sh rows.sh [session log]  (default: the run278 session log). Output kept beside this script as rows_out.txt.
L=${1:-/tmp/x3-bottleX3-run278/session-20260923-222804-212.log}
echo "refusal values:"; grep -o "refusal=[^ ]*" -- "$L" | sort | uniq -c
echo "first and last gate:states frames:"; grep "refusal=gate:states" -- "$L" | head -1 | cut -c1-120; grep "refusal=gate:states" -- "$L" | tail -1 | cut -c1-120
echo "cold step and the frame after the last refusal:"
grep "^volumetric_fog_cache device=1 frame=11692 \|^volumetric_fog_cards device=1 frame=1169[12] \|^volumetric_fog_cards device=1 frame=1280[67] " -- "$L" | cut -c1-230
echo "observed/suppressed/refused/ready/warmup distribution of the volumetric_fog_cards rows:"
grep "^volumetric_fog_cards " -- "$L" | grep -o "observed=[0-9]* suppressed=[0-9]* refused=[0-9]* ready=[0-9]* warmup=[0-9]*" | sort | uniq -c | sort -rn | head -12
echo "state shadow mode, resyncs and invalidations in a docked frame and an undocked one:"
grep -m1 "^state_hooks " -- "$L"
grep "^motion_output_frame device=1 frame=12692 \|^motion_output_frame device=1 frame=12812 " -- "$L" | grep -o "frame=[0-9]*\|draws=[0-9]*\|set_rt=[0-9]*\|rs_resyncs=[0-9]*\|rs_invalidations=[0-9]*\|sb_resyncs=[0-9]*" | tr '\n' ' '; echo
echo "capture rows (none: no per-draw state row exists for the docked frames):"
grep "^frame_end device=1 " -- "$L" | grep -c "capture=1"; grep -c "^motion_route " -- "$L"
