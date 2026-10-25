#!/bin/sh
# Native d3d9 validator run over the material-strength twins (docs/architecture/overlay-material-strength.md 7.2):
# builds verification/probe/material_strength_twin_structure.cpp against the working tree (host g++), generates
# every parent form and its twin for the local corpus into the scratch dir, builds the SysWOW64 validator driver
# (verification/results/hull-widen-native-validator/native_validate.cpp, i686 MinGW, static) and runs it through
# WSL interop on every twin (no device, nothing launched). Writes native_validate_twins.json beside this script
# (names, sizes, verdicts, message counts only). Generated shader bytes stay in the scratch dir (game-derived).
# usage: native_validate_twins.sh <scratch dir> [<corpus, default /tmp/x3-shader-sweep/programs>]
set -e
V=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$V/../../.." && pwd)
S=$1; CORPUS=${2:-/tmp/x3-shader-sweep/programs}
rm -rf "$S/twins"; mkdir -p "$S/twins"
g++ -std=c++17 -O2 "$R/verification/probe/material_strength_twin_structure.cpp" "$R/src/renderer/linear_material.cpp" \
    "$R/src/renderer/material_motion.cpp" -o "$S/twin_gen"
"$S/twin_gen" "$CORPUS" "$S/twins" > "$S/twins.json"
i686-w64-mingw32-g++ -O1 -static "$R/verification/results/hull-widen-native-validator/native_validate.cpp" -o "$S/native_validate.exe"
python3 "$V/native_validate_run.py" "$S" "$S/native_validate.exe"
