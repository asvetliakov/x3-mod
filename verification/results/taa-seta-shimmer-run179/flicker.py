#!/usr/bin/env python3
"""Temporal flicker at stable world points in the resolved TAA image of one F8 burst, and the lock / far / thin / motion-cap
state of the flickering pixels. Every routed hull pixel of the burst's last frame is traced back through the RT1 motion
(nearest texel's displacement, sub-pixel position kept) over all 16 frames; the resolved luma (taa rgba16f, bilinear) and
the raw jittered scene luma (hdr rgba16f, nearest) are sampled along the chain in the lock's q-code domain
(255 L / (1 + L)). Flicker = the chain's high-pass rms (first differences minus their mean) >= THR codes and sign flips of
consecutive differences on >= 50 % of the differences. Displacements include the current jitter as the resolve does. Usage: flicker.py <capdir> <log> <first_frame> [THR] [node=label,...]
(nodes: engine node addresses from cull_census; their object_bounds rows of the last frame select the object's pixels)"""
import sys, re, subprocess, numpy as np
W, H, L_HOLD = 5120, 1440, 8
cap, log, f0 = sys.argv[1], sys.argv[2], int(sys.argv[3])
THR = float(sys.argv[4]) if len(sys.argv) > 4 else 3.0
NODES = dict(a.split('=') for a in sys.argv[5].split(',')) if len(sys.argv) > 5 else {}
_t = subprocess.run(['grep', '-m1', '-E', '^motion_output_taa device=1 initialize', log], capture_output=True, text=True).stdout
FAR_F0, FAR_F1 = float(re.search(r'far_f0=(\S+)', _t).group(1)), float(re.search(r'far_f1=(\S+)', _t).group(1))
frames = list(range(f0, f0 + 16))
def cam(f):
    l = subprocess.run(['grep', '-m1', '-E', f'^camera_state device=1 frame={f} ', log], capture_output=True, text=True).stdout
    kv = dict(re.findall(r'(\w+)=(\S+)', l))
    R = np.array([[float(kv[f'r{i}{j}']) for j in range(3)] for i in range(3)])
    return R, float(kv['p00']), float(kv['p11']), float(kv['p22']), float(kv['p32'])
def jit(f):
    l = subprocess.run(['grep', '-m1', '-E', f'^motion_output_frame device=1 frame={f} ', log], capture_output=True, text=True).stdout
    kv = dict(re.findall(r'(\w+)=(\S+)', l))
    return float(kv['jitter_x']), float(kv['jitter_y'])
JIT = {f: jit(f) for f in frames}
def rd(kind, f, dt, ch):
    return np.fromfile(f'{cap}/{kind}_1_{f}.{ {"taa":"rgba16f","hdr":"rgba16f","motion":"rgba32f","depth":"rgba32f","taa_lock":"bgra8","taa_age":"r32f","present":"bgra8"}[kind] }', dt).reshape(H, W, ch)
def luma(a):
    a = np.nan_to_num(a.astype(np.float32), nan=0, posinf=0, neginf=0)
    return np.maximum(0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2], 0)
q = lambda L: 255 * L / (1 + L)
def bil(img, px, py):
    x = np.clip(px - 0.5, 0, W - 1.001); y = np.clip(py - 0.5, 0, H - 1.001)
    ix, iy = x.astype(int), y.astype(int); fx, fy = x - ix, y - iy
    return (img[iy, ix] * (1 - fx) * (1 - fy) + img[iy, ix + 1] * fx * (1 - fy) + img[iy + 1, ix] * (1 - fx) * fy + img[iy + 1, ix + 1] * fx * fy)
