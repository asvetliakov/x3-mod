#!/bin/sh
# B.5 fixture identity: the base commit's fixture (default 447008ab, main after B.4) and the working tree's, each built from its own sources
# and run in the three acceptance modes (full, --capital, X3M_SEQ_OFF); fixture_lines_b5.py compares every line, scenario T included,
# with the host timings masked and the new 'b5 ' lines set apart: the proofs alone (X3M_B5_NO_LEAVE=1) must leave every line equal;
# with the leaves only scenario T lines may differ (the launched members leave the board). Usage: fixture_identity_b5.sh [base] > fixture_lines_b5.txt
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
BASE=${1:-447008ab}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/base/src/proxy" "$TMP/base/verification/probe"
for f in $(git -C "$ROOT" ls-tree --name-only "$BASE" src/proxy/ | grep -E '/ship_.*\.h$'); do git -C "$ROOT" show "$BASE:$f" > "$TMP/base/$f"; done
git -C "$ROOT" show "$BASE:verification/probe/ship_steering_host.cpp" > "$TMP/base/verification/probe/ship_steering_host.cpp"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$TMP/base/verification/probe/ship_steering_host.cpp" -o "$TMP/before"
c++ -std=c++17 -O2 -Wall -Wextra -Werror "$ROOT/verification/probe/ship_steering_host.cpp" -o "$TMP/after"
echo "base $BASE ($(git -C "$ROOT" rev-parse --short "$BASE")) vs working tree $(git -C "$ROOT" describe --always --dirty --abbrev=8)"
for exe in before after; do
    "$TMP/$exe" > "$TMP/$exe.full" 2>&1 || true
    "$TMP/$exe" --capital > "$TMP/$exe.capital" 2>&1 || true
    X3M_SEQ_OFF=1 "$TMP/$exe" > "$TMP/$exe.seqoff" 2>&1 || true
done
X3M_B5_NO_LEAVE=1 "$TMP/after" > "$TMP/noleave.full" 2>&1 || true
X3M_B5_NO_LEAVE=1 "$TMP/after" --capital > "$TMP/noleave.capital" 2>&1 || true
X3M_B5_NO_LEAVE=1 X3M_SEQ_OFF=1 "$TMP/after" > "$TMP/noleave.seqoff" 2>&1 || true
rc=0
for mode in full capital seqoff; do
    echo "mode $mode, proofs only (X3M_B5_NO_LEAVE=1):"
    python3 "$HERE/fixture_lines_b5.py" "$TMP/before.$mode" "$TMP/noleave.$mode" || rc=1
    echo "mode $mode, B.5 (proofs and leaves):"
    python3 "$HERE/fixture_lines_b5.py" --t-may-differ "$TMP/before.$mode" "$TMP/after.$mode" || rc=1
    grep '^b5 ' "$TMP/after.$mode" | cut -c1-200
done
exit $rc
