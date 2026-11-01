#!/usr/bin/env python3
"""Per-region TAA hold state and TAA-output flicker of an 8-frame taa_debug F8 capture.

Usage: readback_hold.py CAPDIR DRAWS_JSON FIRST_FRAME OUT.json
DRAWS_JSON is ../extract_draws.py's output. Reads crops / single channels through numpy memmaps; writes no capture bytes.

Decoding (src/temporal/resolve.hlsl lines 155-222, 417-429; motion_output.cpp 2567-2586):
  taa_lock bgra8, byte 0 (the lane's b) = t + 32 class + 64 sign + 128 mode; t = b & 31 the lock lifetime, locked = t > 0,
    strength l = saturate(t / 4); mode 1 = motion mode (own screen gate closed).
  taa_age r32f: count = floor(|age|) (1..64), sign negative = exit mark; code = frac(|age|) * 65536 = h + 128 pair;
    h = code mod 128 the region (thin) hold, in the thin region while h > 0, flagged this frame where h == L (8);
    pair = code // 128 = q (L + 1) + t the camera-gate closure hold (q closed quarters, t frames left).
Pixels: geometry (motion .a == 1, depth .r >= 0) owned by the smallest node box containing them (object_bounds of that frame).
Flicker per frame pair k-1 -> k, per pixel of frame k: |Q_k(x) - Q_{k-1}(prev_k(x))|, prev = motion RT1 .xy (bilinear), with
  Q = 255 L / (1 + L), L = luma * 2^ev (hdr_frame ev) for hdr (pre-TAA) and taa (TAA output); present = luma of bgra8 codes.
  'same' = the unwarped same-pixel difference."""
import json, sys, numpy as np
W, H = 5120, 1440
cap, dj, f0, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
D = json.load(open(dj)); frames = [f0 + i for i in range(8)]
L_HOLD = 8
FARM = ('0000556e', '0000556f', '00005570')
LUMA = np.array([0.2126, 0.7152, 0.0722], np.float32)
def mm(kind, f, ext, dt, ch):
    return np.memmap('%s/%s_1_%d.%s' % (cap, kind, f, ext), dtype=dt, mode='r', shape=(H, W, ch) if ch > 1 else (H, W))

def nodes_of(f):
    N = {}
    for d in D['draws']:
        if d['frame'] == f and 'box' in d and d.get('node_handle'):
            b = d['box']; e = N.setdefault(d['node_handle'], {'box': [1e9, 1e9, -1e9, -1e9], 'model': d.get('model'), 'lod': d.get('lod'), 'draws': 0, 'alphatest': 0})
            e['box'] = [min(e['box'][0], b[0]), min(e['box'][1], b[1]), max(e['box'][2], b[2]), max(e['box'][3], b[3])]
            e['draws'] += 1; e['alphatest'] += d['st'].get('alphatest', 0)
    return N

N0 = nodes_of(frames[0])
farm_nodes = sorted(h for h, e in N0.items() if e['model'] in FARM)
station_nodes = sorted(h for h, e in N0.items() if e['model'] in ('000054ca', '000054cb'))
m57df_nodes = sorted(h for h, e in N0.items() if e['model'] == '000057df')
roi_nodes = farm_nodes + station_nodes + m57df_nodes
bx = np.array([N0[h]['box'] for h in roi_nodes])
X0, Y0 = int(max(bx[:, 0].min() - 40, 0)), int(max(bx[:, 1].min() - 40, 0))
X1, Y1 = int(min(bx[:, 2].max() + 40, W)), int(min(bx[:, 3].max() + 40, H))
h, w = Y1 - Y0, X1 - X0

