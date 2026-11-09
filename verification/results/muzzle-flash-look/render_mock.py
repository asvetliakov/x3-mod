#!/usr/bin/env python3
"""One mock of the proposed IRE muzzle flash (docs/architecture/muzzle-flash-look.md): the two mounts of a fighter seen
from the chase camera, each IRE bolt's first-frame streak leaving, the flash (point + short streak) and the hull pulse
at the design's start values, and the vanilla flare's measured size as a dim outline for scale.

No lab, no shaders: analytic Gaussians in linear light with numpy, exposed at 0 EV and sRGB-encoded as the projectile
mocks (verification/results/projectile-look/render_mocks.py). No bloom, AgX or TAA: the halo the bloom would add to a
16 x white point is absent. The frame is a 1440-row frame scaled to 256 rows (f 1280 -> 227.6 px m). Writes
mock-ire-muzzle.png (512x256) and mock_summary.json beside this script.

  python3 verification/results/muzzle-flash-look/render_mock.py
"""
import json
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 512, 256
ROWS = 1440
SCALE = H / ROWS
F_FULL = 1280.0                      # px m at fov 90 (58.72 deg vertical) and 1440 rows
F = F_FULL * SCALE                   # 227.6 px m in the mock
D_REF_M, CORE_PX = 500.0, 2.0        # the bolt's core: 2 px FWHM at 500 m (projectile-look.md section 4)
GLOW_RATIO, GLOW_GAIN, HOT_SHARE, STOPS = 3.0, 0.2, 0.3, 2.0
IRE_SPEED_MPS = 581000 / 500.0
STEP_1X_S = 1 / 60.0
TEXTURE_RGB_8 = (121, 82, 39)        # weapon_effects.json row 0 texture_rgb
# the design's start values (muzzle-flash-look.md section 5)
FLASH_CORE_MULT, FLASH_LEN_M, FLASH_LIFE_FRAC, FLASH_STOPS, HULL_PULSE_R = 1.25, 2.5, 0.2, 2.0, 3.0
PULSE_GAIN = 0.5                     # scene white at the mount (k 0)
# the vanilla flare measured in the run 61 screenshot relative to the bolt core at the gun (the note's before paragraph)
VANILLA_FWHM_OVER_CORE, VANILLA_EXTENT_OVER_CORE = 2.0, 2.7
# the scene: eye 24 m behind and 5 m above the hull centre, looking at it; the mounts 2.5 m ahead on the wings
EYE = np.array([0.0, 5.0, 0.0])
HULL_C = np.array([0.0, 0.0, 24.0])
MOUNTS = [np.array([-3.2, -0.2, 26.5]), np.array([3.2, -0.2, 26.5])]
BOLT_DIR = np.array([0.0, 0.0, 1.0])


def srgb_to_linear(c):
    c = np.asarray(c, dtype=np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def linear_to_srgb(c):
    c = np.clip(c, 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)


def tint():
    lin = srgb_to_linear(TEXTURE_RGB_8)
    return lin / lin.max()


def basis():
    fwd = HULL_C - EYE
    fwd /= np.linalg.norm(fwd)
    right = np.cross(fwd, np.array([0.0, 1.0, 0.0]))
    right /= np.linalg.norm(right)
    up = np.cross(right, fwd)
    return right, up, fwd


def project(p):
    """World point -> (x, y) screen px (y down) and the view depth in metres."""
    right, up, fwd = basis()
    v = np.asarray(p, dtype=np.float64) - EYE
    z = float(v @ fwd)
    return np.array([W / 2 + F * (v @ right) / z, H / 2 - F * (v @ up) / z]), z


def core_fwhm_px(depth_m):
    return CORE_PX * D_REF_M / depth_m * SCALE


def segment_field(p0, p1, fwhm_px, glow_sigma_px):
    """Gaussian core across a screen segment (round caps) and its glow: two (H, W) fields in [0, 1]."""
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float64)
    px, py = xs + 0.5, ys + 0.5
    d = p1 - p0
    ll = float(d @ d)
    t = 0.0 if ll < 1e-9 else np.clip(((px - p0[0]) * d[0] + (py - p0[1]) * d[1]) / ll, 0.0, 1.0)
    cx, cy = p0[0] + t * d[0], p0[1] + t * d[1]
    dist2 = (px - cx) ** 2 + (py - cy) ** 2
    sigma_c = fwhm_px / (2 * np.sqrt(2 * np.log(2)))
    return np.exp(-0.5 * dist2 / sigma_c**2), np.exp(-0.5 * dist2 / glow_sigma_px**2)


