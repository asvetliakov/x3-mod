#!/usr/bin/env python3
"""Project capture pixels into the FL 71 A shadow cascades.
usage: project.py FRAME x0 y0 x1 y1 [step]
Reads camera_state and shadow_replay_map_basis rows for FRAME from /mnt/c/X3FL/x3m.log
(grep, no whole read), the depth capture (.z = view z, checked against .x = p22 + p32/z),
reconstructs world positions, picks the first containing cascade (apply shader law:
max(|x|,|y|) <= 1, z in [0,1]) and compares receiver z to the stored map depth
(nearest texel, no bias, no PCF). Prints per-cascade counts and the covering texel box."""
import sys, re, subprocess, numpy as np
LOG = '/mnt/c/X3FL/x3m.log'; CAP = '/mnt/c/X3FL/x3-modern-captures/'
W, H = 5120, 1440

def rows(frame):
    out = subprocess.run(['grep', '-E', f'^(camera_state|shadow_replay_map_basis) .*frame={frame} ', LOG],
                         capture_output=True, text=True).stdout.splitlines()
    cam = None; cas = {}
    for l in out:
        kv = dict(re.findall(r'(\w+)=(\S+)', l))
        if l.startswith('camera_state'):
            cam = kv
        else:
            cas[int(kv['cascade'])] = kv
    return cam, cas

def vec(s): return np.array([float(v) for v in s.split(',')])

def main():
    f = sys.argv[1]; x0, y0, x1, y1 = map(int, sys.argv[2:6]); st = int(sys.argv[6]) if len(sys.argv) > 6 else 4
    cam, cas = rows(f)
    R = np.array([[float(cam[f'r{i}{j}']) for j in range(3)] for i in range(3)])
    t = vec(cam['t']); p00 = float(cam['p00']); p11 = float(cam['p11'])
    camw = -t @ R.T
    d = np.memmap(CAP + f'depth_1_{f}.rgba32f', dtype='<f4', mode='r', shape=(H, W, 4))
    ys, xs = np.mgrid[y0:y1:st, x0:x1:st]
    z = np.array(d[y0:y1:st, x0:x1:st, 2], dtype=np.float64)
    ok = z > 0
    nx = (xs + .5) / W * 2 - 1; ny = 1 - (ys + .5) / H * 2
    v = np.stack([nx * z / p00, ny * z / p11, z], -1)
    pw = (v - t) @ R.T
    print('frame', f, 'camera_world', np.round(camw, 2), 'pixels', int(ok.sum()), 'view_z median', float(np.median(z[ok])) if ok.any() else None)
    chosen = np.full(z.shape, -1)
    for c in sorted(cas):
        k = cas[c]
        if k.get('valid') != '1': continue
        r, u, fw, ce = vec(k['right']), vec(k['up']), vec(k['forward']), vec(k['center'])
        E = float(k['extent']); L = float(k['depth_light']); B = float(k['depth_behind']); N = int(k['size'])
        q = pw - ce
        sx = q @ r / E; sy = q @ u / E; sz = (q @ fw + L) / (L + B)
        inside = ok & (np.maximum(abs(sx), abs(sy)) <= 1) & (sz >= 0) & (sz <= 1)
        first = inside & (chosen < 0); chosen[first] = c
        if not inside.any():
            print(f'c{c} inside=0'); continue
        col = ((sx * .5 + .5) * N).astype(int).clip(0, N - 1); row = ((-sy * .5 + .5) * N).astype(int).clip(0, N - 1)
        m = np.memmap(CAP + f'shadow_map{c}_1_{f}.r32f', dtype='<f4', mode='r', shape=(N, N))
        md = np.array(m)[row[inside], col[inside]]
        occl = md < sz[inside] - 1e-6
        print(f'c{c} texel_u={2*E/N:.4g} inside={int(inside.sum())} first={int(first.sum())} '
              f'occluded_nearest={int(occl.sum())} ({occl.mean():.3f}) cols={col[inside].min()}..{col[inside].max()} '
              f'rows={row[inside].min()}..{row[inside].max()} recv_z_med={np.median(sz[inside]):.6f} map_z_med_occl={np.median(md[occl]) if occl.any() else -1:.6f}')

if __name__ == '__main__':
    main()
