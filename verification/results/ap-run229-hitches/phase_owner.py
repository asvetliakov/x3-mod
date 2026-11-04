#!/usr/bin/env python3
"""Dominant phase of every frame_phases_slow frame with 16 ms <= dt < 500 ms (AP perf log).
Usage: phase_owner.py LOG"""
import re, sys, collections
kv = lambda s: dict((k, int(v)) for k, v in re.findall(r'(\w+)=(-?\d+)\b', s))
sect = [0, 1371, 11886, 18806, 24901, 30317, 34969]  # volumetric_fog_sector / loading_phase frames in this log
el = {}
rows = []
for ln in open(sys.argv[1], errors='replace'):
    if ln.startswith('frame_end '):
        d = kv(ln); el[d['frame']] = d['elapsed_ms']
    elif ln.startswith('frame_phases_slow '):
        rows.append(kv(ln))
by = collections.defaultdict(list)
for d in rows:
    if not (16000 <= d['dt_us'] < 500000): continue
    f = d['frame']; s = max(x for x in sect if x <= f)
    parts = {'pre_render': d['pre_render_us'], 'view_setup': d['view_setup_us'], 'view_submit': d['view_submit_us'],
             'scene_end': d['scene_end_us'], 'present': d['present_us'],
             'other': d['dt_us'] - d['pre_render_us'] - d['views_us'] - d['scene_end_us'] - d['present_us']}
    own = max(parts, key=parts.get)
    cls = 'sector_entry' if f - s <= 3 else own
    by[cls].append((f, d['dt_us'] // 1000, parts[own] // 1000, round((el.get(f, 0) - el.get(s, 0)) / 1000, 1)))
for k, v in sorted(by.items()):
    print(k, len(v), 'dt_ms=', sorted(x[1] for x in v))
    print('   frame/dt/owner_ms/s_since_sector:', v)
