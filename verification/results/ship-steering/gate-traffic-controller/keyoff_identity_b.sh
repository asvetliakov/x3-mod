#!/bin/sh
# The traffic controller's stage-B key-off identity (keyoff_identity_b.cpp). Extracts the sources of
# the base commit (default ad0e131a: stage A + amendment 18, main, Run161) into a temporary directory with their namespace moved under sa::, builds the
# comparison against the working tree's headers and runs it.
# Usage: verification/results/ship-steering/gate-traffic-controller/keyoff_identity_b.sh [base-commit] > keyoff_identity_b.txt
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
BASE=${1:-ad0e131a}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/sa"
for f in ship_steering_core.h ship_avoidance_core.h ship_capital_lane_core.h; do
    git -C "$ROOT" show "$BASE:src/proxy/$f" | sed 's/x3m::/sa::x3m::/g' > "$TMP/sa/$f"
done
c++ -std=c++17 -O2 -Wall -Wextra -I "$ROOT/src/proxy" -I "$TMP" "$HERE/keyoff_identity_b.cpp" -o "$TMP/keyoff_identity"
echo "base $BASE ($(git -C "$ROOT" rev-parse --short "$BASE")) vs working tree $(git -C "$ROOT" describe --always --dirty --abbrev=8)"
"$TMP/keyoff_identity"
