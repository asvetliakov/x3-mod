#!/usr/bin/env python3
"""fire_material_ref_stats.py <png> [<png> ...]: per frame (sRGB, clamped; the reference frames and our stage dumps), the
luminance and shape statistics docs/architecture/fire-material.md section 6 gates on, measured by the 10 %-of-peak method
of measure.py (lit = luma > 0.1 peak): share of lit pixels above 0.8 / 0.5 of the peak, the dark-gap share inside the lit
set's convex-ish outline (the filled 4-connected outline: holes over lit + holes), the ratio of radial to tangential
gradient energy about the lit centroid (anisotropy: > 1 radial structure, 1 isotropic), the hue classes of the lit pixels
on the heat ramp (white-yellow: min(R,G,B)/max >= 0.6 and G/R >= 0.7; orange: G/R 0.35-0.7; red: G/R < 0.35) and the
mean normalised radius of each class, and the edge thinness (the share of lit pixels within 2 px of an unlit one)."""
import sys, numpy as np
from PIL import Image

def fill_holes(m):
    # background reachable from the border (4-connected flood on a padded frame); holes = ~m & ~reachable
    pad = np.zeros((m.shape[0] + 2, m.shape[1] + 2), bool); pad[1:-1, 1:-1] = m
    reach = np.zeros_like(pad); reach[0, :] = reach[-1, :] = reach[:, 0] = reach[:, -1] = True
    free = ~pad
    while True:
        grow = reach.copy()
        grow[1:, :] |= reach[:-1, :]; grow[:-1, :] |= reach[1:, :]; grow[:, 1:] |= reach[:, :-1]; grow[:, :-1] |= reach[:, 1:]
        grow &= free; grow |= reach
        if (grow == reach).all(): break
        reach = grow
    return pad[1:-1, 1:-1] | ~(reach[1:-1, 1:-1] | pad[1:-1, 1:-1])

def erode(m, it):
    for _ in range(it):
        e = m.copy()
        e[1:, :] &= m[:-1, :]; e[:-1, :] &= m[1:, :]; e[:, 1:] &= m[:, :-1]; e[:, :-1] &= m[:, 1:]
        m = e
    return m

def srgb_to_lin(c):
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)

for arg in sys.argv[1:]:
    # <png>[:x0,y0,x1,y1] crops one panel of a grid
    p, _, box = arg.partition(':')
    img = Image.open(p).convert('RGB')
    if box: img = img.crop(tuple(int(v) for v in box.split(',')))
    im = np.asarray(img).astype(np.float64) / 255.0
    lin = srgb_to_lin(im)
    l = lin.max(axis=2)
    peak = l.max()
    lit = l > 0.1 * peak
    n = int(lit.sum())
    if n == 0:
        print(p, 'nothing lit'); continue
    hi8 = float((l[lit] > 0.8 * peak).mean()); hi5 = float((l[lit] > 0.5 * peak).mean())
    filled = fill_holes(lit)
    holes = filled & ~lit
    gap = float(holes.sum() / filled.sum())
    ys, xs = np.nonzero(lit); cy, cx = ys.mean(), xs.mean()
    gy, gx = np.gradient(l)
    Y, X = np.mgrid[0:l.shape[0], 0:l.shape[1]]
    ry, rx = Y - cy, X - cx; rn = np.hypot(rx, ry) + 1e-6; rx /= rn; ry /= rn
    rad = gx * rx + gy * ry; tan = -gx * ry + gy * rx
    m = filled
    aniso = float((rad[m] ** 2).sum() / max((tan[m] ** 2).sum(), 1e-12))
    # hue classes on the sRGB-encoded values (the frames are clamped, tonemapped captures): white-yellow min/max >= 0.6
    # and G/R >= 0.75; red G/R < 0.5; orange between
    R, G, B = im[..., 0], im[..., 1], im[..., 2]
    mx = im.max(axis=2) + 1e-6; mn = im.min(axis=2)
    gr = G / (R + 1e-6)
    white = lit & (mn / mx >= 0.6) & (gr >= 0.75)
    red = lit & (gr < 0.5) & ~white
    # anisotropy inside the body (the outline's own gradient is radial for any shape): the lit set eroded 4 px;
    # radial tongues have tangential gradients, so aniso_in < 1 is streaky, ~1 isotropic mottle
    # the same on the high-pass luma (a box blur of R/4 removed, R the outline's radius: the radial fall-off and the
    # outline no longer count): thin radial tongues or rays give tangential gradients, hp < 1; mottle ~1; a disc ~1
    inner = erode(lit, 4)
    rr = max(2, int(round(np.hypot(xs - cx, ys - cy).max() / 4)))
    c = np.pad(l, rr + 1).cumsum(0).cumsum(1)
    H, W = l.shape
    box = (c[2*rr+1:2*rr+1+H, 2*rr+1:2*rr+1+W] - c[0:H, 2*rr+1:2*rr+1+W] - c[2*rr+1:2*rr+1+H, 0:W] + c[0:H, 0:W]) / (2*rr+1)**2
    hp = l - box
    hgy, hgx = np.gradient(hp)
    hrad = hgx * rx + hgy * ry; htan = -hgx * ry + hgy * rx
    hp_aniso = float((hrad[inner] ** 2).sum() / max((htan[inner] ** 2).sum(), 1e-12)) if inner.any() else float('nan')
    hp_std = float(hp[inner].std() / peak) if inner.any() else float('nan')
    aniso_in = float((rad[inner] ** 2).sum() / max((tan[inner] ** 2).sum(), 1e-12)) if inner.any() else float('nan')
    orange = lit & ~white & ~red
    rmax = np.hypot(xs - cx, ys - cy).max() + 1e-6
    def mr(mask):
        yy, xx = np.nonzero(mask)
        return float(np.hypot(xx - cx, yy - cy).mean() / rmax) if len(yy) else float('nan')
    edge = lit & ~erode(lit, 2)
    thin = float(edge.sum() / n)
    print(f'{p.split("/")[-1]}: peak {peak:.3f} lit {n} hi0.8 {hi8:.3f} hi0.5 {hi5:.3f} gap {gap:.3f} aniso {aniso:.2f} aniso_in {aniso_in:.2f} hp_aniso {hp_aniso:.2f} hp_std {hp_std:.3f} '
          f'white {white.sum()/n:.3f}@{mr(white):.2f} orange {orange.sum()/n:.3f}@{mr(orange):.2f} red {red.sum()/n:.3f}@{mr(red):.2f} edge2px {thin:.3f}')
