#!/bin/sh
# Run148 (DLL f8122de8, 3454a1cf) crosshair-after-ship-change triage, 2026-10-12. Log /mnt/c/X3/x3m.prev.log (rotated from x3m.log at 01:40) session 20261012-013341-29364; set LOG= to override.
# Script program: Mayhem x3story_07.obj extracted to $SCRATCH (extract_x3story.py in ../native-chase-view-restore-2026-10-11/).
L=${LOG:-/mnt/c/X3/x3m.prev.log}; X=/mnt/c/X3/X3AP.exe; S=${SCRATCH:-/tmp}; D=$(dirname "$0")
echo "## 1 HUD rows present (telemetry off: no chase_central_hud_window / chase_lead_window rows)"
grep -c '^chase_central_hud_window\|^chase_lead_window' $L
grep -n '^chase_central_hud \|^chase_lead ' $L | cut -c1-160
echo "## 2 restore outcome rows (class by ship_id_from/to)"
grep '^chase_view_restore result=' $L | awk '{for(i=1;i<=NF;i++){split($i,a,"=");v[a[1]]=a[2]} print v["frame"],v["result"],v["ship_id_from"]"->"v["ship_id_to"],"s54",v["sector54_from"]"->"v["sector54_to"],"wait="v["wait_updates"],"att="v["attempts"]}'
echo "## 3 ui_scale_camera transitions (ship: one direct row; sector: via 00000000)"
grep '^ui_scale_camera' $L | cut -c1-90
echo "## 4 chase_camera windows around restores"
grep '^chase_camera frame=' $L | awk '{for(i=1;i<=NF;i++){split($i,a,"=");v[a[1]]=a[2]} f=v["frame"]+0; if((f>=2100&&f<=3000)||(f>=6900&&f<=7800)||(f>=10200&&f<=11100)||(f>=14700&&f<=15600)||(f>=26400&&f<=27300)||(f>=32100&&f<=33000)||(f>=35400&&f<=36300)) print f,"app="v["applied"],"ref="v["refused"],"seen="v["cockpits_seen"],"snaps="v["snaps"],"sr="v["snap_reason"],"mode="v["mode"],"trk="v["tracking"],"dist="v["distance"]}'
echo "## 5 script: calls inside SelectMode / ChangeView2 / ChangeViewForMonitor / ChangePlayerShipTo / RestartAllMonitors / WarpToSector / Show / ShowCockpit / UpdateVisibility / SetLeftOffset"
for e in 0x106b1f 0x1047bb 0x104b0b 0xdf5c4 0x103f70 0x16185 0x105e20 0x1072e9 0x103da9 0x105919; do python3 -I $D/method_calls.py $S/x3story_07.obj $e | grep -i 'end\|SelectMode\|UpdateVisibility\|Show\|StartMonitor\|Enable\|AllMonitors\|SetOverlayVisible\|OpenLayout\|SetViewMode\|SetLeftOffset\|UpdateOverlays'; done
echo "## 6 script: who calls UpdateVisibility / SetLeftOffset (static)"
python3 -I $D/static_calls.py $S/x3story_07.obj 0x103da9 0x105919 | awk '{print $1,$2,$3}'
echo "## 7 EXE: writers of cockpit +0x234 (2D draw) and +0x2a0 (display body)"
python3 -I $D/field_writers.py $X 0x2a0 0x234
