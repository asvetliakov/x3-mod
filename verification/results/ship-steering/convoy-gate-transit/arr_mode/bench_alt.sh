#!/bin/sh
# bench24 (the fixture's lane cost bench, host timing) alternating 1c563443's fixture + core (base) and the working tree (head), three runs
# each, the load average before every run (review item 8, 2026-10-16). Usage, from the repository root:
#   sh verification/results/ship-steering/convoy-gate-transit/arr_mode/bench_alt.sh > verification/results/ship-steering/convoy-gate-transit/arr_mode/bench_alt.txt
# (no Wine; X3M_C_ONLY=none skips the casts, the bench runs first in --lane; sequential, nothing else of ours running)
set -e
H=$(mktemp -d)
mkdir -p "$H/base/src/proxy" "$H/base/verification/probe"
for f in $(git ls-tree --name-only 1c563443 src/proxy/ | grep -E 'ship_[a-z_]+_core\.h$'); do git show 1c563443:$f > "$H/base/$f"; done
git show 1c563443:verification/probe/ship_steering_host.cpp > "$H/base/verification/probe/ship_steering_host.cpp"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$H/base/verification/probe/ship_steering_host.cpp" -o "$H/base_host"
c++ -std=c++17 -O2 -Wall -Wextra -Werror verification/probe/ship_steering_host.cpp -o "$H/head_host"
for i in 1 2 3; do for b in base head; do
  echo "$b run $i load $(cut -d' ' -f1-3 /proc/loadavg)"
  X3M_C_ONLY=none "$H/${b}_host" --lane 2>/dev/null | grep 'lane cost (host) bench24' | sed 's/; key off.*//'
done; done
rm -rf "$H"