def bolt_first_frame(mount, tnt):
    """The bolt's first-frame streak: from the muzzle to the head after one 60 fps step at 1x (19.4 m), the head's
    width, +2 stops, hot centre, the glow at 0.2 (projectile-look.md section 2)."""
    head = mount + BOLT_DIR * IRE_SPEED_MPS * STEP_1X_S
    s0, _ = project(mount)
    s1, zh = project(head)
    w = core_fwhm_px(zh)
    glow_sigma = max(GLOW_RATIO * w / 2, 1.5 * SCALE)
    core, glow = segment_field(s0, s1, w, glow_sigma)
    colour = tnt + (1 - tnt) * HOT_SHARE
    img = core[..., None] * colour * 2.0**STOPS + glow[..., None] * tnt * GLOW_GAIN
    return img, {'head_depth_m': zh, 'core_fwhm_px': w, 'streak_px': float(np.linalg.norm(s1 - s0)), 'travel_m': IRE_SPEED_MPS * STEP_1X_S}


def flash(mount, tnt):
    """The flash at its first frame: a point of FLASH_CORE_MULT x the core at the gun's depth, a streak of FLASH_LEN_M
    along the axis in the tint, both at +FLASH_STOPS over the core; the glow as the bolt program draws it."""
    s0, z0 = project(mount)
    s1, _ = project(mount + BOLT_DIR * FLASH_LEN_M)
    radiance = 2.0 ** (STOPS + FLASH_STOPS)
    # width defined at scene white: a Gaussian of peak R clips white out to sigma sqrt(2 ln R), so the flash's sigma is
    # FLASH_CORE_MULT x the core's x sqrt(ln R_core / ln R_flash) and its clipped extent is FLASH_CORE_MULT x the core's
    w = min(FLASH_CORE_MULT * core_fwhm_px(z0) * np.sqrt(np.log(2.0**STOPS) / np.log(radiance)), 48.0 * SCALE)
    glow_sigma = max(GLOW_RATIO * w / 2, 1.5 * SCALE)
    point, pglow = segment_field(s0, s0, w, glow_sigma)
    streak, sglow = segment_field(s0, s1, w, glow_sigma)
    colour = tnt + (1 - tnt) * HOT_SHARE
    img = point[..., None] * colour * radiance + streak[..., None] * tnt * radiance
    img += np.maximum(pglow, sglow)[..., None] * tnt * GLOW_GAIN
    sig = lambda fwhm: fwhm / (2 * np.sqrt(2 * np.log(2)))
    clipped = 2 * sig(w) * np.sqrt(2 * np.log(radiance))
    core_clipped = 2 * sig(core_fwhm_px(z0)) * np.sqrt(2 * np.log(2.0**STOPS))
    return img, {'depth_m': z0, 'point_fwhm_px': w, 'core_fwhm_px_at_gun': core_fwhm_px(z0), 'streak_px': float(np.linalg.norm(s1 - s0)),
                 'radiance': radiance, 'white_clipped_px': clipped, 'core_white_clipped_px_at_gun': core_clipped,
                 'clipped_ratio': clipped / core_clipped, 'life_ms_1x': min(FLASH_LIFE_FRAC * 150.0, 2 * STEP_1X_S * 1000)}


def ship_masks():
    """A fighter from behind: a fuselage and two wings as anti-aliased masks (4x supersampled polygons)."""
    ss = 4
    im = Image.new('L', (W * ss, H * ss), 0)
    dr = ImageDraw.Draw(im)

    def poly(points):
        dr.polygon([tuple((project(p)[0] * ss).tolist()) for p in points], fill=255)

    # wings: from the fuselage out to 6.5 m, dipping 0.6 m, 0.45 m thick, swept back 1.5 m
    for sx in (-1, 1):
        poly([(sx * 1.1, 0.1, 25.0), (sx * 8.0, -0.5, 23.0), (sx * 8.0, -1.0, 23.0), (sx * 1.1, -0.4, 25.0)])
        # a gun pod at the mount
        poly([(sx * 2.85, 0.2, 27.0), (sx * 3.55, 0.2, 27.0), (sx * 3.55, -0.55, 24.0), (sx * 2.85, -0.55, 24.0)])
    # fuselage: an ellipse about the hull centre, 1.3 m half width, 0.9 m half height
    c, z = project(HULL_C)
    rx, ry = 1.3 * F / z * ss, 0.9 * F / z * ss
    dr.ellipse([c[0] * ss - rx, c[1] * ss - ry, c[0] * ss + rx, c[1] * ss + ry], fill=255)
    mask = np.asarray(im.resize((W, H), Image.LANCZOS), dtype=np.float64) / 255.0
    return np.clip(mask, 0, 1)


