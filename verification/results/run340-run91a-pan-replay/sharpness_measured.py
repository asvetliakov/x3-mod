#!/usr/bin/env python3
"""Run 91 A pan replay, step 2: measured station sharpness on the capture (no replay).
Station mask: routed depth in [zlo, zhi] view units inside the box, interior = 3x3 all in the mask.
Images in the display domain: AgX(hdr_) = unresolved jittered current, AgX(taa_) = resolved before the sharpen,
present_ = after the AgX + RCAS write-back, and RCAS(AgX(taa_), gain) as the model of present_ (gain = exp2(-2 (1 - s)),
src/temporal/sharpen.h). Metric: gradient energy E = mean of squared forward differences (x and y) of display luma
(0.2126, 0.7152, 0.0722) on interior pixels, the run254 hull-sharpness ratio E_x / E_current. Also the 10-90 %
edge rise proxy: mean |grad| / sqrt(E) is not used; E only.
Usage: sharpness_measured.py <dir> <frame> x0 y0 x1 y1 zlo zhi"""
import re, subprocess, sys, os, numpy as np
sys.path.insert(0, '/Users/asvetl/x3-mod/tools/analysis')
import agx_reference as ar
W, H = 5120, 1440
P22, P32 = 1.00000298, -6.00001812
LUMA = np.array([0.2126, 0.7152, 0.0722])
d, fr = sys.argv[1], int(sys.argv[2]); x0, y0, x1, y1 = map(int, sys.argv[3:7]); zlo, zhi = map(float, sys.argv[7:9])
log = [p for p in os.listdir(d) if p.startswith('session-')][0]
row = subprocess.run(['grep', '-m1', '-E', r'^hdr_frame device=1 frame=%d ' % fr, f'{d}/{log}'], capture_output=True, text=True).stdout
EV = float(re.search(r'ev_adapted=([-\d.]+)', row).group(1))
def crop(kind, ext, dt, ch, m=2):
    a = np.memmap(f'{d}/{kind}_1_{fr}.{ext}', dt, 'r', shape=(H, W, ch))
    return np.array(a[max(y0 - m, 0):y1 + m, max(x0 - m, 0):x1 + m]).astype(np.float64)
def agx(rgb, ev):
    v = np.maximum(np.nan_to_num(rgb), 1e-10) ** 2.2 * 2. ** ev
    v = np.clip(np.log2(np.maximum(v @ np.array(ar.M_IN).T, ar.LOG_FLOOR)), ar.MIN_EV, ar.MAX_EV)
    v = (v - ar.MIN_EV) / (ar.MAX_EV - ar.MIN_EV)
    v = sum(ck * v ** (6 - i) for i, ck in enumerate(ar.CONTRAST_COEFFICIENTS))
    return np.clip(v @ np.array(ar.M_OUT).T, 0, 1)
def rcas(t, gain):
    t = np.clip(t, 0, 1); b, dd, e, f, h = t[:-2, 1:-1], t[1:-1, :-2], t[1:-1, 1:-1], t[1:-1, 2:], t[2:, 1:-1]
    lum = lambda c: .5 * c[..., 0] + c[..., 1] + .5 * c[..., 2]
    L = np.stack([lum(x) for x in (b, dd, e, f, h)]); nz = np.clip(np.abs(.25 * (L[0] + L[1] + L[3] + L[4]) - L[2]) / np.maximum(L.max(0) - L.min(0), 1 / 256.), 0, 1)
    mn = np.minimum(np.minimum(b, dd), np.minimum(f, h)); mx = np.maximum(np.maximum(b, dd), np.maximum(f, h))
    lobe = np.maximum(-mn / np.maximum(4 * mx, 1 / 4096.), (1 - mx) / np.minimum(4 * mn - 4, -1 / 4096.)).max(-1)
    lobe = (np.maximum(-.1875, np.minimum(lobe, 0)) * gain * (1 - .5 * nz))[..., None]
    out = np.clip(((b + dd + f + h) * lobe + e) / (4 * lobe + 1), np.minimum(mn, e), np.maximum(mx, e))
    return np.pad(out, ((1, 1), (1, 1), (0, 0)), mode='edge')
def energy(img, mask):
    l = img[..., :3] @ LUMA
    gx = np.zeros_like(l); gy = np.zeros_like(l); gx[:, :-1] = l[:, 1:] - l[:, :-1]; gy[:-1] = l[1:] - l[:-1]
    return float(((gx ** 2 + gy ** 2)[mask]).mean())
dep = crop('depth', 'rgba32f', np.float32, 4)[..., 0]
with np.errstate(divide='ignore', invalid='ignore'):
    vz = np.where(dep >= 0, P32 / (dep - P22), 0)
s = (vz >= zlo) & (vz <= zhi)
inter = s.copy()
for dy in (-1, 0, 1):
    for dx in (-1, 0, 1): inter &= np.roll(np.roll(s, dy, 0), dx, 1)
hdr = crop('hdr', 'rgba16f', np.float16, 4); taa = crop('taa', 'rgba16f', np.float16, 4)
pres = crop('present', 'bgra8', np.uint8, 4)[..., [2, 1, 0, 3]] / 255.
A_cur, A_taa = agx(hdr[..., :3], EV), agx(taa[..., :3], EV)
E0 = energy(A_cur, inter)
res = {'taa': energy(A_taa, inter), 'present': energy(pres, inter)}
for sh in (0.75, 1.0): res['rcas%.2f' % sh] = energy(rcas(A_taa, 2 ** (-2 * (1 - sh))), inter)
model_err = np.abs((rcas(A_taa, 2 ** -0.5)[..., :3] - pres[..., :3]) @ LUMA)[inter] * 255
print(f'frame {fr} box {x0},{y0},{x1},{y1} z {zlo:.0f}-{zhi:.0f} interior px {int(inter.sum())} ev {EV}: E_cur {E0:.3e}  ' +
      '  '.join(f'{k}/cur {v / E0:.3f}' for k, v in res.items()) + f'  | present vs RCAS0.75(AgX(taa)) luma err codes p50 {np.median(model_err):.2f} p90 {np.percentile(model_err, 90):.2f}')
