#!/bin/sh
# Review of the arrival round item 3, fail-before: item4_cases.cpp against 97f3804b's src/proxy and the working tree's (item4_before.txt).
# Usage: sh verification/results/ship-steering/convoy-gate-transit/arrival_round/review/item4_before.sh (from the repository root).
set -e
OUT=$(dirname "$0")
H=$(mktemp -d)
git archive 97f3804b src/proxy | tar -x -C "$H"
for v in old new; do
    inc=src/proxy; [ $v = old ] && inc="$H/src/proxy"
    c++ -std=c++17 -O2 -Wall -Wextra -I"$inc" "$OUT/item4_cases.cpp" -o "$H/$v"
    echo "== $v ($([ $v = old ] && echo 97f3804b || echo working tree))"; "$H/$v"
done > "$OUT/item4_before.txt"
rm -rf "$H"
