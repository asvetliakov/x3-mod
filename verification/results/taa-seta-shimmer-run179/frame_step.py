#!/usr/bin/env python3
"""Per captured frame: screen displacement (px/frame, jitter included as the resolve does) of routed hull pixels by depth
group, and the rotation-only camera path's displacement at screen centre. The first frame of a burst follows an
ordinary (uncaptured) frame, so its displacement is the in-flight frame step; later frames follow ~400-530 ms capture
frames. Usage: frame_step.py <capdir> <log> <first_frame> [node]  (node: also the object's pixels from its object_bounds rows)"""
import sys, re, subprocess, numpy as np
W, H = 5120, 1440
cap, log, f0 = sys.argv[1], sys.argv[2], int(sys.argv[3])
NODE = sys.argv[4] if len(sys.argv) > 4 else None
def row(tag, f):
    l = subprocess.run(['grep', '-m1', '-E', f'^{tag} device=1 frame={f} ', log], capture_output=True, text=True).stdout
    return dict(re.findall(r'(\w+)=(\S+)', l))
def R(f):
    kv = row('camera_state', f)
    return np.array([[float(kv[f'r{i}{j}']) for j in range(3)] for i in range(3)])
groups = [('own ship z<300', 0, 300), ('300..25.6k', 300, 25600), ('25.6k..76.8k', 25600, 76800), ('far band >76.8k', 76800, 1e12)]
print('frame dt_ms rot_path_px(centre) | per group: px count, |d| p50/p90')
for f in range(f0, f0 + 16):
    j = row('motion_output_frame', f); dt = row('frame_end', f).get('dt_ms')
    jx, jy = float(j['jitter_x']), float(j['jitter_y'])
    M = R(f).T @ R(f - 1)
    vp = np.array([0, 0, 1.0]) @ M; p00 = float(row('camera_state', f)['p00'])
    rot = np.hypot(vp[0] / vp[2] * p00 * 0.5 * W, vp[1] / vp[2] * 1.777763 * 0.5 * H)
    d = np.memmap(f'{cap}/depth_1_{f}.rgba32f', np.float32, 'r', shape=(H, W, 4))[::2, ::2]
    m = np.memmap(f'{cap}/motion_1_{f}.rgba32f', np.float32, 'r', shape=(H, W, 4))[::2, ::2]
    ys, xs = np.mgrid[0:H:2, 0:W:2]
    ok = (m[..., 3] == 1) & (d[..., 0] >= 0) & (d[..., 0] < 1) & (d[..., 2] > 0)
    s = np.hypot(m[..., 0] * W - (xs + 0.5) + jx, m[..., 1] * H - (ys + 0.5) + jy)
    z = d[..., 2]
    out = []
    for nm, lo, hi in groups:
        g = ok & (z >= lo) & (z < hi)
        out.append(f'{g.sum()} {np.median(s[g]) if g.any() else float("nan"):.2f}/{np.percentile(s[g],90) if g.any() else float("nan"):.2f}')
    if NODE:
        g = np.zeros_like(ok)
        for l in subprocess.run(['grep', '-E', f'^object_bounds device=1 frame={f} .*node={NODE} ', log], capture_output=True, text=True).stdout.splitlines():
            kv = dict(re.findall(r'(\w+)=(\S+)', l))
            g |= (xs + .5 >= float(kv['sx0'])) & (xs + .5 <= float(kv['sx1'])) & (ys + .5 >= float(kv['sy0'])) & (ys + .5 <= float(kv['sy1'])) \
                 & (d[..., 0] >= float(kv['zmin']) - 2e-6) & (d[..., 0] <= float(kv['zmax']) + 2e-6)
        g &= ok
        out.append(f'node {g.sum()} {np.median(s[g]) if g.any() else float("nan"):.2f}/{np.percentile(s[g],90) if g.any() else float("nan"):.2f} z {np.median(z[g]) if g.any() else float("nan"):.0f}')
    print(f, dt, f'{rot:.2f}', '|', ' | '.join(out))
