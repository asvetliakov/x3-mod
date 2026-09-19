#!/usr/bin/env python3
"""Offline SSR feasibility probe over a --taa-debug dump: depth-reconstructed normals + a CPU screen-space march.
usage: [ZB=1] ssr_offline_probe.py <dump dir> <frame> [png prefix]   (ZB=1: view depth from RT2.b clip w instead of z/w .r)
Prints only aggregate numbers; docs/architecture/screen-space-reflections.md section 6 records the run153 results."""
import re, sys, subprocess, glob
import numpy as np
D = sys.argv[1].rstrip('/') + '/'; F = int(sys.argv[2]); PNG = sys.argv[3] if len(sys.argv) > 3 else None
log = glob.glob(D + 'session-*.log')[0]
line = subprocess.run(['grep', '-m1', '-E', r'^sun_shadow_apply_params device=1 frame=%d ' % F, log], capture_output=True, text=True).stdout
kv = dict(re.findall(r'(\w+)=([-\w.+]+)', line))
W, H = int(kv['width']), int(kv['height'])
m00, m11, m20, m21, m22, m32 = (float(kv[k]) for k in ('m00', 'm11', 'm20', 'm21', 'm22', 'm32'))
dep = np.fromfile(D + 'depth_1_%d.rgba32f' % F, dtype=np.float32).reshape(H, W, 4)[..., 0].astype(np.float64)
hdr = np.nan_to_num(np.fromfile(D + 'hdr_1_%d.rgba16f' % F, dtype=np.float16).reshape(H, W, 4)[..., :3].astype(np.float64), nan=0, posinf=65504, neginf=0)
luma = hdr @ np.array([0.2126, 0.7152, 0.0722])
geo = (dep >= 0) & (dep <= 1)
import os
zb = np.fromfile(D + 'depth_1_%d.rgba32f' % F, dtype=np.float32).reshape(H, W, 4)[..., 2].astype(np.float64)
z = np.where(geo, zb if os.environ.get('ZB') else m32 / (dep - m22), np.inf)
ys, xs = np.mgrid[0:H, 0:W]
nx = 2 * (xs + .5) / W - 1; ny = 1 - 2 * (ys + .5) / H
def unproject(px, py, zz): return np.stack([((2 * px / W - 1) - m20) / m00 * zz, ((1 - 2 * py / H) - m21) / m11 * zz, zz], -1)
P = unproject(xs + .5, ys + .5, np.where(geo, z, 1.0))
print('frame %d %dx%d m22=%g m32=%g near=%.3g' % (F, W, H, m22, m32, -m32 / m22))
print('geometry pixels %d (%.1f%% of frame); view z p5/p50/p95 = %s' % (geo.sum(), 100 * geo.mean(), np.percentile(z[geo], [5, 50, 95]).round(0)))

# normals from depth: per axis choose the neighbour with the smaller |dz| (AO-style best-of-two); a side is
# usable when the neighbour is geometry and its relative depth step is below REL
REL = 0.02
def side(dx, dy):
    q = np.roll(P, (-dy, -dx), (0, 1)); g = np.roll(geo, (-dy, -dx), (0, 1)); zz = np.roll(z, (-dy, -dx), (0, 1))
    with np.errstate(invalid='ignore'): ok = g & geo & (np.abs(zz - z) < REL * z)
    return q - P, ok, np.where(ok, np.abs(zz - z), np.inf)
r_, rok, rdz = side(1, 0); l_, lok, ldz = side(-1, 0); d_, dok, ddz = side(0, 1); u_, uok, udz = side(0, -1)
hx = np.where((rdz <= ldz)[..., None], r_, -l_); hok = rok | lok
vy = np.where((ddz <= udz)[..., None], d_, -u_); vok = dok | uok
N = np.cross(vy, hx)  # screen y down, x right, z forward (LH): faces the camera (z < 0)
nl = np.linalg.norm(N, axis=-1); nok = geo & hok & vok & (nl > 0)
N = N / np.where(nl > 0, nl, 1)[..., None]
N = np.where((N[..., 2] > 0)[..., None], -N, N)
print('normal reconstructable (both axes have a continuous neighbour at %.0f%% rel depth): %.1f%% of geometry; both sides on both axes: %.1f%%'
      % (100 * REL, 100 * nok.sum() / geo.sum(), 100 * (geo & rok & lok & dok & uok).sum() / geo.sum()))

