#!/bin/sh
# Scenario audit: station part-model outcome over one flight log (per-interval counters summed).
# usage: part_model_tally.sh [x3m.log]
LOG=${1:-/mnt/c/X3/x3m.log}
grep -a '^ship_steering_summary ' "$LOG" | grep -o 'part_[a-z]*=[0-9]*\|snapshot_overflow=[0-9]*' |
  awk -F= '{s[$1]+=$2} END {for (k in s) printf "%s %d\n", k, s[k]}' | sort
