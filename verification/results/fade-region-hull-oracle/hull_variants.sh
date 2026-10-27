#!/bin/bash
# fade_region_host --hull across compilers / contraction settings and seeds (2026-10-27, x86_64 WSL2).
# Usage: hull_variants.sh <repo-root> <scratch-dir>. Prints one RESULT line (and any fail=1 row) per build.
# Measured: seed 20260914 x 600 failed only index 483 (hull_px 559416 > aabb_px 558764 against the exact-cut box)
# under every build before the same-plane box; 0 failures after it under every build. Seeds 1-9 x 2000 leave one
# fail=1 row (seed 1 index 1827, a NonPositiveW refusal with zn 0.01 < eps/k), present before the change too.
set -u
R=$1 S=$2
SRC=$R/verification/probe/fade_region_host.cpp
build() { "$@" -std=c++17 -O2 -Wall -Wextra -Werror "$SRC" -o "$S/frh_variant" && echo "$* : $("$S/frh_variant" --hull 20260914 600 64 | grep -E 'fail=1|RESULT' | cut -c1-200 | tr '\n' ' ')"; }
build clang++
build clang++ -ffp-contract=off
build clang++ -mfma -ffp-contract=on
build clang++ -mfma -ffp-contract=fast
build c++
build c++ -ffp-contract=off
build c++ -mfma
clang++ -std=c++17 -O2 "$SRC" -o "$S/frh_variant"
for seed in 1 2 3 4 5 6 7 8 9; do echo "seed $seed: $("$S/frh_variant" --hull $seed 2000 64 | grep -E 'fail=1|RESULT' | cut -c1-200 | tr '\n' ' ')"; done
