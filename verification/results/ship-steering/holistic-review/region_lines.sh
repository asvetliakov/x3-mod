#!/bin/sh
# Line counts per region of the ship-steering stack at the given commit (default HEAD), for
# docs/architecture/ship-steering-holistic-review.md (question 3). Regions are by the outline of each file
# (grep -n of the section banners); re-run after a refactor to compare.
cd "$(dirname "$0")/../../../.." || exit 1
C=${1:-HEAD}
f() { git show "$C:$1" | wc -l; }
r() { git show "$C:$1" | awk -v a="$2" -v b="$3" 'NR>=a && NR<=b' | wc -l; }
echo "commit $(git rev-parse --short "$C")"
for p in src/proxy/ship_steering_core.h src/proxy/ship_avoidance_core.h src/proxy/ship_avoidance_walk_core.h \
         src/proxy/ship_capital_lane_core.h src/proxy/ship_gate_ctl_core.h src/proxy/ship_gate_ctl_live_core.h \
         src/proxy/ship_steering_sites.h src/proxy/ship_steering.cpp verification/probe/ship_steering_host.cpp; do
  printf '%-48s %6s\n' "$p" "$(f "$p")"
done
G=src/proxy/ship_gate_ctl_core.h
echo "ship_gate_ctl_core.h regions (banner lines of the HEAD outline):"
printf '  %-44s %6s\n' "board/member/roles (152-698)"        "$(r $G 152 698)"
printf '  %-44s %6s\n' "stage B geometry: Q, petals, hold (699-1246)" "$(r $G 699 1246)"
printf '  %-44s %6s\n' "lane constants/geometry/laws (1247-1621)" "$(r $G 1247 1621)"
printf '  %-44s %6s\n' "rollout/sweep/decide/E rows (1622-2918)"  "$(r $G 1622 2918)"
printf '  %-44s %6s\n' "stage B sequencer/sb_frame (2919-3752)"   "$(r $G 2919 3752)"
printf '  %-44s %6s\n' "lane_frame/site B/defer/steer (3753-4423)" "$(r $G 3753 4423)"
printf '  %-44s %6s\n' "D.5 small-ship B, adopt memo (4424-4524)" "$(r $G 4424 4524)"
printf '  %-44s %6s\n' "lines mentioning petal/Petal"            "$(git show "$C:$G" | grep -c 'petal\|Petal')"
H=verification/probe/ship_steering_host.cpp
echo "ship_steering_host.cpp regions:"
printf '  %-44s %6s\n' "amendment 13-15 capital transit (3938-5663)" "$(r $H 3938 5663)"
printf '  %-44s %6s\n' "amendment 16 lane board units (5664-7860)"  "$(r $H 5664 7860)"
printf '  %-44s %6s\n' "stage A/T, stage B, B.3-B.9 (7861-14886)"   "$(r $H 7861 14886)"
printf '  %-44s %6s\n' "lane scenario C (14887-16013)"             "$(r $H 14887 16013)"
