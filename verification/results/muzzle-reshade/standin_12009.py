#!/usr/bin/env python3
"""The native fixture's body 12009 stand-in (verification/probe/weapon_muzzle_effects_cases.h standin_mesh_12009,
standin_texture_12009), mirrored in Python and measured with the functions that gave the body table (muzzle_inputs.py,
muzzle_bodies.py), so its figures can be set beside the real body's (muzzle_bodies_out.txt: 50 quads, cell mean
luminance 0.0292, 90 % energy radius 48104, glow-weighted luminance 0.0264, ray sum 9.88, hue (3.1806, 0.4087, 0.4367)).

The mesh: 50 quads in the real body's three cones (CONES: the real quads' axis tilt, length, azimuths, gun-edge middle
and across vectors in the cone's frame, rounded; 8 along the axis, 98800 long; 18 tilted 41.6 degrees at six azimuths,
54390 long; 24 tilted 77.9 degrees at eight azimuths, 40640 long), each mapping the cell u 0.406..0.570 (across),
v 0.447 (the gun edge) .. 0.241 (far). The texture: 512 x 512, inside the cell a red streak with the real cell's
profiles (along v: 0.1 at the far edge rising to 1 at 70 %, flat to 86 %, falling to 0 at 99 %; across u: flat over
0.4..0.6 of the cell, a Gaussian edge of WIDTH beyond), a deterministic texel noise, the cell's hue
(1, 0.128, 0.137); black outside; alpha 255. The noise factor 0.3 + 2.1 n^2 (n uniform 0..1; mean 1) gives the
real cell's p99 and band spread, which the brightest pixels of a re-shaded draw follow.

  python3 -I verification/results/muzzle-reshade/standin_12009.py > verification/results/muzzle-reshade/standin_12009_out.txt
Procedural only: reads no game file.
"""
import contextlib
import io
import pathlib
import sys
import tempfile

import numpy as np
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import muzzle_bodies as mb  # noqa: E402
import muzzle_inputs as mi  # noqa: E402

U0, U1, V0, V1 = 0.406, 0.570, 0.241, 0.447
AMPLITUDE = 0.15
WIDTH = 0.07
HUE = (1.0, 0.128, 0.137)
REAL = dict(mean=0.0292, radius=48104.0, lum_glow=0.0264, ray=9.88, hue=(3.1806, 0.4087, 0.4367))


def texture():
    n, seed = 512, 12009
    lum_hue = 0.2126 * HUE[0] + 0.7152 * HUE[1] + 0.0722 * HUE[2]
    t = np.zeros((n, n, 4), np.uint8)
    t[..., 3] = 255
    for y in range(n):
        for x in range(n):
            seed = (seed * 1664525 + 1013904223) & 0xffffffff
            noise = (seed >> 8) / 16777216.0
            u, v = (x + 0.5) / n, (y + 0.5) / n
            if u < U0 or u > U1 or v < V0 or v > V1:
                continue
            s, q = (v - V0) / (V1 - V0), (u - U0) / (U1 - U0)  # s 0 far .. 1 the gun edge; q across
            along = 0.1 + 0.9 * min(1.0, s / 0.7) if s < 0.86 else max(0.0, 1.0 - (s - 0.86) / 0.13)
            across = np.exp(-(max(0.0, abs(q - 0.5) - 0.1) / WIDTH) ** 2)  # flat over q 0.4..0.6
            lum = AMPLITUDE * along * across * (0.3 + 2.1 * noise * noise)
            for c in range(3):
                val = lum / lum_hue * HUE[c]
                t[y, x, c] = 255 if val >= 1 else int(val * 255 + 0.5)
    return t


