#!/bin/sh
# Measured figures cited by docs/architecture/taa-mask-fold.md (design note, 2026-09-25): the Run 82 A rest
# windows of run312 at 5120x1440 (--gpu-sync-timing, medians in us), the rest/pan summaries of run315,
# and the RESOLVE_BUDGET slot rows of the programs the fold touches. Prints only those rows.
cd "$(dirname "$0")/../../.." || exit 1
echo "== run312 rest windows w10-w17 (taa / tests / box / resolve, median us)"
sed -n '/^run312 columns/,$p' verification/results/run312-run82a-launch1/gpu_taa_out.txt | awk '/^  w1[0-7] /' | grep -o '^  w[0-9]*\|taa=[0-9/]*\|taa_mask_tests=[0-9/]*\|taa_box=[0-9/]*\|taa_resolve=[0-9/]*' | paste - - - - -
echo "== run312/run315 window summaries"
grep -h "run312\|run315" verification/results/run315-run82a-thin-source-vote/gpu_rest_pan_out.txt
echo "== RESOLVE_BUDGET rows"
grep "RESOLVE_BUDGET" verification/results/bottle-X3/temporal-lattice.txt | grep -i "far_camera_hold\|line_mask_camera_depth\b\|line_mask_camera_depth_thin\|rows_half\|columns_half" | sed 's/ device_limit.*//'
echo "== device caps"
grep -h "NumSimultaneousRTs\|FLICKER_CAPS simultaneous_rts" verification/results/bottle-X3/temporal-lattice.txt verification/results/bottle-X3/msaa-caps-probe.txt | head -2
