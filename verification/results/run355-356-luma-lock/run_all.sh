#!/bin/bash
# Run 355 (lock off) / 356 (--taa-luma-lock 16, a627f5b5), 1920x1080, taa_debug=1 both.
cd "$(dirname "$0")"
python3 log_rows.py /tmp/x3-bottleX3-run355 /tmp/x3-bottleX3-run356 > log_rows_out.txt
{ python3 bounds_names.py /tmp/x3-bottleX3-run355 1627 1634 2322 2326 2329 2427 2434; python3 bounds_names.py /tmp/x3-bottleX3-run356 1087 1094 1470 1474 1477 1569 1576; } > bounds_names_out.txt
python3 boxes.py > boxes_out.txt
for a in "355 1627" "355 2322" "355 2427" "356 1087" "356 1470" "356 1569"; do set -- $a; echo "== run$1 $2"; python3 -W ignore burst_metrics.py /tmp/x3-bottleX3-run$1 $2 8; done > burst_metrics_out.txt
F() { python3 -W ignore rest_flicker_1080.py "$@" ; }
{ echo "spacedock 355"; F /tmp/x3-bottleX3-run355 1627 8 581 431 996 578 55000 100000; echo "spacedock 356"; F /tmp/x3-bottleX3-run356 1087 8 574 421 988 548 55000 100000
  echo "outpost 355"; F /tmp/x3-bottleX3-run355 1627 8 818 435 1112 680 120000 190000; echo "outpost 356"; F /tmp/x3-bottleX3-run356 1087 8 813 430 1096 674 120000 190000
  echo "after-pan spacedock 355"; F /tmp/x3-bottleX3-run355 2429 6 194 729 718 912 55000 100000; echo "after-pan spacedock 356"; F /tmp/x3-bottleX3-run356 1571 6 270 643 760 787 55000 100000; } > rest_flicker_1080_out.txt
python3 gap_ms.py /tmp/x3-bottleX3-run355 2329 2434 > gap_ms_out.txt; python3 gap_ms.py /tmp/x3-bottleX3-run356 1477 1576 >> gap_ms_out.txt
python3 settle.py /tmp/x3-bottleX3-run356 1477 1569 > settle_out.txt; python3 settle.py /tmp/x3-bottleX3-run355 2329 2427 >> settle_out.txt
