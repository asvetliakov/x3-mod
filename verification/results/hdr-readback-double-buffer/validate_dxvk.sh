#!/bin/sh
# Offline validation of the newest case directory per case (the DXVK runs, made after the wined3d runner pass), from
# the worktree root:
#   sh verification/results/hdr-readback-double-buffer/validate_dxvk.sh > verification/results/hdr-readback-double-buffer/after_dxvk_validation.txt
# Each case ran as: X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_motion_output.py
#   --wine-env CX_GRAPHICS_BACKEND=dxvk --dll <final>/d3d9.dll --seam <final>/seam/d3d9.dll --fixture <final>/motion_output_fixture.exe CASE
# and stopped at the runner's exit-code assertion (teardown crash) or, for the ownership case, at the NaN store record.
B=verification/probe/build
V=verification/results/hdr-readback-double-buffer/validate_offline.py
for c in seam-hdr-exposure seam-hdr-exposure-offset seam-ownership-hdr-exposure seam-hdr-tonemap-fault seam-hdr-meter-selftest-unlock; do
  d=$(ls -d "$B/motion-output-$c-2026"* | tail -n 1)
  printf '%s ' "$(grep -h '^loaded_module name=d3d9.dll' "$d"/x3-modern-captures/session-*.log | grep -o 'image_size=[0-9]*')"
  python3 "$V" "$c" "$d" --nan-store-zero 2>&1 | tail -n 1 | cut -c1-900
done
