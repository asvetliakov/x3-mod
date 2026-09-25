#!/usr/bin/env python3
"""Run309: projection p00/p11/background_p00 over time (camera_state rows), runs of equal value, plus fov/fov_confirm/cull rows."""
import re, sys, math
L = sys.argv[1] if len(sys.argv) > 1 else '/tmp/x3-bottleX3-run309/session-20260924-222017-212.log'
kv = re.compile(r'(\w+)=(\S+)')
runs = []  # (first_frame,last_frame,p00,p11,bp00,count)
cur = None
H = 1440.0; W = 5120.0
def F_of(p11, p00):
    c = max(0.75 * p11 * 1.0, 0)  # cot(F/2) = 0.75*m11 ... when m11 = cot/H with H=0.75
    return c
with open(L, 'rb') as f:
    for raw in f:
        if raw.startswith(b'camera_state device='):
            d = dict(kv.findall(raw.decode('latin1')))
            if d.get('valid') != '1':
                continue
            fr = int(d['frame']); p00 = round(float(d['p00']), 4); p11 = round(float(d['p11']), 4)
            bp = round(float(d.get('background_p00', '0')), 4)
            key = (p00, p11, bp)
            if cur and tuple(cur[2:5]) == key:
                cur[1] = fr; cur[5] += 1
            else:
                if cur: runs.append(cur)
                cur = [fr, fr, p00, p11, bp, 1]
        elif raw.startswith((b'fov ', b'fov_confirm', b'cull_small_parts_value', b'fov_restore')):
            print('ROW', raw.decode('latin1').strip()[:300])
if cur: runs.append(cur)
print('camera_state valid runs (first..last frame, p00, p11, background_p00, rows, F_deg=2*atan(1/(0.75*p11)), vertical_deg=2*atan(1/p11))')
for r in runs:
    p11 = r[3]
    Fdeg = 2*math.degrees(math.atan(1/(0.75*p11))) if p11 > 0 else float('nan')
    v = 2*math.degrees(math.atan(1/p11)) if p11 > 0 else float('nan')
    print(f'{r[0]}..{r[1]} p00={r[2]} p11={r[3]} bg_p00={r[4]} rows={r[5]} F_deg={Fdeg:.2f} F=0x{round(Fdeg/360*65536):04x} vert={v:.2f}')
print('runs', len(runs))
