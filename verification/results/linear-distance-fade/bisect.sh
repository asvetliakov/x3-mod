#!/bin/sh
# Reproduces the 2026-09-24 bisect of the detached distance-fade fixture
# ("actual native/E alpha identity"). Usage: bisect.sh <commit> <scratch-dir>
# Exports src + verification/{probe,analysis} at <commit> into the scratch dir,
# adds linear_emission.cpp to the fade build line when it is missing (needed
# from 72e799b0 on), builds, and runs the runner once under the Wine lock.
# Measured: 051b7652, 6b18a10f, 155ac545, 48d70c35 PASS (78 cases);
# d8642469, 36d25a79 FAIL; d8642469 with 48d70c35's linear_material_fixture.cpp
# PASS, so the defect is in the fixture, not production.
set -eu
REPO=$(cd "$(dirname "$0")/../../.." && pwd)
D=$2/bisect-$1
rm -rf "$D"; mkdir -p "$D"
git -C "$REPO" archive "$1" src verification/probe verification/analysis | tar -x -C "$D"
cd "$D/verification/probe"
grep -q 'linear_emission.cpp ../../src/renderer/material_motion.cpp ../../src/renderer/linear_emission_pass' build_linear_distance_fade.sh ||
  sed -i '' 's#../../src/renderer/material_motion.cpp ../../src/renderer/linear_emission_pass.cpp -o build/distance-fade#../../src/renderer/linear_emission.cpp ../../src/renderer/material_motion.cpp ../../src/renderer/linear_emission_pass.cpp -o build/distance-fade#' build_linear_distance_fade.sh
sh build_linear_distance_fade.sh
cd "$REPO"
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 "$D/verification/probe/run_linear_distance_fade.py" \
  --exe "$D/verification/probe/build/distance-fade/linear_material_fixture.exe" \
  --composite "$D/verification/probe/build/distance-fade/composite.bin" --raw-dir "$D/raw" || true
echo "$1 $(grep '^RESULT' "$D/raw/report.txt")"
