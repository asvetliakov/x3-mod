#!/bin/sh
# Amendment 18's key-off identity against the gate traffic controller's stage A (keyoff_identity.cpp). Extracts the stage-A sources of
# the base commit (default 23184932, stage A reviewed) into a temporary directory with their namespace moved under sa::, builds the
# comparison against the working tree's headers and runs it.
# Usage: verification/results/ship-steering/docking-approach/keyoff_identity.sh [base-commit] > keyoff_identity.txt
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
BASE=${1:-23184932}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/sa"
for f in ship_steering_core.h ship_avoidance_core.h ship_capital_lane_core.h; do
    git -C "$ROOT" show "$BASE:src/proxy/$f" | sed 's/x3m::/sa::x3m::/g' > "$TMP/sa/$f"
done
c++ -std=c++17 -O2 -Wall -Wextra -I "$ROOT/src/proxy" -I "$TMP" "$HERE/keyoff_identity.cpp" -o "$TMP/keyoff_identity"
echo "base $BASE ($(git -C "$ROOT" rev-parse --short "$BASE")) vs working tree $(git -C "$ROOT" describe --always --dirty --abbrev=8)"
"$TMP/keyoff_identity"
