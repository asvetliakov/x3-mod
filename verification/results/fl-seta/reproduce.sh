#!/bin/sh
# FL SETA restore and manual flight, host evidence (docs/verification/farnhams-legacy.md, "seta_restore and
# seta_manual_flight on FL (host evidence, 2026-11-03)"; facts: docs/reverse-engineering/fl/seta.md, "Port (2026-11-03)").
# Read only on the exe and the story catalogue; the fixtures run natively through WSL interop (no Wine). Rewrites
# results.json beside this script; exit 0 = all PASS. Run from anywhere.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../../.."
python3 "$root/verification/probe/verify_seta_manual_flight_sites_fl.py"
python3 "$root/verification/probe/verify_seta_restore_sites_fl.py"
python3 "$here/run_fixtures.py"
cd "$root" && PYTHONPATH=verification/probe:verification/analysis python3 -m unittest \
    verification/analysis/test_fl_seta_restore.py verification/analysis/test_fl_seta_manual_flight.py \
    verification/analysis/test_seta_restore.py verification/analysis/test_seta_manual_flight.py \
    verification/analysis/test_fl_chase_transition.py
python3 "$root/tools/config/generate.py" --check
# FL build (0 warnings expected):
#   cmake -S . -B build-fl -DX3M_EDITION=fl -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build build-fl -j4
# AP parity (needs a configured build-ap): capture.cpp/loader.cpp/chase_transition.cpp disassembly against main and the
# AP SETA sources unchanged
#   python3 -I verification/results/fl-seta/ap_objects_identical.py --base main
