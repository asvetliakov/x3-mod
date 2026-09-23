"""Run 268: frame-to-frame change of one node on consecutive F8 frames (pre-resolve jittered HDR target), in AgX display luma.
Per pair (a, a+1): node mask of each frame = union of its object_bounds rows (inside>0, +2 px) and device z in the node's [zmin, zmax];
frame b is shifted by the integer (dx, dy) in +-3 px that maximises luma correlation over frame a's box; on the mask intersection
prints mean display luma, mean |d|, low-pass (gauss 3 px) mean |d|, and the same with the 2 px edge band removed.
Also prints the 8-frame temporal std per pixel (after per-frame shift to the first frame), which approximates what a
static-scene TAA history averages. Display: agx as pair_diff268.py.
usage: frame_noise.py CAPDIR LOG NODE FIRST_FRAME COUNT"""
import sys, re, os
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../../tools/analysis'))
import agx_reference as ar
cap, log, node, f0, n = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5])
frames = [str(f0 + i) for i in range(n)]; kv = re.compile(r'(\w+)=(\S+)')
P = {}; B = {f: [] for f in frames}; EV = {}
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith(('sun_shadow_apply_params ', 'hdr_frame ')) or (line.startswith('object_bounds ') and f'node={node}' in line and ' inside=0' not in line):
            m = re.search(r' frame=(\d+) ', line)
            if not m or m.group(1) not in B: continue
            d = dict(kv.findall(line))
            if line.startswith('sun_'): P[m.group(1)] = d
            elif line.startswith('hdr_'): EV[m.group(1)] = float(d['ev'])
            else: B[m.group(1)].append(d)
def gauss(a, sg):
    r = int(3 * sg); k = np.exp(-0.5 * (np.arange(-r, r + 1) / sg) ** 2); k /= k.sum(); a = np.pad(a, r, mode='edge')
    a = np.apply_along_axis(lambda v: np.convolve(v, k, 'valid'), 0, a); return np.apply_along_axis(lambda v: np.convolve(v, k, 'valid'), 1, a)
def edge(m, w=2):
    e = m.copy()
    for _ in range(w): e = e & np.roll(e, 1, 0) & np.roll(e, -1, 0) & np.roll(e, 1, 1) & np.roll(e, -1, 1)
    return m & ~e
def load(f):
    p = P[f]; W, H = int(p['width']), int(p['height'])
    rt = np.fromfile(f'{cap}/depth_1_{f}.rgba32f', dtype='<f4').reshape(H, W, 4)
    hdr = np.fromfile(f'{cap}/hdr_1_{f}.rgba16f', dtype='<f2').reshape(H, W, 4).astype(np.float64)
    x0 = int(min(float(b['sx0']) for b in B[f])) - 2; x1 = int(max(float(b['sx1']) for b in B[f])) + 3
    y0 = int(min(float(b['sy0']) for b in B[f])) - 2; y1 = int(max(float(b['sy1']) for b in B[f])) + 3
    zmin = min(float(b['zmin']) for b in B[f]); zmax = max(float(b['zmax']) for b in B[f])
    box = np.zeros((H, W), bool); box[max(y0, 0):y1, max(x0, 0):x1] = True
    mask = box & (rt[..., 0] >= zmin - 2e-6) & (rt[..., 0] <= zmax + 2e-6)
    with np.errstate(all='ignore'):
        v = np.maximum(np.nan_to_num(hdr[..., :3]), 1e-10) ** 2.2 * 2. ** EV[f]
        v = np.clip(np.log2(np.maximum(v @ np.array(ar.M_IN).T, ar.LOG_FLOOR)), ar.MIN_EV, ar.MAX_EV)
        v = (v - ar.MIN_EV) / (ar.MAX_EV - ar.MIN_EV); v = sum(ck * v ** (6 - i) for i, ck in enumerate(ar.CONTRAST_COEFFICIENTS))
        disp = np.clip(v @ np.array(ar.M_OUT).T, 0, 1)
    return dict(dl=0.2126 * disp[..., 0] + 0.7152 * disp[..., 1] + 0.0722 * disp[..., 2], mask=mask, box=(max(y0, 0), y1, max(x0, 0), x1), jit=(p['jitter_x'], p['jitter_y']))
X = {f: load(f) for f in frames}
def best_shift(a, b):
    y0, y1, x0, x1 = a['box']; ca = a['dl'][y0:y1, x0:x1]; best = None
    for dy in range(-3, 4):
        for dx in range(-3, 4):
            cb = np.roll(np.roll(b['dl'], dy, 0), dx, 1)[y0:y1, x0:x1]
            c = np.corrcoef(ca.ravel(), cb.ravel())[0, 1]
            if best is None or c > best[0]: best = (c, dy, dx)
    return best
rows = []; stack = []; masks = []
for i, f in enumerate(frames):
    a = X[frames[0]] if i else None
    c, dy, dx = best_shift(X[frames[0]], X[f]) if i else (1., 0, 0)
    stack.append(np.roll(np.roll(X[f]['dl'], dy, 0), dx, 1)); masks.append(np.roll(np.roll(X[f]['mask'], dy, 0), dx, 1))
for fa, fb in zip(frames, frames[1:]):
    a, b = X[fa], X[fb]; c, dy, dx = best_shift(a, b)
    bl = np.roll(np.roll(b['dl'], dy, 0), dx, 1); bm = np.roll(np.roll(b['mask'], dy, 0), dx, 1)
    I = a['mask'] & bm; band = (edge(a['mask']) | edge(bm)) & I; nb = I & ~band
    d = a['dl'] - bl; low = gauss(np.where(I, d, 0.), 3.) / np.maximum(gauss(I.astype(float), 3.), 1e-6)
    rows.append((np.abs(d[I]).mean(), np.abs(d[nb]).mean(), np.abs(low[I]).mean()))
    print(f"{fa}->{fb} jitter {a['jit']}->{b['jit']} shift ({dx},{dy}) corr={c:.3f} px={int(I.sum())} mean C={a['dl'][I].mean():.4f} mean d={d[I].mean():+.4f} "
          f"mean|d|={np.abs(d[I]).mean():.4f} off-band={np.abs(d[nb]).mean():.4f} low-pass(3px)={np.abs(low[I]).mean():.4f}")
r = np.array(rows); print(f"pairs={len(rows)} median mean|d|={np.median(r[:, 0]):.4f} off-band={np.median(r[:, 1]):.4f} low-pass={np.median(r[:, 2]):.4f}")
S = np.array(stack); M = np.logical_and.reduce(masks)
print(f"8-frame per-pixel temporal std on common mask px={int(M.sum())}: mean={S.std(0)[M].mean():.4f}; std of 8-frame mean vs single frame mean|d|={np.abs(S[0] - S.mean(0))[M].mean():.4f}")
