#!/bin/sh
# usage: dry.sh <extra flags>; prints the sun-occlusion env of the stand command dry run
cd "$(dirname -- "$0")/../../.." || exit
STAND="--direct --camera chase --chase-view-restore --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --camera-log 1 --hdr --hdr-tonemap --hdr-exposure auto --hdr-bloom --bloom-source-clamp 1.0 --crypt-cache --gz-buffer --resource-read fast --dat-handles --mesh-adjacency fast --screen-emission-additive 2 --screen-emission-additive-alpha 0 --emission-source-gain 2 --loading-intervals --sun-shadow-lane --shadow-replay-depth --shadow-replay-candidates --sun-shadow-apply --shadow-sun-poll on --fps-overlay --shadow-cascades 250,1500,7500,37500,150000 --shadow-cascade-drop-order importance --shadow-cascade-records 1024,1024,2048,4096,4096 --shadow-cascade-sizes 2048,2048,2048,2048,2048 --shadow-retention-census --shadow-caster-retention --shadow-cascade-adaptive-c0 1.5 --light-map-far-fade 80,220 --motion-rt-mode lazy --frame-end-stride 1 --volumetric-fog 0.02 --volumetric-fog-cards replace --volumetric-fog-range stored --volumetric-fog-timing --capture-start 999999 --capture-frames 8 --capture-delay 300 --cull-small-parts 4 --frame-timing --frame-phases --object-bounds-log --cull-census"
if [ "$1" = "--vanilla" ]; then STAND=""; fi
env -u CX_DEBUGMSG X3M_FIXTURE_BOTTLE=X3 X3M_MOTION_FRAME_LOG=1 python3 tools/manage.py launch --bottle X3 --dry-run $STAND "$@" > /tmp/x3m-dry-$$.json 2>/tmp/x3m-dry-$$.err
rc=$?
echo "rc=$rc"
python3 -c "import json,sys; d=json.load(open('/tmp/x3m-dry-$$.json')); print({k:v for k,v in d['env'].items() if 'SUN_OCCLUSION' in k})" 2>/dev/null || tail -2 /tmp/x3m-dry-$$.err
rm -f /tmp/x3m-dry-$$.json /tmp/x3m-dry-$$.err
