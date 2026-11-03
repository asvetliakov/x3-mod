#!/bin/sh
# FL pause key and run in background sites on the installed X3FL.exe (read only): docs/reverse-engineering/fl/
# pause-background.md, "Port (2026-11-03)". Rewrites the two verify_*_fl.json beside this script; exit 0 = both PASS.
exe="${1:-/mnt/c/X3FL/X3FL.exe}"
cd "$(dirname "$0")/../../probe" || exit 2
python3 verify_pause_key_only_sites_fl.py --exe "$exe" --out ../results/fl-pause-background/verify_pause_key_only_sites_fl.json
a=$?
python3 verify_run_in_background_sites_fl.py --exe "$exe" --out ../results/fl-pause-background/verify_run_in_background_sites_fl.json
b=$?
[ "$a" -eq 0 ] && [ "$b" -eq 0 ]
