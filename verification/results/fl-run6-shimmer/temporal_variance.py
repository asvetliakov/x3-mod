#!/usr/bin/env python3
"""Motion-compensated temporal luma variance of an 8-frame F8 capture, per node, with a CPU TAA model.

Usage: temporal_variance.py CAPDIR DRAWS_JSON FIRST_FRAME x0 y0 x1 y1 OUT.json
DRAWS_JSON is extract_draws.py's output (object_bounds boxes, ev per frame). Only crops are read
(numpy memmap); no capture bytes are written.

Per geometry pixel of the last frame (motion alpha 1, valid depth) the trajectory is followed back
through each frame's routed previous UV (RT1 .xy) to frame 0; at each frame the pre-TAA luma is
sampled bilinearly from hdr_*.rgba16f and tone-compressed to display codes q = 255 * x/(1+x),
x = luma * 2^ev (ev from hdr_frame). Coverage change: at some step the nearest RT2 texel is the
sentinel, its view w (.b) differs from the last frame's by more than 10 %, or the detrended w residual along the
trajectory exceeds 0.1 % of w (the nearest sample switches surface). Lit pixels are also binned by RT2 .g (sun share).
CPU TAA model [inferred, not the shipped resolve]: H0 = C0, Hk = w*clip(H_{k-1}) + (1-w)*Ck with w = 0.85 and
the clip to the current 3x3 mean +/- 1.25 sigma intersected with min/max (resolve.hlsl clipGamma) in q;
'locked' is the same with w = 0.985 and the 7x7 min/max box (the luma-lock / far path).
Reported per node: mean |dC| (pre-TAA frame-to-frame), mean |dH| over frames 4..7 (TAA model output),
mean history clip distance |clip(H)-H| (history rejection), std of C over 8 frames, view w, sun share (.g),
coverage-change fraction, screen speed px/frame."""
import json, sys, numpy as np
W, H = 5120, 1440
cap, dj, f0 = sys.argv[1], sys.argv[2], int(sys.argv[3]); X0, Y0, X1, Y1 = map(int, sys.argv[4:8]); out = sys.argv[8]
D = json.load(open(dj)); frames = [f0 + i for i in range(8)]
M = 120; cx0, cy0, cx1, cy1 = max(X0 - M, 0), max(Y0 - M, 0), min(X1 + M, W), min(Y1 + M, H)
def mm(kind, f, ext, dt, ch):
    return np.memmap('%s/%s_1_%d.%s' % (cap, kind, f, ext), dtype=dt, mode='r', shape=(H, W, ch))[cy0:cy1, cx0:cx1]
q, rt2, mot = [], [], []
for f in frames:
    ev = float(D['frame_rows'][str(f)]['hdr_frame']['ev'])
    c = np.asarray(mm('hdr', f, 'rgba16f', '<f2', 4)[..., :3], dtype=np.float32)
    L = np.maximum(c @ np.array([0.2126, 0.7152, 0.0722], np.float32), 0) * 2**ev
    q.append(255 * L / (1 + L))
    rt2.append(np.asarray(mm('depth', f, 'rgba32f', '<f4', 4), dtype=np.float32))
    mot.append(np.asarray(mm('motion', f, 'rgba32f', '<f4', 4), dtype=np.float32))
h, w = q[0].shape
def nb(a, r):
    p = np.pad(a, r, mode='edge'); s = [p[r+dy:r+dy+h, r+dx:r+dx+w] for dy in range(-r, r+1) for dx in range(-r, r+1)]
    s = np.stack(s); return s.mean(0), s.std(0), s.min(0), s.max(0)
stats3 = [nb(a, 1) for a in q]; box7 = [nb(a, 3)[2:] for a in q]
def bil(a, x, y):
    x = np.clip(x, 0, w - 1.001); y = np.clip(y, 0, h - 1.001); xi = x.astype(int); yi = y.astype(int); fx = x - xi; fy = y - yi
    return (a[yi, xi]*(1-fx)*(1-fy) + a[yi, xi+1]*fx*(1-fy) + a[yi+1, xi]*(1-fx)*fy + a[yi+1, xi+1]*fx*fy)
last = 7
ys, xs = np.mgrid[Y0-cy0:Y1-cy0, X0-cx0:X1-cx0]
ys = ys.ravel(); xs = xs.ravel()
geo = (mot[last][ys, xs, 3] == 1) & (rt2[last][ys, xs, 0] >= 0)
ys, xs = ys[geo], xs[geo]
n = ys.size
px = np.zeros((8, n), np.float32); py = np.zeros((8, n), np.float32); px[last] = xs; py[last] = ys
ok = np.ones(n, bool); cov = np.zeros(n, bool); w7 = rt2[last][ys, xs, 2]
for k in range(last, 0, -1):
    xi = np.clip(np.rint(px[k]).astype(int), 0, w-1); yi = np.clip(np.rint(py[k]).astype(int), 0, h-1)
    m = mot[k][yi, xi]
    ok &= (m[:, 3] == 1) & (xi > 0) & (yi > 0) & (xi < w-2) & (yi < h-2)
    px[k-1] = m[:, 0]*W - 0.5 - cx0; py[k-1] = m[:, 1]*H - 0.5 - cy0
    xj = np.clip(np.rint(px[k-1]).astype(int), 0, w-1); yj = np.clip(np.rint(py[k-1]).astype(int), 0, h-1)
    d = rt2[k-1][yj, xj]
    cov |= (d[:, 0] < 0) | (np.abs(d[:, 2] - w7) > 0.1*np.abs(w7))
