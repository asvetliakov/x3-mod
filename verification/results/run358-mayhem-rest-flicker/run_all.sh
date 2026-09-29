#!/bin/bash
# Run 358 (Mayhem 3, release 0.7.0, 5120x1440, --debug --perf --taa-debug): rest shimmer triage. Baseline run356 (Run 100 A).
cd "$(dirname "$0")"; R=/tmp/x3-bottleX3-run358; S=${TMPDIR:-/tmp}
python3 log_rows.py $R > log_rows_out.txt
{ python3 -W ignore flicker_map.py $R 2030 2052 $S/fm1.png; python3 -W ignore flicker_map.py $R 4514 4545 $S/fm3.png; } > flicker_map_out.txt
F() { python3 -W ignore stages.py "$@"; }
{ echo "== burst1 ship"; F $R 2030 2052 2300 1000 2820 1440 50 1000; echo "== burst1 station"; F $R 2030 2052 2760 140 2980 310 55000 75000; } > stages_b1_out.txt
{ echo "== burst3 ship"; F $R 4514 4545 2300 1000 2820 1440 50 1000; echo "== burst3 station centre"; F $R 4514 4545 2330 470 2640 790 100000 130000; echo "== burst3 station top-right"; F $R 4514 4545 4780 90 4960 190 120000 145000; } > stages_b3_out.txt
{ echo "== run356 rest spacedock (baseline)"; F /tmp/x3-bottleX3-run356 1087 1094 574 421 988 548 55000 100000 1920 1080; echo "== run356 rest outpost"; F /tmp/x3-bottleX3-run356 1087 1094 813 430 1096 674 120000 190000 1920 1080; } > stages_run356_out.txt
python3 routes.py $R 2030 2052 4514 4545 > routes_out.txt; python3 routes.py /tmp/x3-bottleX3-run356 1087 1094 > routes_run356_out.txt