def owner_map(f):
    N = nodes_of(f); own = np.full((h, w), '', object); area = np.full((h, w), np.inf)
    gy, gx = np.mgrid[Y0:Y1, X0:X1]
    for hnd, e in N.items():
        b = e['box']; a = (b[2]-b[0])*(b[3]-b[1])
        if a <= 0 or a > 0.5*W*H: continue
        if b[2] < X0 or b[0] > X1 or b[3] < Y0 or b[1] > Y1: continue
        ins = (gx >= b[0]-1) & (gx <= b[2]+1) & (gy >= b[1]-1) & (gy <= b[3]+1) & (a < area)
        own[ins] = hnd; area[ins] = a
    return own, N

def decode(f, crop):
    sl = (slice(Y0, Y1), slice(X0, X1)) if crop else (slice(None), slice(None))
    b = np.asarray(mm('taa_lock', f, 'bgra8', 'u1', 4)[sl + (0,)])
    a = np.asarray(mm('taa_age', f, 'r32f', '<f4', 1)[sl], dtype=np.float64)
    m = np.asarray(mm('motion', f, 'rgba32f', '<f4', 4)[sl], dtype=np.float32)
    dep = np.asarray(mm('depth', f, 'rgba32f', '<f4', 4)[sl + (0,)])
    t = (b & 31).astype(np.int32); mode = (b >> 7) & 1
    aa = np.abs(a); cnt = np.floor(aa); code = np.rint((aa - cnt) * 65536).astype(np.int64)
    hold = code % 128; pair = code // 128
    geo = (m[..., 3] == 1) & (dep >= 0)
    return dict(t=t, mode=mode, cnt=cnt, neg=a < 0, hold=hold, pair=pair, geo=geo, mot=m)

def qmap(kind, f, ev, sl):
    if kind == 'present':
        c = np.asarray(mm('present', f, 'bgra8', 'u1', 4)[sl], dtype=np.float32)
        return c[..., 2]*0.2126 + c[..., 1]*0.7152 + c[..., 0]*0.0722
    c = np.asarray(mm(kind, f, 'rgba16f', '<f2', 4)[sl][..., :3], dtype=np.float32)
    L = np.maximum(c @ LUMA, 0) * 2**ev
    return 255*L/(1+L)

def bil(a, x, y):
    hh, ww = a.shape
    ok = (x >= 0) & (y >= 0) & (x <= ww - 1.001) & (y <= hh - 1.001)
    x = np.clip(x, 0, ww-1.001); y = np.clip(y, 0, hh-1.001); xi = x.astype(int); yi = y.astype(int); fx = x-xi; fy = y-yi
    return a[yi, xi]*(1-fx)*(1-fy) + a[yi, xi+1]*fx*(1-fy) + a[yi+1, xi]*(1-fx)*fy + a[yi+1, xi+1]*fx*fy, ok

def cubic(a, x, y):
    # Catmull-Rom 4x4 sample: the interpolation floor of the warped difference is |bil - cubic| at the same positions.
    hh, ww = a.shape
    xi = np.floor(x).astype(int); yi = np.floor(y).astype(int); fx = x - xi; fy = y - yi
    def wts(t):
        return [((-t + 2)*t - 1)*t/2, ((3*t - 5)*t*t + 2)/2, ((-3*t + 4)*t + 1)*t/2, (t - 1)*t*t/2]
    wx, wy = wts(fx), wts(fy); acc = 0
    for j in range(4):
        yy = np.clip(yi + j - 1, 0, hh - 1)
        for i in range(4):
            acc = acc + a[yy, np.clip(xi + i - 1, 0, ww - 1)]*wx[i]*wy[j]
    return acc

