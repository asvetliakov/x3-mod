"""Offline sun-shadow visibility for the pixels of chosen draws in one F8 frame.
Inputs (all from the capture dir): depth_<dev>_<frame>.rgba32f (RT2: .r device z, .g sun share s, .b view w),
shadow_map<k>_<dev>_<frame>.r32f, and from the log the frame's sun_shadow_apply_params (m00, m11, rows<k>,
bias<k>, extent<k>, cascades) and object_bounds rows for the draw indices.
Per draw: pixels inside its screen box whose device z lies in [zmin, zmax]; view pos (x*w/m00, y*w/m11, w);
sun ndc/depth = rows<k> . (v, 1); texel = ((x+1)/2, (1-y)/2) * size; lit when depth <= map + bias<k>
(nearest texel, no PCF, no slope bias: a coarse approximation of the apply quad, for comparison between draws).
Cascade = the smallest k whose |ndc| < 1 (approximation of the selection).
Also prints HDR (hdr_<dev>_<frame>.rgba16f, post-apply scene colour) luminance percentiles of the same pixels.
usage: shadow_visibility.py CAPDIR LOG FRAME INDEX..."""
import sys, re
import numpy as np
cap, log, frame, idx = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4:]
kv = re.compile(r'(\w+)=(\S+)'); P = None; B = {}
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('sun_shadow_apply_params ') and f' frame={frame} ' in line: P = dict(kv.findall(line))
        elif line.startswith('object_bounds ') and f' frame={frame} ' in line:
            d = dict(kv.findall(line))
            if d['index'] in idx: B[d['index']] = d
W, H = int(P['width']), int(P['height'])
rt = np.fromfile(f'{cap}/depth_1_{frame}.rgba32f', dtype='<f4').reshape(H, W, 4)
hdr = np.fromfile(f'{cap}/hdr_1_{frame}.rgba16f', dtype='<f2').reshape(H, W, 4).astype(np.float32)
m00, m11 = float(P['m00']), float(P['m11'])
K = int(P['cascades'])
rows = [np.array([float(x) for x in P[f'rows{k}'].split(',')]).reshape(3, 4) for k in range(K)]
maps = {}
def smap(k):
    if k not in maps:
        n = int(P[f'map{k}']); maps[k] = np.fromfile(f'{cap}/shadow_map{k}_1_{frame}.r32f', dtype='<f4').reshape(n, n)
    return maps[k]
for i in idx:
    b = B.get(i)
    if not b: print('draw', i, 'no bounds'); continue
    x0, y0, x1, y1 = (int(float(b[k])) for k in ('sx0', 'sy0', 'sx1', 'sy1'))
    zmin, zmax = float(b['zmin']), float(b['zmax'])
    sub = rt[max(y0, 0):min(y1 + 1, H), max(x0, 0):min(x1 + 1, W)]
    ys, xs = np.mgrid[max(y0, 0):min(y1 + 1, H), max(x0, 0):min(x1 + 1, W)]
    sel = (sub[..., 0] >= zmin - 2e-6) & (sub[..., 0] <= zmax + 2e-6) & (sub[..., 1] > 0)
    if not sel.any(): print('draw', i, b['model'], 'no pixels'); continue
    w = sub[..., 2][sel]; s = sub[..., 1][sel]
    hs = hdr[max(y0, 0):min(y1 + 1, H), max(x0, 0):min(x1 + 1, W)][sel]
    lum = 0.2126 * hs[:, 0] + 0.7152 * hs[:, 1] + 0.0722 * hs[:, 2]
    nx = (xs[sel] + 0.5) / W * 2 - 1; ny = 1 - (ys[sel] + 0.5) / H * 2
    v = np.stack([nx * w / m00, ny * w / m11, w, np.ones_like(w)])
    res = []
    for k in range(K):
        with np.errstate(all='ignore'): q = rows[k] @ v
        inside = (np.abs(q[0]) < 1) & (np.abs(q[1]) < 1)
        res.append((k, q, inside))
    k = next((k for k, q, ins in res if ins.all()), None)
    if k is None: print('draw', i, 'not inside one cascade'); continue
    q = res[k][1]; m = smap(k); n = m.shape[0]
    u = np.clip(((q[0] + 1) / 2 * n).astype(int), 0, n - 1); t = np.clip(((1 - q[1]) / 2 * n).astype(int), 0, n - 1)
    md = m[t, u]; bias = float(P[f'bias{k}'])
    lit = q[2] <= md + bias
    empty = md >= 1.0
    dz = (q[2] - md) * (float(P[f'depth_light{k}']) + float(P[f'depth_behind{k}']))
    print(f"draw {i} model={b['model']} px={int(sel.sum())} s_mean={s.mean():.3f} cascade={k} lit={lit.mean():.3f} "
          f"empty_texels={empty.mean():.3f} texels={len(set(zip(u.tolist(), t.tolist())))} "
          f"lum p50/p90/p99={np.percentile(lum, 50):.3f}/{np.percentile(lum, 90):.3f}/{np.percentile(lum, 99):.3f} "
          f"sun=lum*s p50/p90/p99={np.percentile(lum * s, 50):.3f}/{np.percentile(lum * s, 90):.3f}/{np.percentile(lum * s, 99):.3f} rest=lum*(1-s) p50/p90={np.percentile(lum * (1 - s), 50):.3f}/{np.percentile(lum * (1 - s), 90):.3f} "
          f"dz_units p10/50/90={np.percentile(dz, 10):.0f}/{np.percentile(dz, 50):.0f}/{np.percentile(dz, 90):.0f} bias_m={bias * (float(P[f'depth_light{k}']) + float(P[f'depth_behind{k}'])):.0f}")
