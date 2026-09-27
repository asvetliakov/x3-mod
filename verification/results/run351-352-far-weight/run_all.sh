#!/bin/sh
# Run351 (far 0.95) vs run352 (far 0.9), 1920x1080 captures; boxes from station_blobs_1080_out.txt.
cd "$(dirname "$0")"
S="python3 -W ignore sharpness_measured_1080.py"; F="python3 -W ignore rest_flicker_1080.py"
{
echo "## sharpness: far blob (z 120-200k) rest / pan, near blob (z 55-80k rest, 85-120k pan)"
$S /tmp/x3-bottleX3-run351 1797 620 370 1064 575 120000 200000
$S /tmp/x3-bottleX3-run352 908 611 379 1047 581 120000 200000
$S /tmp/x3-bottleX3-run351 2173 400 669 889 875 120000 200000
$S /tmp/x3-bottleX3-run352 1419 590 463 1028 664 120000 200000
$S /tmp/x3-bottleX3-run351 1797 1533 269 1920 375 55000 80000
$S /tmp/x3-bottleX3-run352 908 1536 250 1893 353 55000 80000
$S /tmp/x3-bottleX3-run351 2173 1267 533 1609 650 85000 120000
$S /tmp/x3-bottleX3-run352 1420 1380 428 1748 526 85000 120000
echo "## rest flicker (8-frame rest bursts)"
echo "== run351 far"; $F /tmp/x3-bottleX3-run351 1797 8 620 370 1064 575 120000 200000
echo "== run352 far"; $F /tmp/x3-bottleX3-run352 908 8 611 379 1047 581 120000 200000
echo "== run351 near"; $F /tmp/x3-bottleX3-run351 1797 8 1533 269 1920 375 55000 80000
echo "== run352 near"; $F /tmp/x3-bottleX3-run352 908 8 1536 250 1893 353 55000 80000
echo "## baseline 0.985 at 5120x1440 (run341), far blob, original script"
echo "== run341 far"; python3 -W ignore ../run341-343-run92a-weights/rest_flicker.py /tmp/x3-bottleX3-run341 810 8 2049 561 2641 831 120000 200000
} 2>&1
