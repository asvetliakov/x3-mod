#!/bin/sh
# Amendment 18's fixture identity: the stage-A fixture (base commit, default 23184932) and the working tree's, each built from its own
# sources and run in the three acceptance modes (full, --capital, X3M_SEQ_OFF); fixture_lines.py compares the pre-existing lines with
# the host timings masked and counts scenario N's new lines. Usage: fixture_identity.sh [base-commit] > fixture_lines.txt
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
BASE=${1:-23184932}
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
rc=0
for mode in full capital seqoff; do
    echo "mode $mode:"
    python3 "$HERE/fixture_lines.py" "$TMP/before.$mode" "$TMP/after.$mode" || rc=1
done
exit $rc
