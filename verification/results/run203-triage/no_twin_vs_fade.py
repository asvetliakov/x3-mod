#!/usr/bin/env python3
"""Run 203 A: do the lod_strength_frame no_twin draws coincide with fade-owner draws on the sun lane?
Streams the log once and keeps only lod_strength_frame / fade_route_frame / sun_shadow_lane_frame rows per frame.
Usage: no_twin_vs_fade.py /mnt/c/X3/x3m.log"""
import collections
import sys

KEEP = ('lod_strength_frame ', 'fade_route_frame ', 'sun_shadow_lane_frame ')
frames = {}
with open(sys.argv[1], errors='replace') as log:
    for line in log:
        if not line.startswith(KEEP):
            continue
        kind, *fields = line.split()
        row = dict(f.split('=', 1) for f in fields if '=' in f)
        frames.setdefault(int(row['frame']), {})[kind] = row


def key(v):
    fade, lane = v.get('fade_route_frame', {}), v.get('sun_shadow_lane_frame', {})
    return (int(fade.get('fade_routed', -1)) > 0, fade.get('fade_owner'), lane.get('available'))


strength = {f: v for f, v in frames.items() if 'lod_strength_frame' in v}
no_twin = [f for f, v in strength.items() if int(v['lod_strength_frame']['no_twin']) > 0]
print('no_twin frames', len(no_twin), 'draws', sum(int(strength[f]['lod_strength_frame']['no_twin']) for f in no_twin))
print('no_twin frames by (fade_routed>0, fade_owner, sun lane available):', dict(collections.Counter(key(strength[f]) for f in no_twin)))
print('marked frames without no_twin, same key:', dict(collections.Counter(
    key(v) for f, v in strength.items() if int(v['lod_strength_frame']['marked']) > 0 and int(v['lod_strength_frame']['no_twin']) == 0)))
print('no_twin <= fade_routed on', sum(1 for f in no_twin if int(strength[f]['lod_strength_frame']['no_twin']) <=
                                       int(strength[f].get('fade_route_frame', {}).get('fade_routed', 0))), 'of', len(no_twin))
