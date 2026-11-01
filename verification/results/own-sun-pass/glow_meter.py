#!/usr/bin/env python3
"""Step 1c look, tuned in step 1d (docs/architecture/own-sun-look.md): the law (constants read from src/proxy/own_sun_core.h by name) evaluated offline against the HDR meter's tile model
(verification/results/own-sun-pass/meter_exposure.py: tile = 4^steps px until both axes <= 128; p99 of the tile maxima
sets ev_limit = log2(0.9 * 16.2917424) - log2(min(L, 64)); L = engine^2.2 under the default gamma2.2 decode; a tile is
"lit" when its mean log2 L >= log2(1/512), L floored at 1e-4) and against the quad / slot cost. White sun, fog T = 1,
every occlusion tap open. Prints: the field maxima per element, tile counts per threshold at 5120x1440 and 1280x768 over
16 sub-tile placements, the worst ev_limit any sun tile can impose, the ray lengths per spoke, the quad area.
Usage: python3 glow_meter.py
"""
import math
import re
from pathlib import Path

import numpy as np

WHITE, CLIP, FLOOR, BG = 0.9 * 16.2917424, 64.0, 1e-4, 1 / 512
# ---- the law's constants, read by name from src/proxy/own_sun_core.h (core radii; R = radius_u * height px) ----
HEADER = (Path(__file__).resolve().parents[3] / 'src/proxy/own_sun_core.h').read_text()


def constant(name):
    return float(re.search(r'\b%s = ([0-9.]+)f?\b' % name, HEADER).group(1))


RADIUS_U, K = constant('radius_default_u'), constant('radiance_default')
CORE_INNER, WHITE_MIX, CORE_GAIN = constant('core_inner'), constant('white_mix'), constant('core_gain')
CORONA_A, CORONA_SIGMA = constant('corona_gain'), constant('corona_sigma')
HALO_A, HALO_RHO, GLOW_RADIUS = constant('halo_gain'), constant('halo_radius'), constant('glow_radius')
RAY_N, RAY_A, RAY_LENGTH = int(constant('ray_count')), constant('ray_gain'), constant('ray_length')
RAY_SIGMA_PX, RAY_SIGMA_MIN, RAY_OFFSET = constant('ray_sigma'), constant('ray_sigma_min_px'), constant('ray_offset')  # sigma = max(3 px, 0.2 R)
RAY_START = constant('ray_start')  # the rays' along-profile starts at the core's edge
QUAD = GLOW_RADIUS  # half extent, core radii, both axes
GAP = (0.5 - RAY_OFFSET) * 2 * math.pi / RAY_N  # the direction between spokes 0 and 1
# AgX display reads from docs/architecture/bloom-falloff.md (exposed linear -> display), log-linear interpolation [S].
AGX = [(0.0035, 0.04), (0.01, 0.10), (0.05, 0.28), (0.18, 0.50), (0.5, 0.68), (1, 0.79), (2, 0.87), (4, 0.93), (8, 0.97), (16, 0.998)]


def agx(exposed):
    if exposed <= AGX[0][0]:
        return AGX[0][1] * exposed / AGX[0][0]
    for (x0, y0), (x1, y1) in zip(AGX, AGX[1:]):
        if exposed <= x1:
            return y0 + (y1 - y0) * (math.log(exposed / x0) / math.log(x1 / x0))
    return AGX[-1][1]


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0, 1)
    return t * t * (3 - 2 * t)


def spoke_params(n):
    out = []
    for k in range(n):
        h1 = (k * 0.6180340 + 0.3) % 1
        h2 = (k * 0.7548777 + 0.31) % 1
        out.append((RAY_LENGTH * (0.15 + 0.85 * h1), 0.6 + 0.6 * h2))
    return out


