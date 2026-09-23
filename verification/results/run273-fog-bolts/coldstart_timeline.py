#!/usr/bin/env python3
"""Per-frame timeline for each cold start window: frame_end elapsed/dt, cache_frame ready/upload, fog frame reason, card state.
Usage: coldstart_timeline.py session.log a-b [a-b ...]"""
import sys, re
wins = [tuple(map(int, w.split('-'))) for w in sys.argv[2:]]
inw = lambda f: any(a <= f <= b for a, b in wins)
rows = {}
kv = re.compile(r'(\w+)=(\S+)')
for l in open(sys.argv[1], errors='replace'):
    t = l.split(' ', 1)[0]
    if t not in ('frame_end', 'volumetric_fog_cache_frame', 'volumetric_fog_frame', 'volumetric_fog_cards'): continue
    m = re.search(r' frame=(\d+) ', l)
    if not m or not inw(int(m.group(1))): continue
    d = dict(kv.findall(l)); r = rows.setdefault(int(m.group(1)), {})
    if t == 'frame_end': r.update(ms=d['elapsed_ms'], dt=d['dt_ms'])
    elif t == 'volumetric_fog_cache_frame': r.update(far=d['ready_far'], fine=d['ready_fine'], up=d['upload_bytes'], prep=d['prepared'])
    elif t == 'volumetric_fog_frame': r.update(fog=d['reason'])
    else: r.update(sup=d['suppressed'], warm=d['warmup'], ref=d['refused'], mode=d['mode'])
for f in sorted(rows):
    r = rows[f]; print(f, *(f"{k}={r.get(k)}" for k in ('ms', 'dt', 'prep', 'far', 'fine', 'up', 'fog', 'sup', 'warm', 'ref', 'mode')))
