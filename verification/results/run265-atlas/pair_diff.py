"""Run 265 (Run 70 C): per-pixel coarse/fine difference of one node between two F8 frames.
Per frame: node mask = union box of the node's object_bounds rows (+2 px) and device z (depth .r) in the node's
[zmin, zmax]. The fine frame's object_bounds union box is resampled bilinearly onto the coarse one, scaled by t in 0.85..1.15 and
shifted (+-4 px) for the best luminance correlation over the whole box. I = both masks and same view depth (|wC - k wF| < 2 % of wC, k = median depth ratio), which
drops other objects that moved and mismatched silhouettes (counted in the xor). On the mask intersection I it prints:
  ratio   mean lum coarse/fine, median of per-pixel ratio; contrast (std) and fine detail (std of lum - gauss(2 px)).
  diff    d = lumC - lumF: mean |d|; share of sum d^2 in the low-pass part (gauss 3 px of d: patches) vs the rest
          (speckle/lines); silhouette IoU and share of sum|d| within 2 px of either mask edge.
  classes emissive (non-sun part lum*(1-s) > EMIS in either), highlight (sun part > p95 of fine sun part in either),
          silhouette band, plating (rest): pixel share, share of sum|d|, mean d, mean lum ratio.
  spec    view-space normals from RT2 view depth (as outpost_pair.py), H = normalize(L + V):
          mean lum where n.H > 0.95 vs 0.6 < n.H < 0.85 (sun-facing only), per frame on its own mask.
usage: pair_diff.py CAPDIR LOG NODE COARSE_FRAME FINE_FRAME [PNG_OUT]"""
import sys, re
import numpy as np
from PIL import Image
cap, log, node, fc, ff = sys.argv[1:6]; png = sys.argv[6] if len(sys.argv) > 6 else None
EMIS = 0.06
kv = re.compile(r'(\w+)=(\S+)'); P = {fc: None, ff: None}; B = {fc: [], ff: []}
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('sun_shadow_apply_params ') or (line.startswith('object_bounds ') and f'node={node}' in line):
            m = re.search(r' frame=(\d+) ', line)
            if not m or m.group(1) not in P: continue
            d = dict(kv.findall(line))
            if line.startswith('sun_'): P[m.group(1)] = d
            else: B[m.group(1)].append(d)
def gauss(a, sg):
    r = int(3 * sg); k = np.exp(-0.5 * (np.arange(-r, r + 1) / sg) ** 2); k /= k.sum()
    a = np.pad(a, r, mode='edge')
    a = np.apply_along_axis(lambda v: np.convolve(v, k, 'valid'), 0, a)
    return np.apply_along_axis(lambda v: np.convolve(v, k, 'valid'), 1, a)
def edge(m, w=2):
    e = m.copy()
    for _ in range(w):
        e = e & np.roll(e, 1, 0) & np.roll(e, -1, 0) & np.roll(e, 1, 1) & np.roll(e, -1, 1)
    return m & ~e