def field(px, py, radius_px):
    """Engine value of a white sun at offsets (px, py) in core radii."""
    r = np.hypot(px, py)
    d = 1 - smoothstep(CORE_INNER, 1.0, r)
    w = np.clip(1 - (r / GLOW_RADIUS) ** 2, 0, 1) ** 2
    corona = CORONA_A * np.exp(-(r / CORONA_SIGMA) ** 2)
    halo = HALO_A / (1 + (r / HALO_RHO) ** 2) * w
    theta = np.arctan2(py, px)
    rays = np.zeros_like(r)
    sigma = max(RAY_SIGMA_MIN, RAY_SIGMA_PX * radius_px) / radius_px  # core radii
    params = spoke_params(RAY_N)
    for k, (length, strength) in enumerate(params):
        # distance across spoke k (angle k * 2pi/N - offset), two nearest summed == all spokes summed here (Gaussian tails)
        ang = (k - RAY_OFFSET) * 2 * math.pi / RAY_N
        across = np.abs(np.sin(theta - ang)) * r
        along = np.maximum(r - RAY_START, 0) / length
        rays += strength * np.exp(-(across / sigma) ** 2) * np.exp(-along) * np.clip(1 - along, 0, 1) ** 2 * (np.cos(theta - ang) > 0)
    rays *= RAY_A
    glow = corona + halo + rays
    core = K * CORE_GAIN  # white: hue 1, m 1
    return glow + (core - glow) * d, glow, corona, halo, rays, d


def tile_grid(width, height):
    w, h, steps = width, height, 0
    while True:
        w, h, steps = math.ceil(w / 4), math.ceil(h / 4), steps + 1
        if w <= 128 and h <= 128:
            return w, h, 4 ** steps