n = frames[-1]
R1, p00, p11, p22, p32 = cam(n)
dep = rd('depth', n, np.float32, 4)
mot = rd('motion', n, np.float32, 4)
hull = (mot[..., 3] == 1) & (dep[..., 0] >= 0) & (dep[..., 0] < 1) & (dep[..., 2] > 0)
ys, xs = np.nonzero(hull)
N = len(xs)
vz = dep[ys, xs, 2]; vote = dep[ys, xs, 3]
# farw (src/temporal/resolve.h:205 far_gate; resolve.hlsl:489): footprint 60 / 68 units per px as device depth.
scale = p00 * W * 0.5
d0, d1 = p22 + p32 / (FAR_F0 * scale), p22 + p32 / (FAR_F1 * scale)
farw = np.clip((dep[ys, xs, 0] - d0) / (d1 - d0), 0, 1)
px, py = xs + 0.5, ys + 0.5
# Screen displacement (resolve.hlsl:767) of the last frame; rotation-only path from camera_state (columns = camera axes:
# rotation_path.py, residual 0.8-1.0 px on pixels beyond 1e5 units against 77-87 px for the other orientation).
# RT1 xy is the previous unjittered UV of the content at this jittered sample; the shader adds the current jitter
# (resolve.hlsl:648): measured offset at rest = -jitter px on both axes (frames 13095/13096/13104).
sx = mot[ys, xs, 0] * W - px + JIT[n][0]; sy = mot[ys, xs, 1] * H - py + JIT[n][1]
R0 = cam(n - 1)[0]
nx, ny = px / W * 2 - 1, 1 - py / H * 2
v = np.stack([nx / p00, ny / p11, np.ones_like(nx)], 1)
vp = v @ (R1.T @ R0)
rx = (vp[:, 0] / vp[:, 2] * p00 * 0.5 + 0.5) * W - px; ry = (0.5 - vp[:, 1] / vp[:, 2] * p11 * 0.5) * H - py
relx, rely = sx - rx, sy - ry
par2 = np.minimum(relx ** 2 + rely ** 2, sx ** 2 + sy ** 2)
A = -(1 - 0.7) / (8 ** 2 - 2 ** 2); B = 1 - A * 4
cap_w = np.clip(np.maximum(0.7, par2 * A + B), 0, 1)
spd = np.hypot(sx, sy)
lock = rd('taa_lock', n, np.uint8, 4)[ys, xs]  # bytes B G R A
lb = lock[:, 0].astype(int); life = lb & 31; mode = lb >> 7
age = np.abs(rd('taa_age', n, np.float32, 1)[ys, xs, 0])
frac16 = np.where(age <= 65, np.round((age - np.floor(age)) * 65536), 0)
hold = frac16 % 128
# The camera gate's stored peak-hold pair (resolve.hlsl:181 / closureHold :418): pair = q (L + 1) + t; this frame's
# openC <= 1 - q / 4 (q = this frame's closed quarters or the carried level).
pair = np.floor(frac16 / 128); qC = np.floor((pair + 0.5) / (L_HOLD + 1)); openC_ub = 1 - qC / 4
Lfin = luma(rd('taa', n, np.float16, 4))[ys, xs]
del dep, mot
# Chain back through the burst.
T = len(frames)
st = np.zeros((T, N), np.float32); sh = np.zeros((T, N), np.float32); locked = np.zeros((T, N), bool); lifes = np.zeros((T, N), np.int16); sp = np.zeros((T, N), np.float32)
ok = np.ones(N, bool)
cx, cy = px.copy(), py.copy()
for k in range(T - 1, -1, -1):
    f = frames[k]
    st[k] = q(bil(luma(rd('taa', f, np.float16, 4)), cx, cy))
    ix = np.clip(cx.astype(int), 0, W - 1); iy = np.clip(cy.astype(int), 0, H - 1)
    sh[k] = q(luma(rd('hdr', f, np.float16, 4))[iy, ix])
    pr = rd('present', f, np.uint8, 4).astype(np.float32)  # bytes B G R A, display codes
    sp[k] = bil(0.0722 * pr[..., 0] + 0.7152 * pr[..., 1] + 0.2126 * pr[..., 2], cx, cy); del pr
    lifes[k] = rd('taa_lock', f, np.uint8, 4)[iy, ix, 0] & 31
    locked[k] = lifes[k] > 0
    if k == 0:
        # The first burst frame follows an ordinary frame: its motion at the chain's world point is the in-flight step.
        m0 = rd('motion', f, np.float32, 4)[iy, ix]
        s0 = np.where(m0[:, 3] == 1, np.hypot(m0[:, 0] * W - (ix + 0.5) + JIT[f][0], m0[:, 1] * H - (iy + 0.5) + JIT[f][1]), np.nan)
        break
    m = rd('motion', f, np.float32, 4)[iy, ix]
    ok &= m[:, 3] == 1
    cx = cx + (m[:, 0] * W - (ix + 0.5) + JIT[f][0]); cy = cy + (m[:, 1] * H - (iy + 0.5) + JIT[f][1])
    ok &= (cx >= 1) & (cx < W - 1) & (cy >= 1) & (cy < H - 1)
