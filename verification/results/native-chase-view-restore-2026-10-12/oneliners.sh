#!/bin/sh
# Native Run146 chase_view_restore triage (2026-10-12). Log: /mnt/c/X3/x3m.log session 20261012-010344-2916.
L=/mnt/c/X3/x3m.log; X=/mnt/c/X3/X3AP.exe; S=${SCRATCH:-/tmp}
# 1 tiers and install rows (telemetry/diagnostics/timing all 0)
grep -n "^chase_transition installed\|^chase_view_restore installed" $L
grep -n "^ *;\? *perf *=\|^ *;\? *debug *=" /mnt/c/X3/x3m.ini
# 2 the four outcome rows (sector_from=0 on all four)
grep -n "^chase_view_restore result=" $L
# 3 chase-hook visits by window: frames advance in mode 1, inactive=0 unbound=0 (fresh cockpit admitted and bound)
grep "^chase_camera frame=" $L | awk '{for(i=1;i<=NF;i++){split($i,a,"=");v[a[1]]=a[2]} print v["frame"],"frames="v["frames"],"applied="v["applied"],"inactive="v["refused_inactive"],"unbound="v["admitted_unbound"],"seen="v["cockpits_seen"],"mode="v["mode"],"connect="v["connect"],"flags="v["flags_1a0"]}'
grep "^chase_camera frame=" $L | grep -o "coalesced=[0-9]*" | sort | uniq -c
# 4 sector transits seen by the fog sampler (cockpit +0x54 path)
grep -n "^volumetric_fog_sector" $L | cut -c1-140
# 5 EXE: +0x1fc writer, constructor/destructor call sites (return address = call + 5)
python3 -I disasm.py $X 0x00421019 0x004210f0
python3 -I callers.py $X 0x0041f8d0 0x0041fbe8 0x0041fe04 0x0041ffc0
# 6 script program: class 0x137 owns GetCurrentObjectIDInSector in stock (04) and Mayhem (07)
python3 -I class_lookup.py 0x137 GetCurrentObjectIDInSector $S/x3story_04.obj $S/x3story_07.obj
python3 -I method_rows.py GetCurrentObjectIDInSector $S/x3story_04.obj $S/x3story_07.obj

# 2026-10-12 Run148 crosshair-after-ship-change triage: see run148_hud_triage.sh (output run148_hud_triage_out.txt)
