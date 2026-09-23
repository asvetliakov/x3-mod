#!/usr/bin/env python3
"""Per session segment: additive admissions, refusals, pair-mask bits seen, frames with any admission.
Segments split at the load/menu frames found in frame_end dt (see fog_runs.txt)."""
import sys, re
from collections import defaultdict
cuts = [0, 429, 11113, 12160, 15872, 16258, 19555, 24765, 31511, 33817, 35513, 10**9]
seg = lambda f: max(i for i, c in enumerate(cuts) if f >= c)
agg = defaultdict(lambda: [0, 0, 0, 0, set()])
for l in open(sys.argv[1], errors='replace'):
    if not l.startswith('screen_emission_additive_frame '): continue
    d = dict(re.findall(r'(\w+)=(\S+)', l)); f = int(d['frame'])
    a = agg[seg(f)]; a[0] += 1; a[1] += int(d['admitted']); a[2] += int(d['refused']); a[3] += int(d['admitted']) > 0
    a[4].add(d['pairs'])
for s in sorted(agg):
    a = agg[s]
    print(f"seg frames {cuts[s]}-{cuts[s+1]-1}: rows={a[0]} admitted={a[1]} refused={a[2]} frames_with_admit={a[3]} pair_masks={sorted(a[4])}")
