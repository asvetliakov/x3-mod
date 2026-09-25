#!/usr/bin/env python3
"""Run309: the small-parts cull's FOV factor per camera_state change, as logged and as the scene-projection fix gives it.

For every run of equal scene projection (camera_state valid=1 p00/p11, the motion route's scene-Clear read, the
source the fixed cull takes), prints F = focus_from_projection(p00, p11), the factor F/0x4000 and the thresholds
(cull_small_parts_core.h threshold_for) at px 2/4/8 with the width of the cull_small_parts_value row, next to what
the run applied (the value row's m00 and focus: the HUD view's 0.375 at 0x4000 on every frame). Streams the log;
prints only the summary. Usage: python3 cull_factor_by_fov.py [log]
"""
import math
import re
import sys

LOG = sys.argv[1] if len(sys.argv) > 1 else '/tmp/x3-bottleX3-run309/session-20260924-222017-212.log'
KV = re.compile(r'(\w+)=(\S+)')
FOCUS_DEFAULT, FOCUS_MIN, FOCUS_MAX = 0x4000, 0x106, 0x8000


def focus_from_projection(m00, m11):
    if not (m00 > 0 and m11 > 0):
        return None
    exact = 65536 / math.pi * math.atan(1 / max(0.75 * m11, m00))
    if abs(exact - FOCUS_DEFAULT) <= 2.0:   # focus_snap (cull_small_parts_core.h)
        return FOCUS_DEFAULT
    focus = math.floor(exact + 0.5)
    return focus if FOCUS_MIN <= focus <= FOCUS_MAX else None


def threshold_for(px, m00, width, focus=FOCUS_DEFAULT):
    per_s = m00 * width / 1280.0 * (focus / FOCUS_DEFAULT)
    t = math.ceil(px / per_s)
    while t > 1 and (t - 1) * per_s >= px:
        t -= 1
    while t * per_s < px:
        t += 1
    return t


runs, cur, applied, frames_rows = [], None, None, 0
with open(LOG, 'rb') as f:
    for raw in f:
        if raw.startswith(b'camera_state device='):
            d = dict(KV.findall(raw.decode('latin1')))
            if d.get('valid') != '1':
                continue
            frame, p00, p11 = int(d['frame']), float(d['p00']), float(d['p11'])
            focus = focus_from_projection(p00, p11)
            if cur and cur['focus'] == focus and abs(cur['p00'] - p00) < 1e-5:
                cur['last'] = frame
                cur['rows'] += 1
            else:
                cur = {'first': frame, 'last': frame, 'p00': p00, 'p11': p11, 'focus': focus, 'rows': 1}
                runs.append(cur)
        elif raw.startswith(b'cull_small_parts_value ') and applied is None:
            d = dict(KV.findall(raw.decode('latin1')))
            applied = {'px': float(d['px']), 'm00': float(d['m00']), 'width': int(d['width']), 'threshold': int(d['threshold']),
                       'focus': int(d.get('focus', '0x4000'), 16)}
        elif raw.startswith(b'cull_small_parts_frame '):
            frames_rows += 1

width = applied['width']
print(f"applied (cull_small_parts_value): px={applied['px']:g} m00={applied['m00']:.6f} focus=0x{applied['focus']:04x} width={width} "
      f"threshold={applied['threshold']}; cull_small_parts_frame rows={frames_rows}")
print('scene projection runs: first..last frame, rows, p00, F, factor F/0x4000, thresholds px 2/4/8 corrected (scene P[0] and F) | as applied (HUD P[0], 0x4000)')
wrong = tuple(threshold_for(px, applied['m00'], width, applied['focus']) for px in (2, 4, 8))
for r in runs:
    if r['focus'] is None:
        print(f"{r['first']}..{r['last']} rows={r['rows']} p00={r['p00']:.4f} F=unusable")
        continue
    fixed = tuple(threshold_for(px, r['p00'], width, r['focus']) for px in (2, 4, 8))
    mark = '' if fixed == wrong else '  <- differs'
    print(f"{r['first']}..{r['last']} rows={r['rows']} p00={r['p00']:.4f} F=0x{r['focus']:04x} factor={r['focus'] / FOCUS_DEFAULT:.4f} "
          f"thresholds={fixed[0]}/{fixed[1]}/{fixed[2]} | applied {wrong[0]}/{wrong[1]}/{wrong[2]}{mark}")
print('runs', len(runs))
