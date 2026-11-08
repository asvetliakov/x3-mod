#!/usr/bin/env python3
"""fire_turbulence.py <frame.png[:x0,y0,x1,y1]> ...: the round-3 turbulence measures (fire-material.md section 13) on
display frames (the vanilla sRGB capture, a 1080p AgX tile, a lab screenshot), the same arithmetic as the fixture's
classes::burst::fire_of on agx_display values:
  luma  Rec.709 of the display values; peak its maximum
  lit   luma > 0.1 peak; R_eq = sqrt(lit / pi) about the centre (the centroid of the component >= 0.5 peak holding the
        brightest pixel; the fixture uses the body's projected centre)
  hf    sum over the disc of (4-neighbour Laplacian of luma)^2 / sum over the disc of luma^2
  core  the 4-connected component of luma >= 0.5 peak holding the centre (else the nearest), its area over the disc's
  out   the largest other 4-connected component of luma >= 0.5 peak, its area over the disc's
Prints one row per frame."""
import sys
import numpy as np
from PIL import Image


def components(mask):
    lab = np.zeros(mask.shape, dtype=np.int32)
    h, w = mask.shape
    n = 0
    for y0, x0 in zip(*np.nonzero(mask)):
        if lab[y0, x0]:
            continue
        n += 1
        st = [(y0, x0)]
        lab[y0, x0] = n
        while st:
            y, x = st.pop()
            for yy, xx in ((y + 1, x), (y - 1, x), (y, x + 1), (y, x - 1)):
                if 0 <= yy < h and 0 <= xx < w and mask[yy, xx] and not lab[yy, xx]:
                    lab[yy, xx] = n
                    st.append((yy, xx))
    return lab, n


def measure(rgb, centre=None):
    l = rgb @ np.array([0.2126, 0.7152, 0.0722])
    pk = l.max()
    if not pk > 0:
        return None
    lit = l > 0.1 * pk
    hot = l >= 0.5 * pk
    lab, n = components(hot)
    yy, xx = np.mgrid[0:l.shape[0], 0:l.shape[1]]
    if centre is None:
        by, bx = np.unravel_index(np.argmax(l), l.shape)
        m = lab == lab[by, bx]
        centre = ((xx * m).sum() / m.sum(), (yy * m).sum() / m.sum())
    X, Y = centre
    R = np.sqrt(lit.sum() / np.pi)
    disc = np.hypot(xx - X, yy - Y) <= R
    lap = np.zeros_like(l)
    lap[1:-1, 1:-1] = 4 * l[1:-1, 1:-1] - l[2:, 1:-1] - l[:-2, 1:-1] - l[1:-1, 2:] - l[1:-1, :-2]
    inner = disc.copy()
    inner[[0, -1], :] = False
    inner[:, [0, -1]] = False
    hf = (lap[inner] ** 2).sum() / max((l[inner] ** 2).sum(), 1e-12)
    cy, cx = int(round(Y)), int(round(X))
    k = lab[cy, cx] if 0 <= cy < l.shape[0] and 0 <= cx < l.shape[1] else 0
    if k == 0 and n:  # the centre pixel under 0.5 peak: the component nearest the centre
        d = np.where(hot, np.hypot(xx - X, yy - Y), np.inf)
        k = lab.flat[np.argmin(d)]
    areas = np.bincount(lab.ravel(), minlength=n + 1)
    core = areas[k] / disc.sum() if k else 0.0
    others = [areas[j] for j in range(1, n + 1) if j != k]
    out = max(others) / disc.sum() if others else 0.0
    return dict(peak=pk, lit=int(lit.sum()), R_eq=R, hf=hf, core=core, out=out, comps=n)


if __name__ == '__main__':
    for arg in sys.argv[1:]:
        path, _, box = arg.partition(':')
        a = np.asarray(Image.open(path).convert('RGB'), dtype=float) / 255
        if box:
            x0, y0, x1, y1 = map(int, box.split(','))
            a = a[y0:y1, x0:x1]
        r = measure(a)
        print('%s: lit %d px, R_eq %.1f px, hf %.4f, core %.3f, out %.4f, components %d' %
              (arg.split('/')[-1], r['lit'], r['R_eq'], r['hf'], r['core'], r['out'], r['comps']))