def hull(mask):
    """The dim hull: 0.03-0.08 linear (a hull in shadow; a lit hull is 0.2-1.5), cool, its upper edges catching light."""
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float64)
    shade = 0.03 + 0.05 * np.clip((ys - 0.3 * H) / (0.5 * H), 0, 1)
    gy = np.clip(np.gradient(mask, axis=0), 0, None)     # the upper edges catch light
    panel = mask * shade + gy * 0.08
    return np.stack([panel * 0.9, panel * 0.95, panel * 1.08], axis=-1)


def pulse(mount, mask, tnt):
    """The hull pulse at its first frame: PULSE_GAIN x (1 - r / R)^2 on the hull within R = HULL_PULSE_R of the mount."""
    s0, z0 = project(mount)
    r_px = HULL_PULSE_R * F / z0
    ys, xs = np.mgrid[0:H, 0:W].astype(np.float64)
    r = np.hypot(xs + 0.5 - s0[0], ys + 0.5 - s0[1])
    fall = np.clip(1 - r / r_px, 0, None) ** 2
    return (fall * mask)[..., None] * tnt * PULSE_GAIN, {'radius_px': r_px}


def vanilla_outline(draw, mount, note):
    """The vanilla flare's measured size beside the right mount: its extent (solid) and FWHM (dotted) over the core."""
    s0, z0 = project(mount)
    w = core_fwhm_px(z0)
    ext, fwhm = VANILLA_EXTENT_OVER_CORE * w, VANILLA_FWHM_OVER_CORE * w
    c = s0 + np.array([ext + 48.0, 0.0])
    grey = (90, 90, 90)
    draw.ellipse([c[0] - ext / 2, c[1] - ext / 2, c[0] + ext / 2, c[1] + ext / 2], outline=grey)
    for a in np.linspace(0, 2 * np.pi, 24, endpoint=False):
        x, y = c[0] + fwhm / 2 * np.cos(a), c[1] + fwhm / 2 * np.sin(a)
        draw.point((x, y), fill=grey)
    draw.text((c[0] - 36, c[1] + ext / 2 + 14), 'vanilla flare (measured)', fill=grey)
    draw.text((s0[0] - 24, s0[1] + ext / 2 + 26), 'proposed', fill=grey)
    note['vanilla_extent_px'] = ext
    note['vanilla_fwhm_px'] = fwhm


def main():
    tnt = tint()
    mask = ship_masks()
    img = hull(mask)
    summary = {'size': [W, H], 'scale_of_1440_rows': SCALE, 'f_px_m': F, 'eye_m': EYE.tolist(), 'hull_centre_m': HULL_C.tolist(),
               'mounts_m': [m.tolist() for m in MOUNTS], 'texture_rgb': list(TEXTURE_RGB_8),
               'tint_linear': [round(float(v), 3) for v in tnt],
               'tune': {'flash_core_mult': FLASH_CORE_MULT, 'flash_len_m': FLASH_LEN_M, 'flash_life_frac': FLASH_LIFE_FRAC,
                        'flash_stops': FLASH_STOPS, 'hull_pulse_r': HULL_PULSE_R}, 'mounts': []}
    for m in MOUNTS:
        b, bn = bolt_first_frame(m, tnt)
        f, fn = flash(m, tnt)
        p, pn = pulse(m, mask, tnt)
        img = img + b + f + p
        summary['mounts'].append({'bolt_first_frame': bn, 'flash': fn, 'pulse': pn})
    png = (linear_to_srgb(img) * 255.0 + 0.5).astype(np.uint8)
    out = Image.fromarray(png, 'RGB')
    note = {}
    vanilla_outline(ImageDraw.Draw(out), MOUNTS[1], note)
    summary['vanilla_outline'] = note
    path = os.path.join(HERE, 'mock-ire-muzzle.png')
    out.save(path)
    summary['file'] = os.path.basename(path)
    with open(os.path.join(HERE, 'mock_summary.json'), 'w') as fh:
        json.dump(summary, fh, indent=1)
    json.dump(summary, sys.stdout, indent=1)
    print()


if __name__ == '__main__':
    main()