# Per cone: tilt (degrees from +z), length, azimuths, the gun edge's middle in (radial, tangential, z) and the across
# vectors in the cone's frame (d the axis, a = (-sin az, cos az, 0), b = d x a), body units, rounded from the real
# quads' (muzzle_bodies.py's mesh).
CONES = ((0.0, 98800.0, (0.0,), ((250.0, 1800.0, 1200.0), (250.0, -4200.0, 1200.0)),
          ((0.0, 0.0, -35940.0), (0.0, -25415.0, -25415.0), (0.0, 25415.0, -25415.0), (0.0, -35940.0, 0.0))),
         (41.6, 54390.0, tuple(30.0 + 60.0 * k for k in range(6)), ((-2725.0, -250.0, 6784.0),),
          ((0.0, -35940.0, 0.0), (-7217.0, -25414.0, 22410.0), (7217.0, -25414.0, -22410.0))),
         (77.9, 40640.0, tuple(45.0 * k for k in range(8)), ((-4663.0, -250.0, 8852.0),),
          ((0.0, -35940.0, 0.0), (-9015.0, -25414.0, 44988.0), (9015.0, -25414.0, -44988.0))))


def quads():
    """(gun-edge middle, axis unit vector, length, across vector) per quad, body units: 8 + 18 + 24."""
    out = []
    for tilt, length, azimuths, mids, acrosses in CONES:
        t = np.radians(tilt)
        for az in azimuths:
            p = np.radians(az)
            d = np.array([np.sin(t) * np.cos(p), np.sin(t) * np.sin(p), np.cos(t)])
            a = np.array([-np.sin(p), np.cos(p), 0.0])
            b = np.cross(d, a)
            r = np.array([np.cos(p), np.sin(p), 0.0])
            for mid in mids:
                gun = mid[0] * r + mid[1] * a + np.array([0.0, 0.0, mid[2]])
                for c in acrosses:
                    out.append((gun, d, length, c[0] * d + c[1] * a + c[2] * b))
    return out


def mesh():
    tris = []
    for gun, d, length, across in quads():
        g0, g1 = gun - 0.5 * across, gun + 0.5 * across
        f0, f1 = g0 + length * d, g1 + length * d
        a, b, c, e = (tuple(f0), (U0, V0)), (tuple(f1), (U1, V0)), (tuple(g0), (U0, V1)), (tuple(g1), (U1, V1))
        tris += [[a, b, c], [e, c, b]]
    return tris


def main():
    t, tris = texture(), mesh()
    with tempfile.TemporaryDirectory() as tmp:
        png = str(pathlib.Path(tmp) / 'standin_12009.png')
        Image.fromarray(t).save(png)
        log = io.StringIO()
        with contextlib.redirect_stdout(log):
            c = mi.cell(png, (U0, U1, V0, V1))
            radii = mi.lit_extent(png, tris)
            ray = mi.overlap(tris, radii[0.9])
        stats = [line for line in log.getvalue().splitlines() if line.startswith(('texture ', 'texel noise'))]
        lum = (t[..., :3].astype(float) / 255) @ np.array(mi.W)
        ys, xs = np.mgrid[0:512, 0:512]
        U, V = (xs + 0.5) / 512, (ys + 0.5) / 512
        inside = (U >= U0) & (U <= U1) & (V >= V0) & (V <= V1)
        glow = mb.glow_luminance(png, tris, REAL['radius'])[0]
        figures = dict(mean=lum[inside].mean(), radius=radii[0.9], lum_glow=glow, ray=ray)
        print(f'stand-in 12009 (amplitude {AMPLITUDE}, {len(tris) // 2} quads):')
        for k, v in figures.items():
            print(f'  {k:8s} {v:10.4f}  real {REAL[k]:10.4f}  ratio {v / REAL[k]:.3f}')
        print(f'  hue K ({c["hue"][0]:.4f}, {c["hue"][1]:.4f}, {c["hue"][2]:.4f}) real {REAL["hue"]}')
        print('  ' + '\n  '.join(stats))
        print('  real: cell texels 8904: luminance max 0.539 mean 0.0292 p99 0.268; texel noise: luminance sd in r '
              'bands 0-0.1 0.061, 0.3-0.4 0.066, 0.6-0.7 0.073 (muzzle_bodies_out.txt)')
        print('  (lum_glow at the real radius, as standin_444.py; ray: the summed glow a ray through the gun crosses, '
              'mean over 500 directions, at the stand-in\'s own radius)')


if __name__ == '__main__':
    main()
