"""Shared numpy model of the resolve's base-pixel path (src/temporal/resolve.hlsl, region-hold program): 5-tap Catmull-Rom
history (a = -0.5, corner blocks dropped, each bilinear fetch weighed), 3x3 variance clip (gamma 1.25, intersected with the
3x3 min / max) in the weighed domain weigh(c) = c / (1 + k luma), blend lerp(current, clipped history, keep), unweigh.
Not modelled: the depth disocclusion proof, the far weight / far 7x7 clip, the thin region, the motion cap (1 on these
station pixels, camera_parallax.py), sky policy. AgX and RCAS as tools/analysis/taa_resolve_replay.py (gain exp2(-2 (1 - s)))."""
import sys, warnings, numpy as np
warnings.filterwarnings('ignore', category=RuntimeWarning)
sys.path.insert(0, '/Users/asvetl/x3-mod/tools/analysis')
import agx_reference as ar
LUMA = np.array([0.2126, 0.7152, 0.0722])
W, H = 5120, 1440
P22, P32 = 1.00000298, -6.00001812

def luma(c): return np.maximum(c[..., :3] @ LUMA, 0)
def weigh(c, k): return c / (1 + k * luma(c))[..., None]
def unweigh(c, k): return c / np.maximum(1 - k * luma(c), 1 / 65504.)[..., None]

def bilinear(img, px, py):
    """img (h, w, 3) float64; px, py texel-space coordinates where texel i has its centre at i (clamped)."""
    h, w = img.shape[:2]
    x0 = np.floor(px).astype(int); y0 = np.floor(py).astype(int); fx = px - x0; fy = py - y0
    x0c = np.clip(x0, 0, w - 1); x1c = np.clip(x0 + 1, 0, w - 1); y0c = np.clip(y0, 0, h - 1); y1c = np.clip(y0 + 1, 0, h - 1)
    a = img[y0c, x0c]; b = img[y0c, x1c]; c = img[y1c, x0c]; d = img[y1c, x1c]
    fx = fx[..., None]; fy = fy[..., None]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy

def catmull5(hist, px, py, k):
    """History at texel-space position (px, py) (centre of texel i at i): the shader's five weighed bilinear fetches."""
    bx = np.floor(px); by = np.floor(py); fx = px - bx; fy = py - by
    def wts(f):
        w0 = -f * (1 - f) ** 2 / 2; w1 = 1 - 2.5 * f ** 2 + 1.5 * f ** 3; w2 = f / 2 + 2 * f ** 2 - 1.5 * f ** 3; w3 = -f ** 2 * (1 - f) / 2
        return w0, w1 + w2, w2 / (w1 + w2), w3
    w0x, w12x, hx, w3x = wts(fx); w0y, w12y, hy, w3y = wts(fy)
    t12x, t12y = bx + hx, by + hy
    taps = [(t12x, t12y, w12x * w12y), (bx - 1, t12y, w0x * w12y), (bx + 2, t12y, w3x * w12y), (t12x, by - 1, w12x * w0y), (t12x, by + 2, w12x * w3y)]
    acc = 0; tot = 0
    for tx, ty, wt in taps:
        acc = acc + weigh(bilinear(hist, tx, ty), k) * wt[..., None]; tot = tot + wt
    return acc / tot[..., None]

def resolve(cur, hist, prevx, prevy, keep, k):
    """cur (h, w, 3) current HDR (the crop incl. a 1-px ring), hist full history image (unweighed), prevx / prevy texel-space
    previous positions of the crop's interior pixels, keep scalar or array. Returns the interior (h-2, w-2, 3) unweighed."""
    wc = weigh(cur, k)
    h, w = cur.shape[:2]
    nb = np.stack([wc[1 + dy:h - 1 + dy, 1 + dx:w - 1 + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1)])
    mean = nb.mean(0); sigma = np.sqrt(np.maximum((nb ** 2).mean(0) - mean ** 2, 0))
    lo = np.maximum(nb.min(0), mean - 1.25 * sigma); hi = np.minimum(nb.max(0), mean + 1.25 * sigma)
    old = np.clip(catmull5(hist, prevx, prevy, k), lo, hi)
    centre = wc[1:-1, 1:-1]
    kk = keep if np.isscalar(keep) else keep[..., None]
    return unweigh(centre + kk * (old - centre), k)

def agx(rgb, ev):
    v = np.maximum(np.nan_to_num(rgb), 1e-10) ** 2.2 * 2. ** ev
    v = np.clip(np.log2(np.maximum(v @ np.array(ar.M_IN).T, ar.LOG_FLOOR)), ar.MIN_EV, ar.MAX_EV)
    v = (v - ar.MIN_EV) / (ar.MAX_EV - ar.MIN_EV)
    v = sum(ck * v ** (6 - i) for i, ck in enumerate(ar.CONTRAST_COEFFICIENTS))
    return np.clip(v @ np.array(ar.M_OUT).T, 0, 1)

def rcas(t, sharpen):
    gain = 2. ** (-2 * (1 - sharpen))
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
