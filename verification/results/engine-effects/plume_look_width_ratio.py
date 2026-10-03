#!/usr/bin/env python3
"""The visible half-width of the side-view bands against the previous (slab) law (docs/architecture/
engine-exhaust-look-critique.md section 6, "Tuning pass"): the 10 % half-width at u 0.2 ('hw px' of
plume_look_metrics.py: display-decoded luma under 0.1 of the column peak) of plume_look_metrics_out.txt (the current
dump) over plume_look_metrics_before_out.txt (the images of e51872be), per band; the brief's target 0.6..0.7.
Usage: python3 plume_look_width_ratio.py [after.txt] (default plume_look_metrics_out.txt)."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))


def widths(path):
    out = {}
    for line in open(path):
        m = re.match(r"^(\S.*?) \| \d+ \| [0-9.]+ \| \d+ \| [0-9.na]+ \| [0-9.na]+ \((\d+)\)", line)
        if m:
            out[m.group(1)] = int(m.group(2))
    return out


after = widths(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "plume_look_metrics_out.txt"))
before = widths(os.path.join(ROOT, "plume_look_metrics_before_out.txt"))
print("band | hw before px | hw after px | after / before")
ratios = []
for band, b in before.items():
    if band in after:
        r = after[band] / b
        print(f"{band} | {b} | {after[band]} | {r:.2f}")
        if "s=0" not in band and "s=0.5" not in band and "n40" not in band:
            ratios.append((r, band))
ratios.sort()
print(f"# s = 1 capped bands: {ratios[0][0]:.2f} ({ratios[0][1]}) .. {ratios[-1][0]:.2f} ({ratios[-1][1]}); "
      f"in 0.6..0.7: {sum(0.6 <= r <= 0.7 for r, _ in ratios)} of {len(ratios)}")
