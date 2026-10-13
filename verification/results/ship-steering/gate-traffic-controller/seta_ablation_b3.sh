#!/bin/sh
# B.3 item 1 ablation: the working tree's fixture with gc::age_bound forced to the nominal bounds (the B.2 rule) against the working tree
# as is, on the `run162 seta` variant (game steps of 1,500-2,000 ms on 2 % of frames); prints each build's B.3 / B.2 rows of the variant
# and its failure count. Usage: verification/results/ship-steering/gate-traffic-controller/seta_ablation_b3.sh > seta_ablation_b3.txt
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/src/proxy" "$TMP/verification/probe"
cp "$ROOT"/src/proxy/ship_*.h "$TMP/src/proxy/"
cp "$ROOT/verification/probe/ship_steering_host.cpp" "$TMP/verification/probe/"
sed -i 's/^    std::uint32_t b = nominal;$/    std::uint32_t b = nominal; return b; \/\/ ablation: nominal bounds/' "$TMP/src/proxy/ship_gate_ctl_core.h"
grep -q 'ablation: nominal bounds' "$TMP/src/proxy/ship_gate_ctl_core.h"
c++ -std=c++17 -O2 -w "$TMP/verification/probe/ship_steering_host.cpp" -o "$TMP/nominal"
c++ -std=c++17 -O2 -w "$ROOT/verification/probe/ship_steering_host.cpp" -o "$TMP/seta"
for exe in seta nominal; do
    "$TMP/$exe" --traffic > "$TMP/$exe.out" 2> "$TMP/$exe.err" || true
    echo "== $exe ($( [ $exe = seta ] && echo 'B.3: max(nominal, 2 x step, 4 x dt)' || echo 'nominal bounds, B.2' ))"
    grep -E '^traffic T run162 seta .* B\.3:' "$TMP/$exe.out" | sed 's/^traffic T //' | cut -c1-200
    grep -E '^traffic T run162 seta .* B\.2:' "$TMP/$exe.out" | sed -E 's/^traffic T (run162 seta +fps= *[0-9]+ seed=[0-9]+).* launched=([0-9]+\/[0-9]+).*/\1 launched=\2/'
    echo "min capsule gap per run (m): $(grep -E '^traffic T run162 seta .*min_capsule=' "$TMP/$exe.out" | sed -E 's/.*min_capsule=(-?[0-9]+).*/\1/' | tr '\n' ' ')"
    echo "failures in the variant: $(grep -c 'FAIL traffic T run162 seta' "$TMP/$exe.err" || true)"
done