def hp(s):
    d = np.diff(s, axis=0)
    hpr = np.sqrt(((d - d.mean(0)) ** 2).mean(0))
    flips = ((d[1:] * d[:-1]) < 0) & (np.abs(d[1:]) >= THR) & (np.abs(d[:-1]) >= THR)
    return hpr, flips.sum(0) / (d.shape[0] - 1)
hr, fr = hp(st); hr_in, fr_in = hp(sh); hr_p, fr_p = hp(sp)
# Unsharpened display proxy: the resolved luma through this burst's own taa -> present tone curve (binned median of the
# last frame's hull pixels, 64 log bins), so the sharpen's share of the presented flicker is the ratio to this series.
Lbins = np.logspace(-3, 1.5, 65); idx = np.digitize(Lfin, Lbins)
cent, med = [], []
for b in range(1, 65):
    sel_b = idx == b
    if sel_b.sum() >= 50: cent.append(np.sqrt(Lbins[b - 1] * Lbins[b])); med.append(np.median(sp[-1][sel_b]))
Lser = st / np.maximum(255 - st, 1e-3)
su = np.interp(np.log(np.maximum(Lser, 1e-4)), np.log(cent), med)
hr_u, fr_u = hp(su)
flu = ok & (hr_u >= THR) & (fr_u >= 0.5)
fl = ok & (hr >= THR) & (fr >= 0.5)
flp = ok & (hr_p >= THR) & (fr_p >= 0.5)  # the same test on the presented (sharpened, tonemapped) 8-bit image
print(f'far band {FAR_F0:g}/{FAR_F1:g} u/px (eligible beyond view z {FAR_F0*scale:.0f})')
print(f'burst {f0}-{n}: hull px (routed, valid depth) {N}; chain valid over 16 frames {ok.sum()} ({ok.mean():.3f}); THR {THR} codes')
print(f'  screen speed px/frame p10/p50/p90 all hull {np.percentile(spd,[10,50,90]).round(2)}; rotation-only |d| p50 {np.median(np.hypot(rx,ry)):.2f}')
print(f'  flickering (resolved) px {fl.sum()} = {fl.sum()/max(ok.sum(),1):.4f} of valid hull; raw-input flicker share {(ok & (hr_in>=THR) & (fr_in>=0.5)).sum()/max(ok.sum(),1):.4f}')
def desc(sel, tag):
    c = sel.sum()
    if c == 0:
        print(f'  [{tag}] 0 px'); return
    g = hr[sel] / np.maximum(hr_in[sel], 1e-3)
    print(f'  [{tag}] px {c}; luma L p10/p50/p90 {np.percentile(Lfin[sel],[10,50,90]).round(3)}; L>1 {np.mean(Lfin[sel]>1):.3f}; '
          f'view z p10/p50/p90 {np.percentile(vz[sel],[10,50,90]).round(0)}')
    print(f'      hp rms out p50 {np.median(hr[sel]):.2f} codes, in p50 {np.median(hr_in[sel]):.2f}, out/in p50 {np.median(g):.3f}; '
          f'locked now {np.mean(life[sel]>0):.3f}, locked any chain frame {np.mean(locked[:,sel].any(0)):.3f}, mean chain lock share {locked[:,sel].mean():.3f}')
    print(f'      farw>0 {np.mean(farw[sel]>0):.3f} (farw=1 {np.mean(farw[sel]>=1):.3f}); thin vote {np.mean((vote[sel]>=0)&(vote[sel]<1)):.3f}; '
          f'region hold>0 {np.mean(hold[sel]>0):.3f} (flagged now {np.mean(hold[sel]==L_HOLD):.3f}); lane motion mode {np.mean(mode[sel]==1):.3f}')
    print(f'      speed p50 {np.median(spd[sel]):.2f} px/f; >0.25 {np.mean(spd[sel]>0.25):.3f}; >2 {np.mean(spd[sel]>2):.3f}; relative p50 {np.median(np.hypot(relx,rely)[sel]):.2f}; '
          f'motion cap<1 {np.mean(cap_w[sel]<1):.3f} (cap=0.7 {np.mean(cap_w[sel]<=0.7):.3f})')
    print(f'      camera gate (age dump) openC<=0.5 {np.mean(openC_ub[sel]<=0.5):.3f}, openC=0 {np.mean(openC_ub[sel]<=0):.3f}; '
          f'locked now AND openC>0.5 (lock weight applied) {np.mean((life[sel]>0)&(openC_ub[sel]>0.5)):.3f}; '
          f'region AND openC>0.5 (thin weight applied) {np.mean((hold[sel]>0)&(openC_ub[sel]>0.5)):.3f}')
    a, b = lifes[:-1, sel], lifes[1:, sel]
    both = (a > 0) & (b > 0)
    print(f'      lock lifetime transitions on locked chain frames {both.sum()}: frozen (creation gate closed) {np.sum(both&(b==a))}, '
          f'decrement {np.sum(both&(b==a-1))}, refresh {np.sum(both&(b==16)&(a!=16))}; lock lost (t>0 -> 0) {np.sum((a>0)&(b==0))} '
          f'(expiry t=1 {np.sum((a==1)&(b==0))}, killed t>1 {np.sum((a>1)&(b==0))})')
    closed = openC_ub[sel] <= 0.5
    inel = ~closed & (farw[sel] <= 0) & (hold[sel] <= 0)
    unl = ~closed & ~inel & (life[sel] <= 0)
    app = ~closed & ~inel & (life[sel] > 0)
    print(f'      exclusive gate classes: camera gate closed {closed.mean():.3f} | open but ineligible (farw=0, no region) {inel.mean():.3f} | '
          f'eligible, not locked {unl.mean():.3f} | locked and open (hold applied) {app.mean():.3f}; display code p50 {np.median(sp[-1][sel]):.0f}')
    s0s = s0[sel]
    print(f'      in-flight step (first burst frame) p50 {np.nanmedian(s0s) if np.isfinite(s0s).any() else float("nan"):.3f} px/f; <0.25 {np.mean(s0s<0.25):.3f}, <0.5 {np.mean(s0s<0.5):.3f}; '
          f'gate-closed class with in-flight step <0.5 (lever a reach) {np.mean(closed&(s0s<0.5)):.3f}, of those farw>0 or region {np.mean(closed&(s0s<0.5)&((farw[sel]>0)|(hold[sel]>0))):.3f}')
    print(f'      presented image: hp rms p50 {np.median(hr_p[sel]):.2f} display codes; present-flicker share {np.mean(flp[sel]):.3f}')