def main():
    # Field maxima on a fine polar grid outside the core.
    r = np.linspace(1.0, QUAD * 1.5, 4000)
    th = np.linspace(-math.pi, math.pi, 1441)
    rr, tt = np.meshgrid(r, th)
    for height in (1440, 768):
        radius_px = RADIUS_U * height
        v, glow, corona, halo, rays, _ = field(rr * np.cos(tt), rr * np.sin(tt), radius_px)
        print('height %d (R = %.1f px, ray sigma %.1f px): glow max outside the core %.3f (corona %.3f, halo %.3f, rays %.3f); '
              'glow between spokes at r = 1, 1.5, 2, 3, 6, 9, 12, 16, 20 R: %s' % (height, radius_px, max(RAY_SIGMA_MIN, RAY_SIGMA_PX * radius_px),
              glow.max(), corona.max(), halo.max(), rays.max(),
              ', '.join('%.3f' % field(np.array([x * math.cos(GAP)]), np.array([x * math.sin(GAP)]), radius_px)[1][0]
                        for x in (1, 1.5, 2, 3, 6, 9, 12, 16, 20))))
    print('ray lengths beyond r = %g / strengths, N = %d: %s' % (RAY_START, RAY_N, ', '.join('%.2f R x %.2f' % p for p in spoke_params(RAY_N))))
    # Each ray's visible reach (inferred): the last radius where it adds >= 10 % to the glow between spokes there.
    radius_px = RADIUS_U * 1440
    reach = []
    for k, (length, strength) in enumerate(spoke_params(RAY_N)):
        ang = (k - RAY_OFFSET) * 2 * math.pi / RAY_N
        rs = np.linspace(1, RAY_START + length, 2000)
        on = field(rs * math.cos(ang), rs * math.sin(ang), radius_px)
        base = on[2] + on[3]
        seen = rs[on[4] >= 0.1 * base]
        reach.append('%.0f deg %.1f' % (math.degrees(ang) % 360, seen.max() if len(seen) else 0.))
    print('ray visible reach (rays >= 10 %% of corona + halo, 1440 rows), angle and R: %s' % ', '.join(reach))
    # Display reads of the axis profile (between spokes) at EV +1.3 (x 2.46) and EV 0, white sun.
    radius_px = RADIUS_U * 1440
    for ev in (1.3, 0.0):
        reads = []
        for x in (0, 1, 1.5, 2, 3, 4, 6, 9, 12, 16):
            v = field(np.array([x * math.cos(GAP)]), np.array([x * math.sin(GAP)]), radius_px)[0][0]
            reads.append('%gR %.3f->%.2f' % (x, v, agx(v ** 2.2 * 2 ** ev)))
        print('display at EV %+.1f (engine -> AgX): %s' % (ev, '; '.join(reads)))
    # Tiles.
    for width, height in ((5120, 1440), (1280, 768)):
        tw, thh, tile = tile_grid(width, height)
        tiles = tw * thh
        need = tiles - tiles * 99 // 100
        radius_px = RADIUS_U * height
        half = int(QUAD * radius_px) + tile
        worst = {}
        for i in range(4):
            for j in range(4):
                cx, cy = width / 2 + tile * i / 4, height / 2 + tile * j / 4
                x0, y0 = int(cx) - half, int(cy) - half
                xs = np.arange(x0, x0 + 2 * half) + 0.5
                ys = np.arange(y0, y0 + 2 * half) + 0.5
                X, Y = np.meshgrid(xs, ys)
                v = field((X - cx) / radius_px, (Y - cy) / radius_px, radius_px)[0]
                lum = np.clip(v ** 2.2, FLOOR, CLIP)
                # tile reduce: index by tile
                tx, ty = ((X - 0.5) // tile).astype(int), ((Y - 0.5) // tile).astype(int)
                keys = tx * 100000 + ty
                uniq, inv = np.unique(keys, return_inverse=True)
                tmax = np.zeros(len(uniq)); np.maximum.at(tmax, inv.ravel(), lum.ravel())
                tsum = np.zeros(len(uniq)); np.add.at(tsum, inv.ravel(), np.log2(lum).ravel())
                tcount = np.zeros(len(uniq)); np.add.at(tcount, inv.ravel(), 1)
                tmean = tsum / tcount
                full = tcount >= tile * tile * 0.999
                stats = {
                    'max>=5.96 (K 2.25 eq)': int((tmax >= 5.96).sum()),
                    'max>1 (engine>1)': int((tmax > 1.0).sum()),
                    'max>=0.18 (key)': int((tmax >= 0.18).sum()),
                    'max>=0.0035 (sky)': int((tmax >= 0.0035).sum()),
                    'lit (mean log2 >= log2 1/512)': int((tmean[full] >= math.log2(BG)).sum()),
                    'mean>=0.0035 (sky)': int((2 ** tmean[full] >= 0.0035).sum()),
                }
                for k, n in stats.items():
                    worst[k] = (min(worst.get(k, (n, n))[0], n), max(worst.get(k, (n, n))[1], n))
                brightest = tmax.max()
        print('%dx%d tiles %dx%d (%d px) = %d, p99 needs %d: ' % (width, height, tw, thh, tile, tiles, need) +
              '; '.join('%s %d..%d (%.2f..%.2f %%)' % (k, lo, hi, 100 * lo / tiles, 100 * hi / tiles) for k, (lo, hi) in worst.items()))
        print('  brightest sun tile L %.2f -> ev_limit %+.2f EV if it is the p99 tile; a tile at the glow max (engine <= 1.0) '
              '-> ev_limit >= %+.2f' % (brightest, math.log2(WHITE) - math.log2(min(brightest, CLIP)), math.log2(WHITE)))
        area = (2 * QUAD * radius_px) ** 2 / 1e6
        print('  quad %.0f x %.0f px = %.2f Mpx; at 200 slots x 0.136 ps per slot-pixel (1 us per slot per 5120x1440 frame, AGENTS.md) = %.0f us (inferred)'
              % (2 * QUAD * radius_px, 2 * QUAD * radius_px, area, area * 1e6 * 200 * 0.136e-6))


if __name__ == '__main__':
    main()
