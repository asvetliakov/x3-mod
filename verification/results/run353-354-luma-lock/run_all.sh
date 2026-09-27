#!/bin/bash
# Run 353 (lock off) / 354 (--taa-luma-lock 16), 1920x1080. Both sessions taa_debug=0: no taa_/present_ dumps,
# so the resolved-image flicker/sharpness scripts (run351-352-far-weight/*_1080.py) cannot run.
cd "$(dirname "$0")"
python3 log_rows.py /tmp/x3-bottleX3-run353 /tmp/x3-bottleX3-run354 > log_rows_out.txt
for a in "run353 1454" "run353 1817" "run353 1820" "run354 1408" "run354 1836" "run354 1839" "run354 4996"; do set -- $a
  echo "== $1 $2"; python3 -W ignore station_blobs_1080.py /tmp/x3-bottleX3-$1 $2 2000; done > station_blobs_1080_out.txt
for r in 353 354; do echo "== run$r dumped kinds"; ls /tmp/x3-bottleX3-run$r | grep -E '_1_[0-9]+\.' | sed 's/_[0-9]*\..*//' | sort | uniq -c
  L=$(ls /tmp/x3-bottleX3-run$r/session-*.log); echo "reset/lost rows: $(grep -cE '^[a-z_]*(reset|lost|cooperative)' $L)"
  grep -E '^cull_census device=1 frame=(1408|1454) ' $L | grep -i 'body=stations' | grep ' verdict=kept' | grep -oE ' d=\S+| radius=\S+| body=\S+' | paste - - - | sort -u | sort -t= -k2 -n | head -6; done > dumps_names_out.txt
python3 bounds_names.py /tmp/x3-bottleX3-run354 1408 1836 1839 > bounds_names_out.txt; python3 bounds_names.py /tmp/x3-bottleX3-run353 1454 1817 1820 >> bounds_names_out.txt
for a in "run354 1408 599 420 990 509" "run354 1836 624 440 1010 533" "run354 1839 420 610 846 710" "run353 1454 574 432 968 548" "run353 1817 671 327 1058 442" "run353 1820 578 450 970 561"; do set -- $a
  python3 -W ignore shipyard_footprint.py /tmp/x3-bottleX3-$1 $2 $3 $4 $5 $6 55000 125000; done > shipyard_footprint_out.txt
