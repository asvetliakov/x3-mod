#!/bin/sh
# The gate traffic controller's key-off identity against amendment 17 (keyoff_identity.cpp). Extracts the amendment-17 sources of
# the base commit (default 77ddda08, the ratified design commit; src/proxy unchanged since e6189042) into a temporary directory with
# their namespace moved under a17::, builds the comparison against the working tree's headers and runs it.
# Usage: verification/results/ship-steering/gate-traffic-controller/keyoff_identity.sh [base-commit] > keyoff_identity.txt
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
BASE=${1:-77ddda08}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/a17"
for f in ship_steering_core.h ship_avoidance_core.h ship_capital_lane_core.h; do
    git -C "$ROOT" show "$BASE:src/proxy/$f" | sed 's/x3m::/a17::x3m::/g' > "$TMP/a17/$f"
done
c++ -std=c++17 -O2 -Wall -Wextra -I "$ROOT/src/proxy" -I "$TMP" "$HERE/keyoff_identity.cpp" -o "$TMP/keyoff_identity"
echo "base $BASE ($(git -C "$ROOT" rev-parse --short "$BASE")) vs working tree $(git -C "$ROOT" describe --always --dirty --abbrev=8)"
"$TMP/keyoff_identity"
