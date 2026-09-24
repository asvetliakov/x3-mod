#!/usr/bin/env python3
"""The fade-owner prepass parity cases (seam-taa-fade-route-zonly-owner / -zonly-unjit-owner) from the pinned motion-output
summary: per case the RT2 and colour interior holes per frame (392 interior pixels per frame), the frames with jx > 0, the
DLL's unjittered_depth_writers and jittered counts per frame, and the worst RT2 z/w error.
usage: zonly_holes.py   (reads verification/results/bottle-X3/motion-output-summary.json)"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
summary = json.loads((ROOT / 'verification/results/bottle-X3/motion-output-summary.json').read_text())
print('passed', summary['passed'], 'cases', len(summary['cases']))
for name in ('seam-taa-fade-route-zonly-owner', 'seam-taa-fade-route-zonly-unjit-owner'):
    c = summary['cases'][name]
    print(name, 'checks', c['checks'], 'pixels_per_frame', c['pixels_per_frame'])
    print('  rt2_holes', c['rt2_holes'])
    print('  color_holes', c['color_holes'], 'equal', c['color_holes'] == c['rt2_holes'], 'mismatch', c['mismatch'])
    print('  hole_frames', c['hole_frames'], 'positive_jx_frames', c['positive_jx_frames'])
    print('  unjittered_depth_writers', sorted(set(c['unjittered_depth_writers'].values())), 'jittered', sorted(set(c['jittered'].values())))
    print('  max_depth_error', c['max_depth_error'])
