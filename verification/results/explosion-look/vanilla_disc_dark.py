#!/usr/bin/env python3
"""vanilla_disc_dark.py <vanilla_fireball.png>: the round-2 gap clause (fire-material.md section 12) on the vanilla frame:
the lit set (max channel > 0.1 of the peak, sRGB decoded with 2.2), its equal-area disc (radius sqrt(lit / pi)) about the
clipped core's centroid (pixels >= 0.99 of the peak; the fixture uses the burst's projected centre), and the share of the
disc's pixels under 0.1 of the peak. Also with the lit set's centroid and in sRGB values."""
import sys
import numpy as np
from PIL import Image
a = np.asarray(Image.open(sys.argv[1]).convert('RGB'), dtype=float) / 255
yy, xx = np.mgrid[0:a.shape[0], 0:a.shape[1]]
for dom, l in (('linear', (a ** 2.2).max(axis=2)), ('srgb', a.max(axis=2))):
    pk = l.max()
    lit = l > 0.1 * pk
    core = l >= 0.99 * pk
    R = np.sqrt(lit.sum() / np.pi)
    for name, (X, Y) in (('core centroid', ((xx * core).sum() / core.sum(), (yy * core).sum() / core.sum())),
                         ('lit centroid', ((xx * lit).sum() / lit.sum(), (yy * lit).sum() / lit.sum()))):
        disc = np.hypot(xx - X, yy - Y) <= R
        print('%s, %s: lit %d px, R_eq %.1f px, dark share in the disc %.3f' % (dom, name, lit.sum(), R, (l[disc] < 0.1 * pk).mean()))
