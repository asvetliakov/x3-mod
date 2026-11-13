#!/bin/sh
# Host evidence of the LOD-swap inheritance (docs/architecture/shadow-caster-retention.md, "A LOD swap keeps the
# static class (2026-11-13, FL 71 A)"). Run from the repository root with build-lodswap-fl and build-lodswap-ap
# configured (MinGW i686, RelWithDebInfo). Prints the retention and shadow unittest tails, the store driver's RESULT
# line, the review probe, both editions' warning lines from a clean build and their x87 checks, and the forced
# rebuild of the retention fixture (fixture exe and seam DLL, -Werror). No Wine. Output: host-check.txt.
set -e
PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_shadow_retention*.py' 2>&1 | tail -n 3
PYTHONPATH=verification/probe python3 -m unittest discover -s verification/analysis -p 'test_*shadow*.py' 2>&1 | grep -E '^(Ran|OK|FAILED)'
echo "retention tests: $(grep -c '    def test_' verification/analysis/test_shadow_retention.py)"
python3 verification/results/retention-alpha-casters/driver_result.py
python3 verification/results/fl71a-lod-swap/probe.py
for edition in fl ap; do
    echo "build-lodswap-$edition (clean): $(cmake --build build-lodswap-$edition --target d3d9 --clean-first -j10 2>&1 | grep -ci warning) warning lines"
    echo "x87 $edition: $(python3 verification/probe/check_no_x87.py build-lodswap-$edition/d3d9.dll --edition $edition | grep '"result"')"
done
rm -f verification/probe/build/motion_output_fixture.exe verification/probe/build/motion-output-seam/*.o verification/probe/build/motion-output-seam/d3d9.dll
log=$(mktemp)
X3M_CANDIDATE_BUILD=../../build-lodswap-ap sh verification/probe/build_motion_output.sh > "$log" 2>&1 && status=0 || status=$?
echo "retention fixture rebuild (exe + seam DLL, -Werror): exit=$status diagnostic_lines=$(grep -ciE 'warning|error' "$log")"
rm -f "$log"
sha256sum verification/probe/build/motion_output_fixture.exe verification/probe/build/motion-output-seam/d3d9.dll | sed 's|  verification/probe/build/| |'