C = np.stack([bil(q[k], px[k], py[k]) for k in range(8)])
# Surface switching: the nearest RT2 view w along the trajectory, detrended (the camera approach is linear over 8 frames);
# a residual above 0.1 % of w means the pixel's nearest sample lands on a different surface in some frames (coverage).
WK = np.stack([rt2[k][np.clip(np.rint(py[k]).astype(int), 0, h-1), np.clip(np.rint(px[k]).astype(int), 0, w-1), 2] for k in range(8)])
WK = np.where(WK > 0, WK, np.nan)
kk = np.arange(8, dtype=np.float32)[:, None]
fin = np.isfinite(WK).all(0)
Wm = np.nan_to_num(WK); slope = ((kk - 3.5)*(Wm - Wm.mean(0))).sum(0)/((kk - 3.5)**2).sum()
wres = np.where(fin, np.sqrt(((Wm - Wm.mean(0) - slope*(kk - 3.5))**2).mean(0))/np.maximum(np.abs(Wm.mean(0)), 1), 1.0)
cov |= wres > 1e-3
def taa(wt, wide):
    Hs = [C[0]]; rej = []
    for k in range(1, 8):
        xi = np.clip(np.rint(px[k]).astype(int), 0, w-1); yi = np.clip(np.rint(py[k]).astype(int), 0, h-1)
        if wide:
            lo, hi = box7[k][0][yi, xi], box7[k][1][yi, xi]
        else:
            mu, sd, mn, mx = (s[yi, xi] for s in stats3[k]); lo = np.maximum(mn, mu-1.25*sd); hi = np.minimum(mx, mu+1.25*sd)
        hc = np.clip(Hs[-1], lo, hi); rej.append(np.abs(hc - Hs[-1])); Hs.append(wt*hc + (1-wt)*C[k])
    return np.stack(Hs), np.stack(rej)
Hb, Rb = taa(0.85, False); Hl, Rl = taa(0.985, True)
def taa_lock(T=16, rho=.25, tau=3., rel=.65):
    # Luma-lock model (taa-luminance-lock.md sections 1, 10, 11; screen and camera gates taken open, farw = 1, as
    # measured on the first in-flight frame): residual e = C - history, a sign flip on consecutive frames with the
    # larger |e| >= max(tau, rho * 3x3 range) and the smaller >= tau creates / refreshes t = T; running-mean 3x3
    # reference, release below rel; strength saturate(t/4); weight lerp(0.85, 0.985), 7x7 box when locked.
    Hs = [C[0]]; t = np.zeros(n, np.float32); R = np.zeros(n, np.float32); ep = np.zeros(n, np.float32); locked = []
    for k in range(1, 8):
        xi = np.clip(np.rint(px[k]).astype(int), 0, w-1); yi = np.clip(np.rint(py[k]).astype(int), 0, h-1)
        mu, sd, mn, mx = (s_[yi, xi] for s_ in stats3[k]); rng = mx - mn
        e = C[k] - Hs[-1]
        big, small = np.maximum(np.abs(e), np.abs(ep)), np.minimum(np.abs(e), np.abs(ep))
        flip = (e*ep < 0) & (big >= np.maximum(tau, rho*rng)) & (small >= tau)
        R = np.where(flip & (t <= 0), mu, R); t = np.where(flip, T, np.maximum(t - 1, 0))
        R = np.where(t > 0, R + (mu - R)/8, R)
        t = np.where((t > 0) & (np.minimum(R, mu)/np.maximum(np.maximum(R, mu), 1e-3) < rel), 0, t)
        l = np.clip(t/4, 0, 1); wt = 0.85 + l*(0.985 - 0.85)
        lo3 = np.maximum(mn, mu - 1.25*sd); hi3 = np.minimum(mx, mu + 1.25*sd)
        lo = np.where(t > 0, box7[k][0][yi, xi], lo3); hi = np.where(t > 0, box7[k][1][yi, xi], hi3)
        Hs.append(wt*np.clip(Hs[-1], lo, hi) + (1 - wt)*C[k]); ep = e; locked.append(t > 0)
    return np.stack(Hs), np.stack(locked)
Hm, Lm = taa_lock()
speed = np.hypot(px[last]-px[last-1], py[last]-py[last-1])
share = rt2[last][ys, xs, 1]
nodes = {}
for d in D['draws']:
    if d['frame'] == frames[last] and 'box' in d and d.get('node_handle'):
        b = d['box']; e = nodes.setdefault(d['node_handle'], {'box': [1e9, 1e9, -1e9, -1e9], 'model': d.get('model'), 'ps': {}, 'alphatest': 0, 'draws': 0, 'lod': d.get('lod')})
        e['box'] = [min(e['box'][0], b[0]), min(e['box'][1], b[1]), max(e['box'][2], b[2]), max(e['box'][3], b[3])]
        e['ps'][d['ps']] = e['ps'].get(d['ps'], 0) + 1; e['alphatest'] += d['st'].get('alphatest', 0); e['draws'] += 1
