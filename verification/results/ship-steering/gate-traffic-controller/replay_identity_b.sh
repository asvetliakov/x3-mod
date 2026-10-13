#!/bin/sh
# Stage B's Run160b replay identity: ship_trace_replay.py (it imports ship_trace_rows.py, whose ship_trace row gained the optional
# cap_ms / cap_age_ms tail) and replay_lag.py from the base commit (default ad0e131a, main) and from the working tree, on the Run160b
# log; stdout, the JSON and the summary must be byte-identical. Usage: replay_identity_b.sh <x3m.log> [base-commit] > replay_identity_b.txt
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../.." && pwd)
LOG=$1
BASE=${2:-ad0e131a}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/base/verification/probe" "$TMP/base/verification/results/ship-steering"
for f in verification/probe/ship_trace_replay.py verification/probe/ship_trace_rows.py verification/results/ship-steering/replay_lag.py; do
    git -C "$ROOT" show "$BASE:$f" > "$TMP/base/$f"
done
echo "base $BASE ($(git -C "$ROOT" rev-parse --short "$BASE")) vs working tree $(git -C "$ROOT" describe --always --dirty --abbrev=8); log $(basename "$LOG") $(stat -c %s "$LOG") B"
python3 -I "$TMP/base/verification/probe/ship_trace_replay.py" "$LOG" --json "$TMP/before.json" > "$TMP/before.out"
python3 -I "$TMP/base/verification/results/ship-steering/replay_lag.py" "$TMP/before.json" > "$TMP/before.lag"
python3 -I "$ROOT/verification/probe/ship_trace_replay.py" "$LOG" --json "$TMP/after.json" > "$TMP/after.out"
python3 -I "$ROOT/verification/results/ship-steering/replay_lag.py" "$TMP/after.json" > "$TMP/after.lag"
rc=0
for k in out json lag; do
    if cmp -s "$TMP/before.$k" "$TMP/after.$k"; then echo "$k: byte-identical ($(stat -c %s "$TMP/after.$k") B)"; else echo "$k: DIFFERS"; rc=1; fi
done
[ $rc -eq 0 ] && echo "RESULT PASS" || echo "RESULT FAIL"
exit $rc