def load(f):
    p = P[f]; W, H = int(p['width']), int(p['height']); m00, m11 = float(p['m00']), float(p['m11'])
    rt = np.fromfile(f'{cap}/depth_1_{f}.rgba32f', dtype='<f4').reshape(H, W, 4).astype(np.float64)
    hdr = np.fromfile(f'{cap}/hdr_1_{f}.rgba16f', dtype='<f2').reshape(H, W, 4).astype(np.float64)
    x0 = int(min(float(b['sx0']) for b in B[f])) - 2; x1 = int(max(float(b['sx1']) for b in B[f])) + 3
    y0 = int(min(float(b['sy0']) for b in B[f])) - 2; y1 = int(max(float(b['sy1']) for b in B[f])) + 3
    zmin = min(float(b['zmin']) for b in B[f]); zmax = max(float(b['zmax']) for b in B[f])
    box = np.zeros((H, W), bool); box[max(y0, 0):y1, max(x0, 0):x1] = True
    mask = box & (rt[..., 0] >= zmin - 2e-6) & (rt[..., 0] <= zmax + 2e-6)
    lum = 0.2126 * hdr[..., 0] + 0.7152 * hdr[..., 1] + 0.0722 * hdr[..., 2]
    s = rt[..., 1]
    ys, xs = np.mgrid[0:H, 0:W]; w = rt[..., 2]
    V = np.stack([((xs + 0.5) / W * 2 - 1) * w / m00, (1 - (ys + 0.5) / H * 2) * w / m11, w], -1)
    dx = V[:, 2:, :] - V[:, :-2, :]; dy = V[2:, :, :] - V[:-2, :, :]
    n = np.zeros_like(V); n[1:-1, 1:-1] = np.cross(dx[1:-1], dy[:, 1:-1])
    n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-30); n[n[..., 2] > 0] *= -1
    r2 = np.array([float(v) for v in p['rows4'].split(',')]).reshape(3, 4)[2, :3]; L = -r2 / np.linalg.norm(r2)
    Vd = -V / np.maximum(np.linalg.norm(V, axis=-1, keepdims=True), 1e-30)
    Hh = L + Vd; Hh /= np.maximum(np.linalg.norm(Hh, axis=-1, keepdims=True), 1e-30)
    with np.errstate(all='ignore'):
        nl = np.nan_to_num(n @ L); nh = np.nan_to_num(np.einsum('ijk,ijk->ij', n, Hh))
    same = np.zeros((H, W), bool); same[1:-1, 1:-1] = mask[1:-1, :-2] & mask[1:-1, 2:] & mask[:-2, 1:-1] & mask[2:, 1:-1]
    return dict(lum=lum, s=s, mask=mask, nl=nl, nh=nh, same=same, w=w, box=(max(y0, 0), y1, max(x0, 0), x1))
C, F = load(fc), load(ff)
for f, X in ((fc, C), (ff, F)):
    sel = X['same'] & (X['nl'] > 0.2); hi = sel & (X['nh'] > 0.95); mid = sel & (X['nh'] > 0.6) & (X['nh'] < 0.85)
    print(f"spec frame {f}: mask px={int(X['mask'].sum())} sun-facing px={int(sel.sum())} n.H>0.95 px={int(hi.sum())} lum={X['lum'][hi].mean() if hi.any() else float('nan'):.3f} "
          f"| 0.6<n.H<0.85 px={int(mid.sum())} lum={X['lum'][mid].mean():.3f} | ratio={X['lum'][hi].mean() / X['lum'][mid].mean() if hi.any() else float('nan'):.2f} "
          f"| sun-facing lum p50/p95/p99={np.percentile(X['lum'][sel], 50):.3f}/{np.percentile(X['lum'][sel], 95):.3f}/{np.percentile(X['lum'][sel], 99):.3f}")
cy0, cy1, cx0, cx1 = C['box']; fy0, fy1, fx0, fx1 = F['box']
hC, wC = cy1 - cy0, cx1 - cx0
def rs(a, t, nearest=False):
    # fine box resampled to t x the coarse box size, centred on the coarse box (crop or zero pad)
    h2, w2 = max(int(round(hC * t)), 2), max(int(round(wC * t)), 2)
    im = Image.fromarray(a[fy0:fy1, fx0:fx1].astype(np.float32))
    r = np.asarray(im.resize((w2, h2), Image.NEAREST if nearest else Image.BILINEAR)).astype(np.float64)
    out = np.zeros((hC, wC)); oy, ox = (hC - h2) // 2, (wC - w2) // 2
    sy0, sx0 = max(-oy, 0), max(-ox, 0); dy0, dx0 = max(oy, 0), max(ox, 0)
    hh, ww = min(h2 - sy0, hC - dy0), min(w2 - sx0, wC - dx0)
    out[dy0:dy0 + hh, dx0:dx0 + ww] = r[sy0:sy0 + hh, sx0:sx0 + ww]; return out
cl, cs, cm, cw = (C[k][cy0:cy1, cx0:cx1] for k in ('lum', 's', 'mask', 'w'))
best = None
for t in np.arange(0.85, 1.155, 0.01):
    fl0 = rs(F['lum'], t)
    for dy in range(-4, 5):
        for dx in range(-4, 5):
            l2 = np.roll(np.roll(fl0, dy, 0), dx, 1)
            c = np.corrcoef(cl[4:-4, 4:-4].ravel(), l2[4:-4, 4:-4].ravel())[0, 1]
            if best is None or c > best[0]: best = (c, dy, dx, t)
