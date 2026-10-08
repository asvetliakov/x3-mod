#!/bin/sh
# Builds legacy_cost.exe: the Run137 occlusion cull pass (commit 833f1ac7) on the realistic-state scene, for the
# "before" row of verification/results/occlusion-cull-batched. The commit's pass and core are extracted with git show
# into <out>, never into the repository.
#   sh verification/results/occlusion-cull-batched/legacy_build.sh <out-dir>
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
OUT=${1:?out dir}
COMMIT=833f1ac7
mkdir -p "$OUT/src/renderer" "$OUT/src/proxy"
for f in src/renderer/occlusion_cull_pass.cpp src/renderer/occlusion_cull_pass.h src/proxy/occlusion_cull_core.h \
         src/proxy/cull_census_core.h; do
    git -C "$ROOT" show "$COMMIT:$f" > "$OUT/$f"
done
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 \
    -DWIN32_LEAN_AND_MEAN -DNOMINMAX -I "$OUT/src/renderer" -I "$ROOT/verification/probe" \
    "$ROOT/verification/results/occlusion-cull-batched/legacy_cost.cpp" "$OUT/src/renderer/occlusion_cull_pass.cpp" \
    -static -static-libgcc -static-libstdc++ -luser32 -o "$OUT/legacy_cost.exe"
echo "$COMMIT" > "$OUT/BUILT_FROM"
echo "$OUT/legacy_cost.exe"
