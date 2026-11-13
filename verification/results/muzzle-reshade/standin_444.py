#!/usr/bin/env python3
"""The native fixture's body 444 stand-in (verification/probe/weapon_muzzle_reshade_fixture.h standin_mesh_444,
standin_texture_444), mirrored in Python and measured with the functions that gave the body table
(muzzle_inputs.py, muzzle_bodies.py), so the stand-in's figures can be set beside the real body's
(muzzle_bodies_out.txt: cell mean 0.1282, 90 % energy radius 59102, glow-weighted luminance 0.2187).

  python3 -I verification/results/muzzle-reshade/standin_444.py > verification/results/muzzle-reshade/standin_444_out.txt
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

U0, U1, V0, V1 = 0.001, 0.991, 0.513, 0.985
AMPLITUDE = 0.7


def texture():
    hue = np.array([0.707, 1.0, 0.456])
    lum_hue = 0.2126 * 0.707 + 0.7152 + 0.0722 * 0.456
    n, seed = 256, 376
    t = np.zeros((n, n, 4), np.uint8)
    for y in range(n):
        for x in range(n):
            seed = (seed * 1664525 + 1013904223) & 0xffffffff
            noise = (seed >> 8) / 16777216.0
            u, v = (x + 0.5) / n, (y + 0.5) / n
            t[y, x, 3] = 255
            if u < U0 or u > U1 or v < V0 or v > V1:
                continue
            along, across = (u - U0) / (U1 - U0), abs(v - 0.749) / 0.236
            width = 0.35 + 0.45 * min(1.0, along / 0.25)
            env = 1.0 if along < 0.15 else max(0.0, 1 - (along - 0.15) / 0.75)
            lum = AMPLITUDE * env * (1 - across / width) if across < width else 0.0
            lum *= 0.4 + 1.2 * noise
            for k in range(3):
                c = lum / lum_hue * hue[k]
                t[y, x, k] = 255 if c >= 1 else int(c * 255 + 0.5)
    return t


def mesh():
    tris = []
    ua, ub = U0 + 0.001, U1
    for deg in (0, 90, 45, 135):
        va, vb = (0.534, V1) if deg in (45, 135) else (V0, 0.964)
        w = 10085 * np.array([np.cos(np.radians(deg)), np.sin(np.radians(deg)), 0.0])
        gun, far = np.array([0, 0, -489.0]), np.array([0, 0, 98969.0])
        a, b, c, d = (tuple(far - w), (ub, va)), (tuple(far + w), (ub, vb)), (tuple(gun - w), (ua, va)), (tuple(gun + w), (ua, vb))
        tris += [[a, b, c], [d, c, b]]
    return tris


def main():
    t, tris = texture(), mesh()
    with tempfile.TemporaryDirectory() as tmp:
        png = str(pathlib.Path(tmp) / 'standin_444.png')
        Image.fromarray(t).save(png)
        with contextlib.redirect_stdout(io.StringIO()):
            c = mi.cell(png, (U0, U1, V0, V1))
            radii = mi.lit_extent(png, tris)
        lum = (t[..., :3].astype(float) / 255) @ np.array(mi.W)
        ys, xs = np.mgrid[0:256, 0:256]
        U, V = (xs + 0.5) / 256, (ys + 0.5) / 256
        inside = (U >= U0) & (U <= U1) & (V >= V0) & (V <= V1)
        print(f'stand-in 444 (amplitude {AMPLITUDE}): cell mean luminance {lum[inside].mean():.4f} (real 0.1282); '
              f'90 % energy radius {radii[0.9]:.0f} (real 59102); glow-weighted luminance at the real radius '
              f'{mb.glow_luminance(png, tris, 59102.0)[0]:.4f} (real 0.2187); hue K ({c["hue"][0]:.4f}, {c["hue"][1]:.4f}, '
              f'{c["hue"][2]:.4f}) (real 0.7865, 1.1132, 0.5072)')


if __name__ == '__main__':
    main()
