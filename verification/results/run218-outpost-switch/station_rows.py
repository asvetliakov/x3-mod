#!/usr/bin/env python3
"""Run 218: per-frame station draw rows (node 3d8c0290, model 5c09) plus widen rows and frame counters.
Usage: LC_ALL=C grep -E 'frame=(3571|4148|15021|16134)( |$)' /mnt/c/X3/x3m.log > frames.txt; station_rows.py frames.txt"""
import sys
R = [l.split() for l in open(sys.argv[1])]
d = lambda r: dict(t.split('=', 1) for t in r[1:] if '=' in t)
idx = {}
for r in R:
    if r[0] == 'motion_route' and 'model=00005c09' in r:
        x = d(r); idx.setdefault(x['frame'], set()).add(x['index'])
        print('route', *(f"{k}={x[k]}" for k in ('frame', 'index', 'vs', 'ps', 'routed', 'gate', 'zwrite', 'blend', 'src', 'dst', 'atest', 'fade_arm', 'fade_permille', 'unmatched', 'primitives')))
for r in R:
    x = d(r)
    if r[0] == 'hull_lightmap_widen_draw' and x['index'] in idx.get(x['frame'], ()): print('widen', x['frame'], x['index'], x['ps'], x['size'])
    if r[0] in ('fade_route_frame', 'sun_shadow_lane_frame', 'hull_lightmap_frame', 'hull_lightmap_far_fade_frame', 'engine_light_frame'):
        keys = {'fade_route_frame': 'fade_routed fade_fill fade_gain fade_widen fade_owner fade_owner_masked',
                'sun_shadow_lane_frame': 'receiver_draws non_depth_writers original_variants',
                'hull_lightmap_frame': 'admitted gain', 'hull_lightmap_far_fade_frame': 'admitted faded min_gain',
                'engine_light_frame': 'draws_lit nodes'}[r[0]].split()
        print(r[0], x['frame'], *(f'{k}={x.get(k)}' for k in keys))
