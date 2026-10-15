#!/bin/bash
# Section 13.1's distance condition, cast rt_far's fail-before (convoy-gate-transit.md section 13.11 "Distance condition"): the working
# tree's fixture against --base's src/proxy (gc::arr_return_test absent: the fixture's X3M_GC_HAS_ARR_FAR switch takes the record test
# alone) and against the working tree, --lane, rt_far only (X3M_C_ONLY); prints the Rf rows and the failure counts per check.
#   bash arr_far/fail_before.sh [base [base-src.tar]] > arr_far/fail_before.txt     (no Wine, no game; about 1 minute; the tar, from
#   git archive -o base-src.tar <base> src, replaces the script's own git archive)
set -e
base=${1:-67db4e3d}
root=$(cd "$(dirname "$0")/../../../../.." && pwd)
work=$(mktemp -d)
mkdir -p "$work/before/verification/probe" "$work/after/verification/probe"
if [ -n "$2" ]; then tar -x -C "$work/before" -f "$2"; else git -C "$root" archive "$base" src | tar -x -C "$work/before"; fi
cp -r "$root/src" "$work/after/"
for t in before after; do
    cp "$root/verification/probe/ship_steering_host.cpp" "$work/$t/verification/probe/"
    c++ -std=c++17 -O2 -Wall -Wextra -Werror "$work/$t/verification/probe/ship_steering_host.cpp" -o "$work/$t/host"
    X3M_C_ONLY=rt_far X3M_C_ALL=1 "$work/$t/host" --lane > "$work/$t.txt" 2>&1 || true
    echo "== $t ($([ $t = before ] && echo "core $base" || echo 'working tree core'))"
    grep -o 'scenario C rt_far rf tf .*' "$work/$t.txt"
    grep -o 'FAIL scenario C rt_far tf [0-9]* seed [0-9]*: [^|]*' "$work/$t.txt" | sed 's/ tf [0-9]* seed [0-9]*//' | sort | uniq -c
    grep -o 'ship_steering_host checks=[0-9]* failures=[0-9]*' "$work/$t.txt"
done
echo "(not rt_far's: the cast filter leaves the key-off control summary without runs, its check fails in both builds; the lane cost bench (host timing) may fail under load)"
rm -rf "$work"
