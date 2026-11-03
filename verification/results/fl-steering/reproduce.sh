#!/bin/sh
# FL base ship steering and avoidance, host evidence (docs/verification/farnhams-legacy.md, "ship_steering and ship_avoidance
# on FL (host evidence, 2026-11-03)"; facts: docs/reverse-engineering/fl/ship-steering.md and ship-avoidance.md,
# "Port (2026-11-03)"). Read only on the exe; the native fixtures run through WSL interop (no Wine). Exit 0 = all PASS.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../../.."
python3 "$root/verification/probe/verify_ship_steering_sites_fl.py"
python3 "$root/verification/probe/verify_ship_avoidance_sites_fl.py"
# The two native fixtures (built and run by the host tests; their RESULT/DIFF/CASES lines are asserted there), the AP
# steering modules and the engine-effects pin:
cd "$root" && PYTHONPATH=verification/probe:verification/analysis python3 -m unittest \
    verification/analysis/test_fl_ship_steering.py verification/analysis/test_fl_ship_avoidance.py \
    verification/analysis/test_ship_steering.py verification/analysis/test_ship_steering_sites.py \
    verification/analysis/test_engine_effects.py
python3 "$root/tools/config/generate.py" --check
# FL build (0 warnings expected):
#   cmake -S . -B build-fl -DX3M_EDITION=fl -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build build-fl -j4
# AP parity (needs a configured build-ap with x3m_source_commit built): capture.cpp and every AP steering translation unit
# (the walk core's reader-bound slot) disassembly against main, the AP steering sources unchanged
#   python3 -I verification/results/fl-steering/ap_objects_identical.py --base main
