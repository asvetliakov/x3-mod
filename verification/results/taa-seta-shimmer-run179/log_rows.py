#!/usr/bin/env python3
"""Per captured frame: camera rotation and translation step, dt, jitter, k, SETA state, far-fade row.
Usage: log_rows.py <log> [first_frame ...] (default: the Run179 first-flight bursts)   (greps only the needed tags; never loads the log whole)"""
import re, subprocess, sys, math
LOG = sys.argv[1]
BURSTS = [(int(a), int(a) + 15) for a in sys.argv[2:]] or [(4707, 4722), (7631, 7646), (11391, 11406), (13089, 13104)]
frames = set()
for a, b in BURSTS:
    frames.update(range(a - 1, b + 1))
pat = r'^(camera_state|frame_end|motion_output_frame|hull_lightmap_far_fade_frame|engine_seta) device=1 frame=(' + '|'.join(map(str, sorted(frames))) + ') '
out = subprocess.run(['grep', '-E', pat, LOG], capture_output=True, text=True, env={'LC_ALL': 'C'}).stdout
rows = {}
for line in out.splitlines():
    tag = line.split(' ', 1)[0]
    kv = dict(re.findall(r'(\w+)=(\S+)', line))
    rows.setdefault(int(kv['frame']), {})[tag] = kv
seta = subprocess.run(['grep', '-E', '^engine_seta device=1', LOG], capture_output=True, text=True, env={'LC_ALL': 'C'}).stdout.splitlines()
events = [(int(re.search(r'frame=(\d+)', l).group(1)), re.search(r'state=(\w+)', l).group(1), re.search(r'warp=(\S+)', l).group(1)) for l in seta]
def seta_at(f):
    s = ('off', '1.000')
    for fr, st, w in events:
        if fr <= f: s = (st, w)
    return s
print('engine_seta events:', events)
print('frame dt_ms rot_deg cam_step_u cam_speed_u/s jit_x jit_y k far_fade(admit/faded/min_gain) seta')
for a, b in BURSTS:
    print(f'== burst {a}-{b}')
    prev = None
    for f in range(a - 1, b + 1):
        r = rows.get(f, {})
        cs, fe, mo, ff = r.get('camera_state', {}), r.get('frame_end', {}), r.get('motion_output_frame', {}), r.get('hull_lightmap_far_fade_frame', {})
        t = tuple(map(float, cs['t'].split(','))) if 't' in cs else None
        step = math.dist(t, prev) if (t and prev) else float('nan')
        dt = float(fe.get('dt_ms', 'nan'))
        prev = t
        if f < a: continue
        print(f, fe.get('dt_ms'), cs.get('rotation_deg'), f'{step:.1f}', f'{step / dt * 1000 if dt > 0 else float("nan"):.1f}',
              mo.get('jitter_x'), mo.get('jitter_y'), mo.get('taa_k'), f"{ff.get('admitted')}/{ff.get('faded')}/{ff.get('min_gain')}", seta_at(f))