mot = np.fromfile(D + 'motion_1_%d.rgba32f' % F, dtype=np.float32).reshape(H, W, 4)[..., 3]
print('sentinel-depth pixels flagged motion alpha==1 (glass/lattice mask input, no depth): %.1f%% of frame' % (100 * ((dep <= -.5) & (mot == 1)).mean()))
V = P / np.linalg.norm(P, axis=-1, keepdims=True)
ndv = (V * N).sum(-1)
R = V - 2 * ndv[..., None] * N
sel = nok & (ndv < 0)
idx = np.flatnonzero(sel)
P0 = P.reshape(-1, 3)[idx]; Rd = R.reshape(-1, 3)[idx]; Z0 = z.reshape(-1)[idx]
fres = 0.04 + 0.96 * (1 - np.clip(-ndv.reshape(-1)[idx], 0, 1)) ** 5
print('rays: %d; toward camera (R.z<0): %.1f%%; Schlick(F0=.04) mean %.3f, share with F>0.2: %.1f%%' % (idx.size, 100 * (Rd[:, 2] < 0).mean(), fres.mean(), 100 * (fres > .2).mean()))

def march(stride, nsteps, thick_rel, label):
    """Perspective-correct linear march in pixel space (McGuire & Mara style), all rays at once."""
    NEAR = -m32 / m22 * 1.01
    maxd = np.full(idx.size, 1e7)
    back = Rd[:, 2] < 0
    maxd[back] = np.minimum(maxd[back], (NEAR - P0[back, 2]) / Rd[back, 2])
    P0b = P0 + N.reshape(-1, 3)[idx] * (Z0 * 2e-3)[:, None]  # normal bias ~ one texel
    P1 = P0b + Rd * maxd[:, None]
    def proj(p): return np.stack([((p[:, 0] / p[:, 2] * m00 + m20) * .5 + .5) * W, (.5 - .5 * (p[:, 1] / p[:, 2] * m11 + m21)) * H], -1), 1 / p[:, 2]
    s0, k0 = proj(P0b); s1, k1 = proj(P1)
    dpx = s1 - s0; ln = np.maximum(np.abs(dpx).max(-1), 1e-6)  # pixels along the major axis
    dstep = dpx / ln[:, None] * stride; dk = (k1 - k0) / ln * stride
    state = np.zeros(idx.size, np.int8)  # 0 marching, 1 hit, 2 left screen, 3 ran out of ray, 4 out of steps
    hitpix = np.full(idx.size, -1, np.int64); hitstep = np.zeros(idx.size, np.int32)
    act = np.arange(idx.size); zprev = Z0.copy(); front = np.zeros(idx.size, bool)
    depf = z.reshape(-1)
    for i in range(1, nsteps + 1):
        t = i * stride
        s = s0[act] + dstep[act] * i; k = k0[act] + dk[act] * i
        rz = 1 / k
        px = np.floor(s[:, 0]).astype(np.int64); py = np.floor(s[:, 1]).astype(np.int64)
        off = (px < 0) | (px >= W) | (py < 0) | (py >= H)
        end = (t > ln[act]) & ~off
        pix = np.clip(py, 0, H - 1) * W + np.clip(px, 0, W - 1)
        sz = depf[pix]
        zmin = np.minimum(zprev[act], rz); zmax = np.maximum(zprev[act], rz)
        hit = ~off & ~end & front[act] & np.isfinite(sz) & (zmax >= sz) & (zmin <= sz + thick_rel * sz)
        front[act] = rz < sz * (1 - 1e-3)
        state[act[off]] = 2; state[act[end]] = 3; state[act[hit]] = 1
        hitpix[act[hit]] = pix[hit]; hitstep[act[hit]] = i
        zprev[act] = rz
        act = act[~(off | end | hit)]
        if act.size == 0: break
    state[act] = 4
    n = idx.size; h = state == 1
    print('%s stride=%g steps=%d thick=%.3f: hit %.1f%% | left screen %.1f%% | ray ended over sky/near plane %.1f%% | out of steps %.1f%% ; hits = %.1f%% of frame; median hit step %d, p90 %d'
          % (label, stride, nsteps, thick_rel, 100 * h.mean(), 100 * (state == 2).mean(), 100 * (state == 3).mean(), 100 * (state == 4).mean(), 100 * h.sum() / (W * H),
             np.percentile(hitstep[h], 50) if h.any() else 0, np.percentile(hitstep[h], 90) if h.any() else 0))
    return state, hitpix

