#!/bin/sh
# 2026-10-16 rerun after the one-read-port repack in linear_material.cpp:
# builds gen_widen_variants against the working tree, generates the gain,
# widen and swiden variants of the 16 captured originals, validates them with
# the native SysWOW64 d3d9 validator (native_validate.exe through WSL interop;
# no device, nothing launched), folds the output (summarize.py ->
# result-generator-fix.json) and checks the widened bytes against the
# pre-fix generator plus patch_one_port.py, byte for byte.
# usage: rerun_generator_fix.sh <scratch dir> [<pre-fix commit, default HEAD>]
# Generated shader bytes stay in the scratch dir (game-derived; never commit).
set -e
V=$(cd "$(dirname "$0")" && pwd)
R=$(cd "$V/../../.." && pwd)
S=$1; BASE=${2:-HEAD}
CAPTURES=/mnt/c/X3/x3-modern-captures
HASHES=$(python3 -c "import json,sys;print(' '.join(sorted({r['original'] for r in json.load(open(sys.argv[1]))['rows']})))" "$V/result.json")
mkdir -p "$S/out" "$S/old" "$S/oldout" "$S/patched"
g++ -std=c++17 -O2 "$V/gen_widen_variants.cpp" "$R/src/renderer/linear_material.cpp" "$R/src/renderer/material_motion.cpp" -o "$S/gen"
i686-w64-mingw32-g++ -O1 -static "$V/native_validate.cpp" -o "$S/native_validate.exe"
(cd "$S" && ./gen "$CAPTURES" out $HASHES > gen.txt && ./native_validate.exe out/*.bin > native.txt)
python3 "$V/summarize.py" "$V/result-generator-fix.json" "$S/native.txt"
git -C "$R" show "$BASE:src/renderer/linear_material.cpp" > "$S/old/linear_material.cpp"
g++ -std=c++17 -O2 -I"$R/src/renderer" "$V/gen_widen_variants.cpp" "$S/old/linear_material.cpp" "$R/src/renderer/material_motion.cpp" -o "$S/gen_old"
(cd "$S" && ./gen_old "$CAPTURES" oldout $HASHES > /dev/null)
same=0; diff=0
for f in "$S"/oldout/*widen.bin; do
  b=$(basename "$f")
  python3 "$V/patch_one_port.py" "$f" "$S/patched/$b" > /dev/null
  if cmp -s "$S/patched/$b" "$S/out/$b"; then same=$((same+1)); else diff=$((diff+1)); fi
done
echo "widened bytes equal to the patch witness: $same, different: $diff"