gx, gy = xs + cx0, ys + cy0
owner = np.full(n, '', object); area = np.full(n, np.inf)
for hnd, e in nodes.items():
    b = e['box']; a = (b[2]-b[0])*(b[3]-b[1])
    if a <= 0 or a > 0.5*W*H: continue
    inside = (gx >= b[0]-1) & (gx <= b[2]+1) & (gy >= b[1]-1) & (gy <= b[3]+1) & (a < area)
    owner[inside] = hnd; area[inside] = a
dC = np.abs(np.diff(C, axis=0)); dHb = np.abs(np.diff(Hb, axis=0))[3:]; dHl = np.abs(np.diff(Hl, axis=0))[3:]
def summ(sel):
    sel = sel & ok
    if sel.sum() == 0: return None
    lit = sel & (C.mean(0) > 8)
    r = {'px': int(sel.sum()), 'lit_px': int(lit.sum()),
         'view_w_p50': float(np.median(w7[sel])), 'speed_px_p50': float(np.median(speed[sel])),
         'sun_share_mean': float(share[sel].mean()), 'coverage_change_frac': float(cov[sel].mean()),
         'C_mean': float(C[:, sel].mean())}
    for tag, s in (('all', sel), ('lit', lit), ('lit_cov', lit & cov), ('lit_nocov', lit & ~cov)):
        if s.sum() < 5: continue
        r[tag] = {'n': int(s.sum()), 'pre_dC': round(float(dC[:, s].mean()), 3), 'pre_std8': round(float(C[:, s].std(0).mean()), 3),
                  'taa_dH': round(float(dHb[:, s].mean()), 3), 'taa_lock_dH': round(float(dHl[:, s].mean()), 3),
                  'hist_reject': round(float(Rb[:, s].mean()), 3),
                  'lockmodel_dH': round(float(np.abs(np.diff(Hm, axis=0))[3:][:, s].mean()), 3), 'lockmodel_locked_last3': round(float(Lm[4:, s].mean()), 3), 'flicker_px_frac_dC_gt8': round(float((dC[:, s].mean(0) > 8).mean()), 4),
                  'flicker_px_frac_dH_gt2': round(float((dHb[:, s].mean(0) > 2).mean()), 4)}
    for lo_, hi_ in ((0, .05), (.05, .2), (.2, .5), (.5, 1.01)):
        s = lit & (share >= lo_) & (share < hi_)
        if s.sum() >= 5:
            r['share_%.2f_%.2f' % (lo_, hi_)] = {'n': int(s.sum()), 'pre_dC': round(float(dC[:, s].mean()), 3), 'taa_dH': round(float(dHb[:, s].mean()), 3), 'C_mean': round(float(C[:, s].mean()), 2)}
    for tag, s in (('sun_cov', lit & (share >= .5) & cov), ('sun_nocov', lit & (share >= .5) & ~cov),
                   ('dark_cov', lit & (share < .05) & cov), ('dark_nocov', lit & (share < .05) & ~cov)):
        if s.sum() >= 5:
            r[tag] = {'n': int(s.sum()), 'pre_dC': round(float(dC[:, s].mean()), 3), 'taa_dH': round(float(dHb[:, s].mean()), 3), 'C_mean': round(float(C[:, s].mean()), 2)}
    r['wres_p50_p90'] = [float(np.percentile(wres[sel], 50)), float(np.percentile(wres[sel], 90))]
    return r
res = {'capture': f0, 'crop': [X0, Y0, X1, Y1], 'geo_px': int(n), 'traced_px': int(ok.sum()), 'nodes': {}}
for hnd in sorted(set(owner) - {''}):
    s = summ(owner == hnd)
    if s and s['px'] >= 20:
        e = nodes[hnd]; s.update(model=e['model'], lod=e['lod'], draws=e['draws'], alphatest_draws=e['alphatest'], box=[round(v) for v in e['box']], ps=e['ps'])
        res['nodes'][hnd] = s
res['unowned'] = summ(owner == '')
json.dump(res, open(out, 'w'), indent=1)
print('capture', f0, 'geo', n, 'traced', int(ok.sum()))
for hnd, s in sorted(res['nodes'].items(), key=lambda kv: -kv[1].get('lit', {}).get('taa_dH', 0)):
    l = s.get('lit', {}); print(hnd, s['model'], 'lod', s['lod'], 'px', s['px'], 'w %.0f' % s['view_w_p50'], 'spd %.2f' % s['speed_px_p50'], 'share %.2f' % s['sun_share_mean'], 'cov %.2f' % s['coverage_change_frac'], 'at', s['alphatest_draws'], '/', s['draws'], 'lit', l)