def hold_stats(dc, sel):
    n = int(sel.sum())
    if n == 0: return None
    c = dc['cnt'][sel]
    return {'px': n, 'lock_frac': round(float((dc['t'][sel] > 0).mean()), 4),
            'lock_full_frac': round(float((dc['t'][sel] >= 4).mean()), 4),
            'lock_motion_mode_frac': round(float(dc['mode'][sel].mean()), 4),
            'thin_region_frac': round(float((dc['hold'][sel] > 0).mean()), 4),
            'thin_flagged_now_frac': round(float((dc['hold'][sel] == L_HOLD).mean()), 4),
            'closure_held_frac': round(float((dc['pair'][sel] % (L_HOLD + 1) > 0).mean()), 4),
            'age_p50': float(np.median(c)), 'age_p10': float(np.percentile(c, 10)),
            'age_le1_frac': round(float((c <= 1).mean()), 4), 'age_ge32_frac': round(float((c >= 32).mean()), 4),
            'age_exit_neg_frac': round(float(dc['neg'][sel].mean()), 4)}

sl = (slice(Y0, Y1), slice(X0, X1))
regions = ['farm_all', 'farm_alphatest', 'farm_nonalphatest', 'station_54ca_54cb', 'model_57df', 'roi_other']
res = {'capture': f0, 'crop': [X0, Y0, X1, Y1], 'farm_nodes': {h_: {k: N0[h_][k] for k in ('model', 'lod', 'draws', 'alphatest')} | {'box': [round(v) for v in N0[h_]['box']]} for h_ in farm_nodes},
       'station_nodes': station_nodes, 'model_57df_nodes': m57df_nodes, 'per_frame': [], 'pairs': []}
