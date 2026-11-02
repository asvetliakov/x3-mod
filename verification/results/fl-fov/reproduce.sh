#!/bin/sh
# FL field-of-view sites on the installed X3FL.exe (read only): docs/reverse-engineering/fl/field-of-view.md,
# "Verified 2026-11-02". Rewrites verify_fov_site_fl.json beside this script; exit 0 = PASS.
cd "$(dirname "$0")/../../probe" && python3 verify_fov_site_fl.py --exe "${1:-/mnt/c/X3FL/X3FL.exe}" \
    --out ../results/fl-fov/verify_fov_site_fl.json > /dev/null
