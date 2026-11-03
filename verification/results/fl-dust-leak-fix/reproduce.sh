#!/bin/sh
# FL dust leak fix, host evidence (docs/verification/farnhams-legacy.md, "dust_leak_fix on FL (host evidence,
# 2026-11-03)"; facts: docs/reverse-engineering/fl/dust-light.md, "Port (2026-11-03)"). Read only on the exe; the
# fixture runs natively through WSL interop (no Wine). Rewrites verify_dust_leak_fix_site_fl.json and fixture.json
# beside this script; exit 0 = all PASS. Run from anywhere.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../../.."
python3 "$root/verification/probe/verify_dust_leak_fix_site_fl.py" "${1:-/mnt/c/X3FL/X3FL.exe}" \
    --out "$here/verify_dust_leak_fix_site_fl.json" > /dev/null
python3 -c "import json,sys; r=json.load(open(sys.argv[1])); print('verifier', r['result'], sum(r['checks'].values()), '/', len(r['checks']), 'edges', r['raw_edges_to_site'], 'interior', r['raw_edges_into_span'])" \
    "$here/verify_dust_leak_fix_site_fl.json"
python3 "$here/run_fixture.py"
cd "$root" && PYTHONPATH=verification/probe:verification/analysis python3 -m unittest \
    verification/analysis/test_fl_dust_leak_fix.py verification/analysis/test_dust_leak_fix_site.py
python3 "$root/tools/config/generate.py" --check
# AP parity (needs a configured build-ap): capture.cpp/loader.cpp disassembly against main, dust_leak_fix.cpp.obj hash
#   python3 -I verification/results/fl-dust-leak-fix/ap_objects_identical.py --base main