prevQ = None; prev_frame_lods = None
for k, f in enumerate(frames):
    ev = float(D['frame_rows'][str(f)]['hdr_frame']['ev'])
    own, N = owner_map(f)
    dc = decode(f, True)
    farm_px = np.isin(own, [x for x in farm_nodes])
    at_nodes = [x for x in farm_nodes if N.get(x, {}).get('alphatest', 0) > 0]
    masks = {'farm_all': farm_px, 'farm_alphatest': np.isin(own, at_nodes), 'farm_nonalphatest': farm_px & ~np.isin(own, at_nodes),
             'station_54ca_54cb': np.isin(own, station_nodes), 'model_57df': np.isin(own, m57df_nodes)}
    masks['roi_other'] = (own != '') & ~np.isin(own, roi_nodes)
    for x in farm_nodes: masks['node_' + x] = own == x
    masks = {r: m & dc['geo'] for r, m in masks.items()}
    row = {'frame': f, 'ev': ev, 'farm_lods': {x: N.get(x, {}).get('lod') for x in farm_nodes},
           'farm_alphatest_nodes': at_nodes, 'regions': {r: hold_stats(dc, m) for r, m in masks.items()}}
    full = decode(f, False)
    row['regions']['whole_frame_geo'] = hold_stats(full, full['geo']); del full
    res['per_frame'].append(row)
    Q = {kind: qmap(kind, f, ev, sl) for kind in ('hdr', 'taa', 'present')}
    if prevQ is not None:
        m = dc['mot']; ys, xs = np.mgrid[0:h, 0:w]
        mf = D['frame_rows'][str(f)]['motion_output_frame']
        jx, jy = float(mf['jitter_x']), float(mf['jitter_y']); jpx, jpy = float(mf['jitter_previous_x']), float(mf['jitter_previous_y'])
        px_ = m[..., 0]*W - 0.5 - X0; py_ = m[..., 1]*H - 0.5 - Y0  # q: the previous UNJITTERED position (resolve.hlsl 20-28)
        # The previous position of the content per image: taa (unjittered grid, the resolve's history read) q + current
        # jitter; hdr / present (jittered raster) q + previous jitter; 'hdr_nojit' = q alone (temporal_variance.py's form).
        POS = {'taa': (px_ + jx, py_ + jy), 'hdr': (px_ + jpx, py_ + jpy), 'present': (px_ + jx, py_ + jy), 'hdr_nojit': (px_, py_)}
        speed = np.hypot(xs - px_, ys - py_)
        pr = {'pair': [frames[k-1], f], 'regions': {}}
        for r, msk in masks.items():
            if msk.sum() < 20: continue
            e = {'px': int(msk.sum()), 'speed_px_p50': round(float(np.median(speed[msk])), 3)}
            for kind in ('hdr', 'taa', 'present', 'hdr_nojit'):
                src = 'hdr' if kind == 'hdr_nojit' else kind; qx, qy = POS[kind]
                v, okb = bil(prevQ[src], qx[msk], qy[msk]); d = np.abs(Q[src][msk] - v)[okb]
                ds = np.abs(Q[src][msk] - prevQ[src][msk])
                vc = cubic(prevQ[src], qx[msk], qy[msk]); dcub = np.abs(Q[src][msk] - vc)[okb]; fl = np.abs(v - vc)[okb]
                e[kind] = {'dQ_mean': round(float(d.mean()), 3), 'dQ_p90': round(float(np.percentile(d, 90)), 3),
                           'gt4_frac': round(float((d > 4).mean()), 4), 'dQ_cubic_mean': round(float(dcub.mean()), 3),
                           'interp_floor_mean': round(float(fl.mean()), 3), 'same_px_dQ_mean': round(float(ds.mean()), 3),
                           'Q_mean': round(float(Q[src][msk].mean()), 2)}
            # hold state of the pixels at this frame, split: TAA flicker of locked / in-region / neither
            tt, hh_, md = dc['t'][msk], dc['hold'][msk], dc['mode'][msk]
            for tag, s in (('locked', tt > 0), ('lock_full', tt >= 4), ('thin_region', hh_ > 0),
                           ('locked_and_thin', (tt > 0) & (hh_ > 0)), ('locked_motion_mode', (tt > 0) & (md == 1)),
                           ('locked_rest_mode', (tt > 0) & (md == 0)), ('neither', (tt <= 0) & (hh_ == 0))):
                if s.sum() >= 10:
                    v, okb = bil(prevQ['taa'], POS['taa'][0][msk][s], POS['taa'][1][msk][s]); d = np.abs(Q['taa'][msk][s] - v)[okb]
                    vh, okh = bil(prevQ['hdr'], POS['hdr'][0][msk][s], POS['hdr'][1][msk][s]); dh = np.abs(Q['hdr'][msk][s] - vh)[okh]
                    vc = cubic(prevQ['taa'], POS['taa'][0][msk][s], POS['taa'][1][msk][s])
                    e['by_' + tag] = {'px': int(s.sum()), 'taa_dQ_mean': round(float(d.mean()), 3), 'hdr_dQ_mean': round(float(dh.mean()), 3),
                                      'taa_interp_floor': round(float(np.abs(v - vc)[okb].mean()), 3),
                                      'taa_dQ_cubic': round(float(np.abs(Q['taa'][msk][s] - vc)[okb].mean()), 3),
                                      'taa_gt4_frac': round(float((d > 4).mean()), 4)}
            pr['regions'][r] = e
        res['pairs'].append(pr)
    prevQ = Q
json.dump(res, open(out, 'w'), indent=1)
def fmt(s): return 'n/a' if s is None else 'px %d lock %.3f thin %.3f flag %.3f age50 %g le1 %.3f' % (s['px'], s['lock_frac'], s['thin_region_frac'], s['thin_flagged_now_frac'], s['age_p50'], s['age_le1_frac'])
for pr in res['pairs']:
    print(pr['pair'][1], {r: (e['taa']['dQ_mean'], e['taa']['dQ_cubic_mean'], e['taa']['interp_floor_mean'], e['hdr']['dQ_mean'], e['hdr_nojit']['dQ_mean'], e['present']['dQ_mean']) for r, e in pr['regions'].items() if r in ('farm_alphatest', 'station_54ca_54cb')})
print('capture', f0, 'crop', res['crop'], 'farm', farm_nodes)
for row in res['per_frame']:
    print(row['frame'], {r: fmt(s) for r, s in row['regions'].items() if r in ('farm_alphatest', 'farm_nonalphatest', 'station_54ca_54cb', 'whole_frame_geo')})
