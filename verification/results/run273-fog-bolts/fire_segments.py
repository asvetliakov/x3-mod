#!/usr/bin/env python3
"""Run 73 B: per session segment, the player's fire (chase_fire_window native_inactive_override,
refused_view), bullet-PS creations (screen_emission_additive_variant rows), bolt_footprint draws and the
additive admissions; answers whether the post-new-game additive drop had any bullet draw to admit.
Usage: fire_segments.py SESSION_LOG  (segment cuts as in additive_segments.py)."""
import sys, re
from collections import defaultdict
cuts = [0, 429, 11113, 12160, 15872, 16258, 19555, 24765, 31511, 33817, 35513, 10**9]
seg = lambda f: max(i for i, c in enumerate(cuts) if f >= c)
agg = defaultdict(lambda: defaultdict(int))
last_frame = 0
for l in open(sys.argv[1], errors='replace'):
    m = re.search(r' frame=(\d+)', l)
    if m: last_frame = int(m.group(1))
    if l.startswith('chase_fire_window '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); s = agg[seg(int(d['frame']))]
        s['fire_override'] += int(d['native_inactive_override']); s['fire_refused_view'] += int(d['refused_view'])
    elif l.startswith('screen_emission_additive_variant '):
        agg[seg(last_frame)]['bullet_ps_created'] += 1
    elif l.startswith('bolt_footprint device='):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); agg[seg(int(d['frame']))]['footprint_draws'] += int(d['draws'])
    elif l.startswith('screen_emission_additive_frame '):
        d = dict(re.findall(r'(\w+)=(\S+)', l)); s = agg[seg(int(d['frame']))]
        s['admitted'] += int(d['admitted']); s['refused'] += int(d['refused'])
for i in sorted(agg):
    print(f"seg frames {cuts[i]}-{cuts[i+1]-1}: " + ' '.join(f'{k}={v}' for k, v in sorted(agg[i].items())))
