#!/usr/bin/env python3
"""Three mocks of one IRE bolt at 500 m on a black field with a dim hull edge (docs/architecture/projectile-look.md).

No lab, no shaders: an analytic core + Gaussian glow rendered in linear light with numpy, exposed and sRGB-encoded.
Not a render of the production chain: no bloom, no AgX, no TAA; the figures are the design's starting values, to be
calibrated in flight. Writes mock-thin.png, mock-stretched.png, mock-glow.png (512x256) beside this script.

  python3 verification/results/projectile-look/render_mocks.py
"""
import json
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 512, 256
ROWS = 1440                 # the user's screen height
F_PX_PER_M_AT_1M = 1280.0   # (rows/2)/tan(vfov/2) at the launcher default fov 90 (58.72 vertical); vanilla 73.74 gives 960
D_REF_M = 500.0
PPM = F_PX_PER_M_AT_1M / D_REF_M          # 2.56 px per metre at 500 m
IRE_SPEED_MPS = 581000 / 500.0            # FL TBullets col 11 / 500
STEP_1X_S = 1 / 60.0                       # one 60 fps frame of game time at SETA 1
TEXTURE_RGB_8 = (121, 82, 39)              # weapon_effects.json row 0 (SS_BULLET_IRE) texture_rgb, 8-bit sRGB


def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)


def tint_from_texture():
    lin = srgb_to_linear(TEXTURE_RGB_8)
    return lin / lin.max()                 # the hue of the texture mean at full radiance


def bolt(core_px, glow_ratio, glow_strength, stretch_px, hot_share, core_stops):
    """Radiance field (H, W, 3), linear, scene white = 1.0.

    core: a 1-D Gaussian across the axis with FWHM core_px, a flat segment of length stretch_px along it
          with Gaussian ends (the per-frame motion stretch), radiance 2**core_stops, colour lerp(tint, white, hot_share)
          at its centre line.
    glow: a Gaussian of sigma glow_ratio * core_px/2 about the same segment, radiance glow_strength, the tint.
    """
    tint = tint_from_texture()
    cx, cy, ang = 0.42 * W, 0.46 * H, np.deg2rad(-12.0)
    ux, uy = np.cos(ang), np.sin(ang)
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float64)
    dx, dy = xs + 0.5 - cx, ys + 0.5 - cy
    along = dx * ux + dy * uy
    across = -dx * uy + dy * ux
    half = stretch_px / 2.0
    # distance to the segment [-half, +half] on the axis
    seg = np.maximum(np.abs(along) - half, 0.0)
    r2 = seg * seg + across * across
    sigma_core = core_px / 2.3548          # FWHM -> sigma
    core = np.exp(-r2 / (2 * sigma_core ** 2))
    sigma_glow = glow_ratio * core_px / 2.0
    glow = np.exp(-r2 / (2 * sigma_glow ** 2))
    core_colour = tint * (1 - hot_share) + np.ones(3) * hot_share
    e_core = 2.0 ** core_stops
    img = core[..., None] * core_colour * e_core + glow[..., None] * tint * glow_strength
    return img


def hull_edge():
    """A dim hull edge in the lower right: a dark panel (linear luminance 0.012-0.03, slightly cool) whose straight
    slanted edge crosses the frame, so the bolt is judged against black space and beside a faint lit surface."""
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float64)
    edge = (ys - 0.80 * H) - 0.30 * (xs - 0.60 * W)       # >0 below the edge line
    inside = np.clip(edge / 1.5, 0, 1)                        # anti-aliased edge
    shade = 0.012 + 0.018 * np.clip((xs - 0.4 * W) / (0.6 * W), 0, 1)   # a gentle lateral gradient (a lit panel)
    panel = inside * shade
    rgb = np.stack([panel * 0.9, panel * 0.95, panel * 1.08], axis=-1)
    lip = inside * np.exp(-np.clip(edge, 0, None) / 2.5) * 0.03       # the edge catches a little more light
    return rgb + lip[..., None] * np.array([0.9, 0.95, 1.08])


def expose_encode(img, ev=0.0):
    out = img * (2.0 ** ev)
    return (linear_to_srgb(out) * 255.0 + 0.5).astype(np.uint8)


def main():
    tint = tint_from_texture()
    stretch_1x_m = IRE_SPEED_MPS * STEP_1X_S
    stretch_1x_px = stretch_1x_m * PPM
    core_px = 2.0
    mocks = {
        # name: (core_px, glow_ratio, glow_strength, stretch_px, hot_share, core_stops)
        'thin':      (core_px, 3.0, 0.20, 0.0,           0.30, 2.0),   # A: 2 px core, faint glow (sigma 3 px), no stretch
        'stretched': (core_px, 3.0, 0.20, 6.0 * core_px, 0.30, 2.0),   # B: A with a 6:1 motion stretch (12 px)
        'glow':      (core_px, 8.0, 0.40, 0.0,           0.30, 2.0),   # C: A with a wider, stronger halo (sigma 8 px)
    }
    hull = hull_edge()
    summary = {
        'texture_rgb_srgb8': list(TEXTURE_RGB_8),
        'tint_linear_normalised': [round(float(v), 4) for v in tint],
        'tint_srgb8_at_full': [int(round(float(v) * 255)) for v in linear_to_srgb(tint)],
        'f_px_per_m_at_1m': F_PX_PER_M_AT_1M, 'd_ref_m': D_REF_M, 'px_per_m_at_ref': PPM,
        'ire_speed_mps': IRE_SPEED_MPS,
        'stretch_1x_60fps_m': round(stretch_1x_m, 2), 'stretch_1x_60fps_px_at_ref': round(stretch_1x_px, 1),
        'mocks': {},
    }
    for name, params in mocks.items():
        img = bolt(*params) + hull
        png = expose_encode(img)
        path = os.path.join(HERE, f'mock-{name}.png')
        Image.fromarray(png, 'RGB').save(path)
        keys = ('core_px_fwhm', 'glow_sigma_over_core_radius', 'glow_strength', 'stretch_px', 'hot_share', 'core_stops')
        summary['mocks'][name] = dict(zip(keys, params)) | {'size': [W, H], 'file': os.path.basename(path)}
    with open(os.path.join(HERE, 'mocks_summary.json'), 'w') as fh:
        json.dump(summary, fh, indent=1)
    json.dump(summary, sys.stdout, indent=1)
    print()


if __name__ == '__main__':
    main()
