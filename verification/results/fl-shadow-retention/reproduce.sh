#!/bin/sh
# shadow_caster_retention on FL: the retirement journal of the FL lifetime observer, host evidence
# (docs/verification/farnhams-legacy.md, "shadow caster retention on FL (host evidence, 2026-11-04)"; facts:
# docs/reverse-engineering/fl/object-lifetimes.md, "Port (2026-11-04)"). Read only on the exe; the native fixture runs
# through WSL interop (no Wine). Exit 0 = all PASS.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
root="$here/../../.."
cd "$root"
# The FL observer fixture (RESULT checks=336, JOURNAL cases=12, asserted by the test) and the retention consumer:
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_fl_object_lifetime \
    verification.analysis.test_shadow_retention
# The fixture's RESULT/TIMING/ANCHOR/JOURNAL lines and the dispatch-path CPU-state audit into fixture.json:
python3 verification/results/fl-object-lifetime/run_fixture.py
python3 tools/config/generate.py --check
# FL build (0 warnings expected):
#   cmake -S . -B build-fl -DX3M_EDITION=fl -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build build-fl -j6
# AP parity (a configured build-ap with CMAKE_EXPORT_COMPILE_COMMANDS=ON is enough): the AP observer, the retention
# consumer's translation unit, loader and capture against main's merge base, and no AP build input changed:
#   python3 -I verification/results/fl-shadow-retention/ap_objects_identical.py --base main \
#       object_lifetime.cpp motion_output.cpp loader.cpp capture.cpp
