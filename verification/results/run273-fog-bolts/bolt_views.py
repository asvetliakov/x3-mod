#!/usr/bin/env python3
"""Run 73 B, task 2: bolt_footprint 300-frame windows split by view. The view of a window comes from the
chase_camera rows at its two ends: applied frames in the window (delta of `applied`) and the last verdict
(0 applied, 1 internal view = first person). Prints per window and the totals per view: instances planned
and expanded under the Run73 3,8 rule.
Usage: bolt_views.py SESSION_LOG"""
import sys, re
chase = {}  # frame -> (applied, refused, verdict)
windows = []
for l in open(sys.argv[1], errors='replace'):
    if l.startswith('chase_camera '):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        if 'applied' in d and 'frame' in d:
            chase[int(d['frame'])] = (int(d['applied']), int(d['refused']), int(d.get('verdict', -1)))
    elif l.startswith('bolt_footprint device='):
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        windows.append((int(d['frame']), int(d['draws']), int(d['instances']), int(d['expanded'])))
frames = sorted(chase)
def at(f):
    k = max((x for x in frames if x <= f), default=None)
    return chase[k] if k is not None else (0, 0, -1)
totals = {}
for f, draws, inst, exp in windows:
    if not inst:
        continue
    a0, r0, _ = at(f - 299); a1, r1, v1 = at(f + 1)
    applied, refused = a1 - a0, r1 - r0
    view = 'chase' if refused == 0 and applied > 0 else 'first_person' if applied == 0 and v1 == 1 else 'mixed'
    t = totals.setdefault(view, [0, 0, 0])
    t[0] += 1; t[1] += inst; t[2] += exp
    print(f'window_end {f} view {view} chase_applied {applied} chase_refused {refused} last_verdict {v1} draws {draws} instances {inst} expanded {exp} share {exp / inst:.2f}')
for view, (n, inst, exp) in sorted(totals.items()):
    print(f'total {view}: windows {n} instances {inst} expanded {exp} share {exp / inst:.3f}')
