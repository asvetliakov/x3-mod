#!/bin/sh
# Pan12.5 rows of the committed temporal pass record (MOTION_WEIGHT, both programs, off/on): e_ratio, ripple_rms, weight.
grep "MOTION_WEIGHT .*row=pan12.5" "$(dirname "$0")/../bottle-X3/temporal-pass.txt" | tr -d '\r' | sort -u |
  sed -E 's/.*program=([a-z_]+).* on=([0-9]) e_ratio=([0-9.]+).*ripple_rms=([0-9.]+).*output_diff=([0-9.]+).*weight=([0-9.]+)/\1 on=\2 e_ratio=\3 ripple_rms=\4 output_diff=\5 weight=\6/'
