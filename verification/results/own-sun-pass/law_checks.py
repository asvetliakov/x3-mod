#!/usr/bin/env python3
"""Offline checks of the own sun's step-1b ray law (src/effects/own_sun_ps.hlsl, constants from src/proxy/own_sun_core.h;
adapted from the step-1b review's checks): the minimax arctangent's error against numpy's (in radians and spoke units),
the hashed ray lengths and strengths per spoke, the seam a pixel sees at a spoke boundary with the nearest spoke alone
(step 1b as first built) and with the two nearest spokes summed (the review fix), and the core-visibility tap weights.
Reads the constants by name from the header and the program; no device. Usage: python3 law_checks.py
"""
import math
import re
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
HEADER = (ROOT / 'src/proxy/own_sun_core.h').read_text()
PROGRAM = (ROOT / 'src/effects/own_sun_ps.hlsl').read_text()


def constant(name):
    return float(re.search(r'\b%s = ([0-9.]+)f?\b' % name, HEADER).group(1))


N = int(constant('ray_count'))
L_MAX, W0, OFFSET, GAIN = constant('ray_length'), constant('ray_width'), constant('ray_offset'), constant('ray_gain')
CORE_INNER = constant('core_inner')
HALO_GAIN, HALO_RADIUS, GLOW_RADIUS = constant('halo_gain'), constant('halo_radius'), constant('glow_radius')


def atan_poly(u):
    u2 = u * u
    return ((((-0.0134804 * u2 + 0.0574773) * u2 - 0.1212391) * u2 + 0.1956359) * u2 - 0.3329946) * u2 * u + 0.9999956 * u


def hashes(k):
    return (k * (0.6180340 + k * 0.1547)) % 1.0, (k * 0.7548777 + 0.31) % 1.0


def spoke(r, k, across):
    k = k % N
    h1, h2 = hashes(k)
    along = r / (L_MAX * (0.25 + 0.75 * h1 * h1))
    fade = min(1., max(0., 1 - along))
    perpendicular = r * across * (2 * math.pi / N) / W0 / (1 - 0.7 * min(1., max(0., along)))
    return (0.6 + 0.4 * h2) * math.exp(-perpendicular * perpendicular) * fade * fade


def core_weight(r):
    s = min(1., max(0., (r - CORE_INNER) / (1 - CORE_INNER)))
    return 1 - s * s * (3 - 2 * s)


def main():
    assert '0.9999956 * u' in PROGRAM and '-0.0134804' in PROGRAM, 'the program\'s arctangent changed'
    u = np.linspace(0, 1, 200001)
    error = np.abs(atan_poly(u) - np.arctan(u))
    print('atan: max error %.2e rad at u %.4f = %.2e spoke units' % (error.max(), u[error.argmax()], error.max() * N / (2 * math.pi)))
    print('spokes (k: length core radii, strength):', ', '.join('%d: %.2f, %.2f' % (k, L_MAX * (0.25 + 0.75 * hashes(k)[0] ** 2),
                                                                                    0.6 + 0.4 * hashes(k)[1]) for k in range(N)))
    single, summed = (0., None), (0., None)
    for r in np.linspace(CORE_INNER, GLOW_RADIUS, 2000):
        outside = 1 - core_weight(r)
        for k in range(N):
            # The boundary between spoke k (across +0.5) and k + 1 (across -0.5).
            jump = abs(spoke(r, k, .5) - spoke(r, k + 1, -.5)) * GAIN * outside
            if jump > single[0]:
                single = (jump, (round(float(r), 3), k))
            # Summed: the nearest and its neighbour on the pixel's side, from either side of the boundary.
            left = spoke(r, k, .5) + spoke(r, k + 1, -.5)
            right = spoke(r, k + 1, -.5) + spoke(r, k, .5)
            jump = abs(left - right) * GAIN * outside
            if jump > summed[0]:
                summed = (jump, (round(float(r), 3), k))
    print('seam (glow units x (1 - D)): nearest spoke alone %.4f at r %s; two nearest summed %.2e' % (single[0], single[1], summed[0]))
    block = PROGRAM[PROGRAM.index('const float visible = '):]
    weights = [float(v) for v in re.findall(r'(\d\.\d{7})', block[:block.index(';')])]
    print('core-visibility weights centre %.4f inner %.4f outer %.4f, sum %.5f, largest single-tap step %.1f %%'
          % (weights[0], weights[1], weights[2], weights[0] + 6 * weights[1] + 6 * weights[2], 100 * max(weights)))
    print('halo at r 1 / 4 / 8: %.4f / %.4f / %.4f' % tuple(HALO_GAIN / (1 + r * r / HALO_RADIUS ** 2) * max(0., 1 - (r / GLOW_RADIUS) ** 2) ** 2
                                                         for r in (1, 4, 8)))


if __name__ == '__main__':
    main()
