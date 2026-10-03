#!/usr/bin/env python3
"""Gap 5 precondition (docs/architecture/engine-exhaust-gap-analysis.md): how far each body's peak_linear differs from
its mean_linear, both divided by their largest channel as the proxy parses them (engine_effects_core.h Body), per
cluster, with the peak's luminance over the mean's (Rec. 709) and one example pair per cluster.
Usage: plume_two_tone_colours.py <engine_bodies.json>; plume_two_tone_colours_out.txt is the X3 bottle's installed
x3m/engine_bodies.json (2026-10-01, Mayhem install)."""
import json
import sys
from collections import defaultdict

path = sys.argv[1]
data = json.load(open(path))
bodies = data["bodies"] if isinstance(data, dict) and "bodies" in data else data
items = bodies.values() if isinstance(bodies, dict) else bodies


def norm(c):
    m = max(c)
    return [x / m for x in c] if m > 0 else [1.0, 1.0, 1.0]


def luma(c):
    return 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]


per = defaultdict(list)
lum = defaultdict(list)
example = {}
for b in items:
    if not isinstance(b, dict) or not b.get("mean_linear") or not b.get("peak_linear"):
        continue
    m, p = norm(b["mean_linear"]), norm(b["peak_linear"])
    c = str(b.get("cluster", "?"))
    per[c].append(max(abs(a - q) for a, q in zip(m, p)))
    lum[c].append(luma(p) / max(luma(m), 1e-6))
    example.setdefault(c, (m, p))
total = within = 0
for c in sorted(per):
    v = sorted(per[c])
    r = sorted(lum[c])
    n5 = sum(1 for x in v if x <= 0.05)
    total += len(v)
    within += n5
    m, p = example[c]
    print(f"cluster={c} bodies={len(v)} max_channel_diff median={v[len(v) // 2]:.3f} max={v[-1]:.3f} within_5pct={n5} "
          f"luma_peak_over_mean median={r[len(r) // 2]:.2f} "
          f"mean=({m[0]:.2f},{m[1]:.2f},{m[2]:.2f}) peak=({p[0]:.2f},{p[1]:.2f},{p[2]:.2f})")
print(f"total bodies={total} within_5pct={within} ({100.0 * within / max(total, 1):.1f} %)")
