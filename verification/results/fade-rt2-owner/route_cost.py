#!/usr/bin/env python3
"""Per-draw route cost of the fade owner in the motion-output fixture (docs/architecture/fade-rt2-ownership.md section 6):
the `motion_output_frame` lines of each `seam-taa-fade-route-<script>-owner` case against its option-off twin, from the
capture logs the full run copies to verification/results/bottle-X3/motion-output-<case>-capture.log. Per case, the mean
over frames 1-11 (frame 0 carries first-execution translation) of route_draw_us (all routed draws of the frame: the
full-screen A plus the routed quads), gate_us, set_rt_us and lazy_mask_writes, and route_draw_us per routed draw.
CPU QPC under FEX with telemetry draw metrics: diagnostic timings of a 64 x 64 fixture, not game frame time.
usage: route_cost.py"""
import re
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RESULTS = ROOT / 'verification/results/bottle-X3'
PAIRS = [('routed', 'routed-owner'), ('routed-perdraw', 'routed-perdraw-owner'), ('sentinel', 'sentinel-owner'), ('hover', 'hover-owner'),
         ('original', 'original-owner'), ('behind', 'behind-owner'), ('overlay', 'overlay-owner'), ('foreign', 'foreign-owner'), ('hull', 'hull-owner')]
FIELDS = ('route_draw_us', 'gate_us', 'set_rt_us', 'lazy_mask_writes', 'routed')


def frames(case):
    path = RESULTS / f'motion-output-seam-taa-fade-route-{case}-capture.log'
    rows = []
    for line in path.read_text(errors='replace').splitlines():
        if line.startswith('motion_output_frame '):
            f = dict(re.findall(r'(\w+)=(\S+)', line))
            if 1 <= int(f['frame']) <= 11:
                rows.append({k: float(f[k]) for k in FIELDS})
    return rows


def summary(rows):
    out = {k: statistics.mean(r[k] for r in rows) for k in FIELDS}
    per = [r['route_draw_us'] / r['routed'] for r in rows if r['routed']]
    out['route_draw_us_per_routed_draw'] = statistics.mean(per) if per else 0.0
    return out


print('case frames ' + ' '.join(FIELDS) + ' route_draw_us_per_routed_draw')
for off, on in PAIRS:
    for case in (off, on):
        rows = frames(case)
        s = summary(rows)
        print(f'{case} {len(rows)} ' + ' '.join(f'{s[k]:.2f}' for k in FIELDS) + f" {s['route_draw_us_per_routed_draw']:.2f}")
