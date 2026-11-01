#!/bin/sh
# Counts bloom_prepare outcomes per retained flight log (native AP runs, FL run 7) and bloom_commit rows in the
# Wine-era row-type summaries. Read-only; prints one line per file.
cd "$(dirname "$0")/.." || exit 1
for f in $(grep -rl "^bloom_prepare" . | grep '\.log$' | sort) /mnt/c/X3FL/x3m.log; do
  printf '%s ready1=%s invalid=%s %s\n' "$f" "$(grep -c '^bloom_prepare.*ready=1' "$f")" \
    "$(grep -c '^bloom_prepare.*reason=invalid operation=80070057' "$f")" \
    "$(grep -m1 -o 'source_commit=[0-9a-f]\{8\}' "$f")"
done
grep -H "bloom_commit\|bloom_prepare" run312-run82a-launch1/rowtypes_out.txt run323-run83a-launch1-3/rowtypes_out.txt
