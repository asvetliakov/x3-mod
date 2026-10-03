#!/usr/bin/env python3
"""Before / after of the plume look metrics (plume_look_metrics.py's output) per side band, plus the 02b disc gate and
the 05 far dots (docs/architecture/engine-exhaust-look-critique.md section 6, "Review fixes"). Before: the tuning
pass's images (8eb0514b, plume_look_metrics_tuned_out.txt); after: plume_look_metrics_out.txt (the current dump).
Usage: python3 plume_look_metrics_compare.py [before.txt] [after.txt]; the output is plume_look_metrics_compare_out.txt.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
BEFORE = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "plume_look_metrics_tuned_out.txt")
AFTER = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "plume_look_metrics_out.txt")
COLUMNS = ("peak disp", "core/edge u.2", "core/edge u.5", "white head u.1", "white body u.5", "aniso", "lane cells", "lane gaps")


def parse(path):
    side, extra = {}, {}
    for line in open(path):
        cells = [c.strip() for c in line.split("|")]
        if len(cells) == 10 and not line.startswith("band"):
            head, body = (float(v) for v in cells[7].split("/"))
            lane, gaps = (float(v) for v in cells[9].split("/"))
            side[cells[0]] = dict(zip(COLUMNS, (float(cells[3]), float(cells[5].split()[0]), float(cells[6]), head, body,
                                                float(cells[8].split("=")[1]), lane, gaps)))
        m = re.match(r"\s+(red|blue): ring ([0-9.]+) at", line)
        if m:
            extra[f"02b ring {m.group(1)}"] = float(m.group(2))
        m = re.match(r"\s+(\d+) px: ([0-9.]+) \(before", line)
        if m:
            extra[f"05 dot {m.group(1)} px (display sum)"] = float(m.group(2))
    return side, extra


b_side, b_extra = parse(BEFORE)
a_side, a_extra = parse(AFTER)
print(f"# before {os.path.basename(BEFORE)}, after {os.path.basename(AFTER)}; per band: before -> after")
print("band | " + " | ".join(COLUMNS))
for band, a in a_side.items():
    b = b_side.get(band)
    if b:
        print(f"{band} | " + " | ".join(f"{b[c]:g} -> {a[c]:g}" for c in COLUMNS))
print("# disc and far dots")
for key, a in a_extra.items():
    if key in b_extra:
        print(f"{key}: {b_extra[key]:g} -> {a:g}")
