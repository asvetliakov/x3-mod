#!/bin/sh
# Round C: the lane cost bench (bench24 and abreast5, tf 1 / 10; the units and the bench only: X3M_C_ONLY names no cast) alternating
# d2871285 and the working tree, three times each, on one quiet host -> bench.txt. Usage: sh .../round_c/bench.sh (repository root).
set -e
OUT=$(dirname "$0")
H=$(mktemp -d)
mkdir -p "$H/base"
git archive d2871285 src/proxy verification/probe/ship_steering_host.cpp | tar -x -C "$H/base"
c++ -std=c++17 -O2 -Wall -Wextra -Werror verification/probe/ship_steering_host.cpp -o "$H/tree"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$H/base/verification/probe/ship_steering_host.cpp" -o "$H/base_bin"
: > "$OUT/bench.txt"
for i in 1 2 3; do
    for b in base_bin tree; do
        X3M_C_ONLY=none "$H/$b" --lane 2> /dev/null | grep -E '^lane cost \(host\) bench24' | sed "s/^/$b run $i: /" | cut -c1-200 >> "$OUT/bench.txt" || true
    done
done
rm -rf "$H"
