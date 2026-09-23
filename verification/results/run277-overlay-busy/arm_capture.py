#!/usr/bin/env python3
"""Run 74 A (run277): rasterise the body's original LOD 0 faces (per-material label + view z,
no alpha) with the draw's logged world/view/projection, then compare against the captured
depth (channel 2 = view z) and HDR colour: per material (front-most in the raster) pixels,
'hit' (captured z within TOL of the raster z: surface drawn), 'hole' (captured farther: alpha
cut / missing geometry), 'occl' (captured nearer), mean HDR luminance and rgb over hit pixels.
Optional zone: faces with sqrt(y^2+z^2) > R and |x| < XB (the solar-panel arm zone) get
label 100+mat. Usage: arm_capture.py LOG DIR BODY FRAME:DRAWLINE [...]"""
import sys, re, math, collections
import numpy as np
sys.path.insert(0, '/Users/asvetl/x3-mod/tools/analysis')
import bob1, lod_overlay
log, D, name = sys.argv[1], sys.argv[2], sys.argv[3]
jobs = [tuple(int(v) for v in a.split(':')) for a in sys.argv[4:]]
R, XB, TOL = 10000, 4000, 0.02
W_, H_ = 1920, 1080
oa, _ = lod_overlay.original_assets(bob1.DEFAULT_GAME)
t = bob1.parse(oa.read_entry(bob1.resolve_body(oa, name)))
alpha = lod_overlay.alpha_materials(bob1.materials(t))
L0 = bob1.lods(t)[0]
pts = np.array([p[1:4] if p[0] & 1 else (0, 0, 0) for p in L0['points']], dtype=np.float64) / 65536.0
faces, lab = [], []
for part in L0['parts']:
    for g in part['groups']:
        for f in g['faces']:
            c = pts[list(f[:3])].mean(0) * 65536
            zone = math.hypot(c[1], c[2]) > R and abs(c[0]) < XB
            faces.append(f[:3]); lab.append(g['material'] + (100 if zone else 0))
faces = np.array(faces); lab = np.array(lab)
def f32(h): return np.frombuffer(bytes.fromhex(h), '>f4')[0] if False else np.frombuffer(int(h, 16).to_bytes(4, 'little'), '<f4')[0]
def matrices(lineno):
    M = collections.defaultdict(lambda: np.zeros((4, 4)))
    with open(log, errors='replace') as fh:
        for i, line in enumerate(fh, 1):
            if i < lineno: continue
            if i > lineno + 400: break
            m = re.match(r'object_matrix role=(\w+) row=(\d) bits=(\S+)', line)
            if m: M[m.group(1)][int(m.group(2))] = [f32(b) for b in m.group(3).split(',')]
    return M
for frame, lineno in jobs:
    M = matrices(lineno)
    hom = np.c_[pts, np.ones(len(pts))] @ M['world'] @ M['view']
    vz = hom[:, 2]
    clip = hom @ M['projection']
    sx = (clip[:, 0] / clip[:, 3] + 1) * 0.5 * W_; sy = (1 - clip[:, 1] / clip[:, 3]) * 0.5 * H_
    zbuf = np.full((H_, W_), np.inf); lbuf = np.full((H_, W_), -1, np.int32)
    for fi, (a, b, c) in enumerate(faces):
        xs = sx[[a, b, c]]; ys = sy[[a, b, c]]; zs = vz[[a, b, c]]
        x0, x1 = max(int(np.floor(xs.min())), 0), min(int(np.ceil(xs.max())), W_ - 1)
        y0, y1 = max(int(np.floor(ys.min())), 0), min(int(np.ceil(ys.max())), H_ - 1)
        if x1 < x0 or y1 < y0: continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + .5, np.arange(y0, y1 + 1) + .5)
        d = (xs[1] - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (ys[1] - ys[0])
        if abs(d) < 1e-12: continue
        w1 = ((gx - xs[0]) * (ys[2] - ys[0]) - (xs[2] - xs[0]) * (gy - ys[0])) / d
        w2 = ((xs[1] - xs[0]) * (gy - ys[0]) - (gx - xs[0]) * (ys[1] - ys[0])) / d
        w0 = 1 - w1 - w2
        ins = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not ins.any(): continue
        z = w0 * zs[0] + w1 * zs[1] + w2 * zs[2]
        sub = zbuf[y0:y1 + 1, x0:x1 + 1]; sl = lbuf[y0:y1 + 1, x0:x1 + 1]
        upd = ins & (z < sub)
        sub[upd] = z[upd]; sl[upd] = lab[fi]
    dep = np.fromfile(D + f'depth_1_{frame}.rgba32f', np.float32).reshape(H_, W_, 4)
    zc = dep[..., 2].astype(np.float64)
    hdr = np.nan_to_num(np.fromfile(D + f'hdr_1_{frame}.rgba16f', np.float16).reshape(H_, W_, 4)[..., :3].astype(np.float64))
    luma = hdr @ np.array([0.2126, 0.7152, 0.0722])
    body = lbuf >= 0
    ys_, xs_ = np.nonzero(body)
    print(f'frame {frame}: raster bbox x=[{xs_.min()},{xs_.max()}] y=[{ys_.min()},{ys_.max()}] px={body.sum()} '
          f'view z [{zbuf[body].min():.0f},{zbuf[body].max():.0f}]')
    rel = np.where(body, (zc - zbuf) / zbuf, 0)
    cap = zc > 0
    hit = body & cap & (np.abs(rel) < TOL); hole = body & (~cap | (rel >= TOL)); occl = body & cap & (rel <= -TOL)
    print(f'  all: hit {hit.sum()} ({hit.sum() / body.sum():.3f}) hole {hole.sum()} occl {occl.sum()} hit luma mean {luma[hit].mean():.4f}')
    for g, nm in ((lambda l: l >= 100, 'ZONE (arms, r_yz>%d |x|<%d)' % (R, XB)), (lambda l: l < 100, 'CORE')):
        m = body & g(lbuf); h = m & hit
        print(f'  {nm}: px {m.sum()} hit {h.sum()} ({h.sum() / max(m.sum(), 1):.3f}) hole {(m & hole).sum()} ({(m & hole).sum() / max(m.sum(), 1):.3f}) luma {luma[h].mean():.4f}')
    for l in sorted(set(lbuf[body].tolist()), key=lambda v: -(lbuf == v).sum()):
        m = lbuf == l; n = m.sum()
        if n < 30: continue
        h = m & hit; rgb = hdr[h].mean(0) if h.any() else np.zeros(3)
        mat = l % 100
        print(f'  {"zone" if l >= 100 else "core"} mat{mat:>2} {"A" if mat in alpha else "-"} px={n:5d} hit={h.sum() / n:.3f} hole={(m & hole).sum() / n:.3f} occl={(m & occl).sum() / n:.3f} '
              f'luma={luma[h].mean() if h.any() else 0:.4f} rgb={rgb[0]:.4f},{rgb[1]:.4f},{rgb[2]:.4f}')
    np.save(f'/private/tmp/claude-501/-Users-asvetl-x3-mod/ec1d2a17-c1f5-4762-8f3d-98edd98ef126/scratchpad/lbuf_{frame}.npy', lbuf)
