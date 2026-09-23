#!/bin/sh
# Run 76 C (run283) docked-at-load card refusal: the refused state vector behind fog-handover.md case C (run283 paragraph).
# Usage: sh rows.sh [session log]  (default: the run283 session log). Output kept beside this script as rows_out.txt.
L=${1:-/tmp/x3-bottleX3-run283/session-20260924-003849-212.log}
echo "refused state vectors (frames, then the distinct vectors):"
grep -o "volumetric_fog_card_states device=1 frame=[0-9]*" -- "$L"
grep "volumetric_fog_card_states " -- "$L" | sed 's/.*volumetric_fog_card_states //; s/frame=[0-9]* //' | sort | uniq -c
echo "refusal values of the volumetric_fog_cards rows:"
grep "^volumetric_fog_cards " -- "$L" | grep -o "refusal=[^ ]*" | sort | uniq -c
echo "first and last gate:states frames:"
grep "^volumetric_fog_cards .*refusal=gate:states" -- "$L" | head -1 | cut -c1-120
grep "^volumetric_fog_cards .*refusal=gate:states" -- "$L" | tail -1 | cut -c1-120