print(f'  flickering (presented, display codes) px {flp.sum()} = {flp.sum()/max(ok.sum(),1):.4f} of valid hull; overlap with resolved set {(flp&fl).sum()}')
print(f'  flickering (unsharpened display proxy) px {flu.sum()} = {flu.sum()/max(ok.sum(),1):.4f} of valid hull; '
      f'presented/proxy hp rms p50: all hull {np.median((hr_p/np.maximum(hr_u,1e-3))[ok]):.2f}, present-flickering {np.median((hr_p/np.maximum(hr_u,1e-3))[flp]):.2f}')
for f0_, f1_ in [(60, 68), (30, 34), (20, 24)]:
    zr = f0_ * scale
    print(f'  far band {f0_}/{f1_} u/px (farw>0 beyond z {zr:.0f}): present-flickering px beyond it {np.mean(vz[flp] > zr):.3f}; '
          f'of those with the age-dump camera gate open {np.mean((vz[flp] > zr) & (openC_ub[flp] > 0.5)):.3f}')
desc(ok, 'all valid hull')
desc(flp, 'present-flickering')
desc(flp & (Lfin > 1), 'present-flickering, L>1')
desc(fl, 'flickering')
desc(fl & (Lfin > 1), 'flickering, L>1')
obj = {}
if NODES:
    rows = subprocess.run(['grep', '-E', f'^object_bounds device=1 frame={n} ', log], capture_output=True, text=True).stdout.splitlines()
    dd = d0 * 0 + 0
    devd = None
    for label, node in NODES.items():
        m_ = np.zeros(N, bool); cnt = 0
        for l in rows:
            kv = dict(re.findall(r'(\w+)=(\S+)', l))
            if kv.get('node') != node: continue
            cnt += 1
            if devd is None: devd = rd('depth', n, np.float32, 4)[ys, xs, 0]
            x0, y0, x1, y1 = (float(kv[k]) for k in ('sx0', 'sy0', 'sx1', 'sy1'))
            z0, z1 = float(kv['zmin']), float(kv['zmax'])
            m_ |= (px >= x0) & (px <= x1) & (py >= y0) & (py <= y1) & (devd >= z0 - 2e-6) & (devd <= z1 + 2e-6)
        obj[label] = m_
        print(f'  object {label} node {node}: {cnt} draws, {m_.sum()} hull px, chain-valid {(m_&ok).sum()}, view z p10/p50/p90 '
              f'{np.percentile(vz[m_],[10,50,90]).round(0) if m_.any() else "-"}, capture-frame speed p50 {np.median(spd[m_]) if m_.any() else float("nan"):.2f}')
        desc(m_ & ok, f'{label} all')
        desc(m_ & flp, f'{label} present-flickering')
        desc(m_ & flp & (Lfin > 1), f'{label} present-flickering, L>1')
for lo, hi, nm in [(0, 300, 'z<300 (own ship)'), (300, FAR_F0 * scale, f'300..{FAR_F0*scale:.0f} (farw=0)'), (FAR_F0 * scale, 1e12, 'far band')]:
    s = fl & (vz >= lo) & (vz < hi)
    desc(s, 'flickering ' + nm)
    print(f'      share of valid hull in this z range that flickers: {s.sum()/max((ok&(vz>=lo)&(vz<hi)).sum(),1):.4f} (valid px {(ok&(vz>=lo)&(vz<hi)).sum()})')
# Bounding boxes of the flicker in 64-px tiles (top 8 tiles by count) to name the object.
tiles = {}
for x, y in zip(xs[fl] // 128, ys[fl] // 128):
    tiles[(x, y)] = tiles.get((x, y), 0) + 1
top = sorted(tiles.items(), key=lambda t: -t[1])[:8]
print('  top 128-px tiles (x0,y0: count, z p50, L p50): ' + '; '.join(
    f'{a*128},{b*128}: {c}, {np.median(vz[fl & (xs//128==a) & (ys//128==b)]):.0f}, {np.median(Lfin[fl & (xs//128==a) & (ys//128==b)]):.2f}' for (a, b), c in top))