ref_state, ref_hit = march(1, 1600, 0.02, 'REFERENCE')
# visible contribution: reflected radiance * Fresnel vs the receiver's own radiance; display-relative threshold via luma
h = ref_state == 1
hl = np.zeros(idx.size); hl[h] = luma.reshape(-1)[ref_hit[h]]
own = luma.reshape(-1)[idx]
for name, refl in (('Schlick F0=0.04', fres), ('flat 0.25', np.full(idx.size, .25))):
    add = hl * refl
    vis = h & (add > 0.1 * np.maximum(own, 1e-4)) & (add > 0.002)
    print('  %s: rays whose SSR term exceeds 10%% of receiver luma (and 0.002 abs): %.2f%% of rays, %.2f%% of frame; median hit luma %.4f vs receiver %.4f'
          % (name, 100 * vis.mean(), 100 * vis.sum() / (W * H), np.percentile(hl[h], 50) if h.any() else 0, np.percentile(own, 50)))
# self-hit share: hit lands within 8 px of the origin (likely same surface / bias artefact)
oy, ox = np.divmod(idx, W); hy, hx_ = np.divmod(np.maximum(ref_hit, 0), W)
near8 = h & (np.maximum(np.abs(oy - hy), np.abs(ox - hx_)) <= 8)
far8 = h & ~near8
for name, refl in (('Schlick', fres), ('flat .25', np.full(idx.size, .25))):
    add = hl * refl; vis = far8 & (add > 0.1 * np.maximum(own, 1e-4)) & (add > 0.002)
    print('  hits beyond 8 px: %.1f%% of rays, %.2f%% of frame; of those visible (%s): %.2f%% of frame' % (100 * far8.mean(), 100 * far8.sum() / (W * H), name, 100 * vis.sum() / (W * H)))
nearf = Z0 < 3000
print('  receivers with view z < 3000: %.1f%% of rays; their hit rate %.1f%%, beyond-8px hit rate %.1f%%' % (100 * nearf.mean(), 100 * h[nearf].mean() if nearf.any() else 0, 100 * far8[nearf].mean() if nearf.any() else 0))
print('  hits within 8 px of origin (self/adjacent-surface): %.1f%% of hits' % (100 * near8.sum() / max(h.sum(), 1)))
for stride, steps, th in ((8, 32, .02), (4, 48, .02)):
    st, hp = march(stride, steps, th, 'BUDGET')
    both = (st == 1) & h
    agree = both & (np.maximum(np.abs(np.divmod(hp, W)[0] - hy), np.abs(np.divmod(hp, W)[1] - hx_)) <= stride)
    print('  vs reference: recall %.1f%% of reference hits, false hits (budget hit, reference none) %.1f%% of budget hits, position agrees within one stride %.1f%% of common'
          % (100 * both.sum() / max(h.sum(), 1), 100 * ((st == 1) & ~h).sum() / max((st == 1).sum(), 1), 100 * agree.sum() / max(both.sum(), 1)))
if PNG:
    from PIL import Image
    img = np.zeros((H * W, 3), np.uint8); img[idx[ref_state == 1]] = (0, 255, 0); img[idx[ref_state == 2]] = (90, 0, 0); img[idx[ref_state == 3]] = (0, 0, 120); img[idx[ref_state == 4]] = (120, 120, 0)
    Image.fromarray(img.reshape(H, W, 3)).save(PNG + '_class.png')
    Image.fromarray(((N * .5 + .5) * 255 * nok[..., None]).astype(np.uint8)).save(PNG + '_normal.png')