c, dy, dx, t = best
fl_, fs_, fw_ = rs(F['lum'], t), rs(F['s'], t), rs(F['w'], t); fm_ = rs(F['mask'].astype(np.float32), t, True) > 0.5
fl_, fs_, fm_, fw_ = (np.roll(np.roll(a, dy, 0), dx, 1) for a in (fl_, fs_, fm_, fw_))
O = cm & fm_; k = np.median(cw[O] / np.maximum(fw_[O], 1e-9))
U = cm | fm_; I = O & (np.abs(cw - k * fw_) < 0.02 * np.abs(cw))
cI = np.corrcoef(cl[I], fl_[I])[0, 1]
print(f"align: corr on I={cI:.3f}; coarse box {wC}x{hC} at ({cx0},{cy0}); fine bbox {fx1 - fx0}x{fy1 - fy0} at ({fx0},{fy0}) box scale {wC / (fx1 - fx0):.3f}x{hC / (fy1 - fy0):.3f} x t={t:.2f}; shift ({dx},{dy}); depth ratio k={k:.3f}; lum corr={c:.3f}")
d = cl - fl_; dz = np.where(I, d, 0.0)
low = gauss(dz, 3.0) / np.maximum(gauss(I.astype(float), 3.0), 1e-6) * I
e_tot = (dz[I] ** 2).sum(); e_low = (low[I] ** 2).sum(); e_res = ((dz - low)[I] ** 2).sum()
band = (edge(cm) | edge(fm_)) & I
print(f"ratio: mean lum coarse/fine={cl[I].mean() / fl_[I].mean():.3f} (coarse {cl[I].mean():.4f}, fine {fl_[I].mean():.4f}); "
      f"median px ratio={np.median(cl[I] / np.maximum(fl_[I], 1e-4)):.3f}; std coarse/fine={cl[I].std():.4f}/{fl_[I].std():.4f}; "
      f"detail(std lum-gauss2) coarse/fine={(cl - gauss(cl, 2))[I].std():.4f}/{(fl_ - gauss(fl_, 2))[I].std():.4f}")
print(f"diff: px={int(I.sum())} mean|d|={np.abs(d[I]).mean():.4f} mean d={d[I].mean():+.4f}; energy low-pass(3px)={e_low / e_tot:.2f} residual={e_res / e_tot:.2f}; "
      f"silhouette IoU={(cm & fm_).sum() / U.sum():.3f} xor px={int((U & ~(cm & fm_)).sum())} same-depth share of overlap={I.sum() / max((cm & fm_).sum(), 1):.2f}; |d| share in 2px edge band={np.abs(d[band]).sum() / np.abs(d[I]).sum():.2f} (band px share {band.sum() / I.sum():.2f})")
remC, remF = cl * (1 - cs), fl_ * (1 - fs_); sunC, sunF = cl * cs, fl_ * fs_
p95 = np.percentile(sunF[I], 95)
emis = I & ((remC > EMIS) | (remF > EMIS)) & ~band
high = I & ((sunC > p95) | (sunF > p95)) & ~band & ~emis
plat = I & ~band & ~emis & ~high
tot = np.abs(d[I]).sum()
for name, k in (('emissive', emis), ('highlight', high), ('edge band', band), ('plating', plat)):
    if not k.any(): print(f"  {name:10s} none"); continue
    print(f"  {name:10s} px share={k.sum() / I.sum():.3f} |d| share={np.abs(d[k]).sum() / tot:.3f} mean d={d[k].mean():+.4f} "
          f"lum C/F={cl[k].mean():.4f}/{fl_[k].mean():.4f} ratio={cl[k].mean() / max(fl_[k].mean(), 1e-9):.3f} nonsun C/F={remC[k].mean():.4f}/{remF[k].mean():.4f}")
print(f"  thresholds: emissive nonsun>{EMIS}; highlight sun part>{p95:.3f} (p95 fine)")
if png:
    sc = 4; g = lambda a: np.clip(a / max(np.percentile(fl_[I], 99), 1e-6) * 255, 0, 255)
    dd = np.clip(128 + d / max(np.percentile(np.abs(d[I]), 99), 1e-6) * 127, 0, 255) * I + 64 * ~I
    row = np.concatenate([g(fl_), g(cl), dd], 1).astype(np.uint8)
    Image.fromarray(row).resize((row.shape[1] * sc, row.shape[0] * sc), Image.NEAREST).save(png)
