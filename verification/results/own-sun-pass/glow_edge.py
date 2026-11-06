#!/usr/bin/env python3
"""The own sun's glow edge in tonemapped 8-bit terms (docs/architecture/own-sun-look.md, "Step 1e: the glow's tail").

The glow law of src/effects/own_sun_ps.hlsl (constants read by name from src/proxy/own_sun_core.h; the window form
selected by --window) for a white sun at K = 2, fog T = 1, every tap open, ONE/ONE onto a black sky (engine 0) or the
sky level (decoded 0.0035), sampled one pixel apart along the direction between spokes 0 and 1 at 1440 and 768 rows,
then the display chain of tools/analysis/agx_reference.py (gamma-2.2 decode, 2^EV, minimal AgX, 8-bit round; the
display dither is not applied). Prints per resolution, EV and background: the radius where the display drops below
1/255 above the background, the window's value there, the largest 8-bit step between neighbouring pixels beyond the
core (r >= 1 R and r >= 3 R), the largest jump of the display's per-pixel slope beyond 3 R (a derivative
discontinuity, codes per px) with its radius, and the slope at the quad's edge.
Usage: python3 glow_edge.py [--window poly2|smoother] [--glow-radius R] [--taper-start R]
  poly2     W = saturate(1 - (r / R_g)^2)^2 (step 1d)
  smoother  W = 1 - smootherstep(r_a, R_g, r): C2 at both ends, exactly 1 inside r_a (step 1e)
Defaults: the header's own form (a taper_start constant -> smoother) and constants.
"""
import argparse
import math
import re
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import agx_reference as agx  # noqa: E402

HEADER = (ROOT / 'src/proxy/own_sun_core.h').read_text()


def constant(name, default=None):
    m = re.search(r'\b%s = ([0-9.]+)f?\b' % name, HEADER)
    if not m:
        if default is None:
            raise SystemExit('missing constant %s' % name)
        return default
    return float(m.group(1))


C = {n: constant(n) for n in ('corona_gain', 'corona_sigma', 'halo_gain', 'halo_radius', 'ray_count', 'ray_gain', 'ray_length',
                              'ray_start', 'ray_offset', 'ray_sigma', 'ray_sigma_min_px', 'core_inner', 'core_gain',
                              'radiance_default', 'radius_default_u')}


def window(r, form, glow_radius, taper_start):
    if form == 'poly2':
        return np.clip(1 - (r / glow_radius) ** 2, 0, 1) ** 2
    t = np.clip((r - taper_start) / (glow_radius - taper_start), 0, 1)
    return 1 - t * t * t * (t * (6 * t - 15) + 10)


def glow(r, theta, radius_px, form, glow_radius, taper_start):
    corona = C['corona_gain'] * np.exp(-(r / C['corona_sigma']) ** 2)
    halo = C['halo_gain'] / (1 + (r / C['halo_radius']) ** 2) * window(r, form, glow_radius, taper_start)
    n = int(C['ray_count'])
    sigma = max(C['ray_sigma_min_px'], C['ray_sigma'] * radius_px) / radius_px
    rays = np.zeros_like(r)
    for k in range(n):
        h1, h2 = (k * 0.6180340 + 0.3) % 1, (k * 0.7548777 + 0.31) % 1
        length, strength = C['ray_length'] * (0.15 + 0.85 * h1), 0.6 + 0.6 * h2
        ang = (k - C['ray_offset']) * 2 * math.pi / n
        across = np.abs(np.sin(theta - ang)) * r
        along = np.maximum(r - C['ray_start'], 0) / length
        rays += strength * np.exp(-(across / sigma) ** 2) * np.exp(-along) * np.clip(1 - along, 0, 1) ** 2 * (np.cos(theta - ang) > 0)
    g = corona + halo + C['ray_gain'] * rays
    s = np.clip((r - C['core_inner']) / (1 - C['core_inner']), 0, 1)
    d = 1 - s * s * (3 - 2 * s)
    return g + (C['radiance_default'] * C['core_gain'] - g) * d


def display(engine, ev):
    return np.array([agx.tonemap_engine((v, v, v), 2.0 ** ev)[1] for v in engine])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--window', choices=('poly2', 'smoother'))
    ap.add_argument('--glow-radius', type=float)
    ap.add_argument('--taper-start', type=float)
    a = ap.parse_args()
    form = a.window or ('smoother' if re.search(r'\btaper_start = ', HEADER) else 'poly2')
    glow_radius = a.glow_radius or constant('glow_radius')
    taper_start = a.taper_start or constant('taper_start', 0.0)
    quad = glow_radius  # quad_extent = glow_radius
    gap = (0.5 - C['ray_offset']) * 2 * math.pi / int(C['ray_count'])
    print('window %s, R_g %.2f, taper start %.2f, quad half extent %.2f core radii' % (form, glow_radius, taper_start, quad))
    sky_engine = 0.0035 ** (1 / 2.2)
    for height in (1440, 768):
        radius_px = C['radius_default_u'] * height
        px = np.arange(0, int(quad * radius_px * 1.6)) + 0.5  # pixel centres along the line, past the quad
        r = px / radius_px
        e = glow(r, np.full_like(r, gap), radius_px, form, glow_radius, taper_start)
        e[r > quad] = 0.0  # outside the quad nothing is drawn
        for ev in (1.3, 0.0):
            for name, bg in (('black', 0.0), ('sky', sky_engine)):
                disp = display(e + bg, ev)
                base = display(np.array([bg]), ev)[0]
                codes = np.floor(np.clip(disp, 0, 1) * 255 + 0.5)
                above = np.nonzero(disp - base >= 1 / 255)[0]
                r_cross = r[above[-1]] if len(above) else 0.0
                step = np.abs(np.diff(codes))
                slope = np.diff(disp) * 255  # codes per px
                jump = np.abs(np.diff(slope))
                ji = int(np.argmax(np.where(r[1:-1] >= 3.0, jump, 0)))
                w_cross = window(np.array([r_cross]), form, glow_radius, taper_start)[0]
                edge = int(np.searchsorted(r, quad)) - 2
                print('%4d rows R %.2f px EV %+.1f %-5s: < 1/255 above the background from %.2f R (%.0f px), window there %.3f; '
                      'max 8-bit step r>=1R %d, r>=3R %d; max slope jump r>=3R %.4f codes/px at %.2f R; slope at the quad edge %.4f codes/px'
                      % (height, radius_px, ev, name, r_cross, r_cross * radius_px, w_cross, step[r[1:] >= 1.0].max(),
                         step[r[1:] >= 3.0].max(), jump[ji], r[1 + ji], slope[edge]))
        rs = np.array([1, 1.5, 2, 3, 4, 6, 9, 12, 16, 20, 24, 28])
        er = glow(rs, np.full_like(rs, gap), radius_px, form, glow_radius, taper_start)
        er[rs > quad] = 0
        dr = display(er, 1.3)
        print('     engine/display(EV +1.3, black) at %s R: %s' % (','.join('%g' % x for x in rs),
                                                                 ' '.join('%.4f/%.3f' % (x, y) for x, y in zip(er, dr))))
        print('     quad %.0f px square = %.3f Mpx at %d rows' % (2 * quad * radius_px, (2 * quad * radius_px) ** 2 / 1e6, height))


if __name__ == '__main__':
    main()
