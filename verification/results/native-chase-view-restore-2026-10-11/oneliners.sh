# Native chase_view_restore triage, session 20261011-225940 (C:\X3\x3m.log, 6,307,221 B, 16,381 lines).
grep -o "^[a-z_0-9]*chase_view_restore[a-z_]*" /mnt/c/X3/x3m.log | sort | uniq -c   # restore 1, site 7, arm_refused 1, state 0
grep -n "chase_view_restore_arm_refused" /mnt/c/X3/x3m.log                          # L923, reason=4 valid=263 refused=2696
python3 -I triage.py /mnt/c/X3/x3m.log > triage_out.txt                            # transits + chase_camera mode changes
python3 -I cat_x3story.py /mnt/c/X3/*.cat /mnt/c/X3/addon/*.cat > cat_x3story_out.txt
for c in 04 07; do python3 -I extract_x3story.py /mnt/c/X3/addon/$c.cat "$SCRATCH/x3story_$c.obj"; done > extract_out.txt  # obj stays untracked
grep -rn "identity_valid=263\|identity_valid=399" ../run409-engine-exhaust/run409.txt ../run132-dxvk-triage/failed_rows.txt ../../../docs/reverse-engineering/chase-view-transition.md
# Mask decode (chase_transition_identity_core.h:11-22): 263=vm1|native2|player4|killed256; 2696=controller8|warp128|player_live512|native_live2048
