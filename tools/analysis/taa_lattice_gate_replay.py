#!/usr/bin/env python3
"""Host replay of the camera-relative thin-region gate with a wide-box neighbourhood clip.

Step 1 of the pivot in docs/architecture/lattice-approach-review-2026-09-21.md section 6:
decide numerically, on captures already on disk, whether opening the thin region's speed gate
on camera-relative speed (pixel speed minus the camera path, the subtraction the mask pass's
gateClosure already has both terms for) and bounding the retained history with a wide min/max
box beats the installed thin region on the moving lattice.

This is an offline experiment, not a shader implementation: it re-uses
``tools/analysis/taa_resolve_replay.py``'s ``resolve`` as the oracle and patches its source with
the two candidate terms, exactly as the section 15 investigation did. Display is AgX + RCAS
without bloom, as in section 15; metrics are display luma codes.

Variants (all with plain Catmull-Rom history and W = 0.97):
  installed         the installed thin region (own speed, gate 0.03-0.25, clip off)
  gate_open         gate closed by camera-relative speed instead, clip off
  gate_open_box7    same gate, retained history clipped to the 7x7 current min/max box
  gate_open_box11   same gate, 11x11 box

The global history-cut heuristics (median displacement, missing key) are CPU-side whole-frame
discards and are not modelled here at all, so every variant runs with both off, matching the
launcher defaults since Run57. ``taa_history=1`` on every replayed frame of every sequence, so
the dumped history the replay seeds from carries no cut either.

usage: taa_lattice_gate_replay.py --output <json> [--png-dir DIR] [--sequences a,b,...]
"""
import argparse
import inspect
import json
import os
import runpy
import sys
import time
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
FRAME_W, FRAME_H = 1280, 768

# Installed parameters: --taa-thin-region 0.97 (RELAX 1.0 = clip off), gate 0.030-0.250.
THIN = dict(W=0.97, lo=0.030, hi=0.250)

VARIANTS = {
    'installed': dict(thinregion=dict(THIN)),
    'gate_open': dict(coherent=True, thinregion=dict(THIN)),
    'gate_open_box7': dict(coherent=True, widebox=3, thinregion=dict(THIN)),
    'gate_open_box11': dict(coherent=True, widebox=5, thinregion=dict(THIN)),
}
# Reference for the background-trail measure of section 13: the plain resolve of these captures
# (w = 0.900, no thin region), which is what those captures actually recorded.
PLAIN = {}

# Replay rectangle / material-tracking rectangle per sequence, frame coordinates.
# run177 rotation reproduces section 15's published rectangles; the other lattice sequences use the
# densest thin-geometry 350x315 window of their first frame (visually checked: run201 = the station's
# solar-panel lattice, run159 = the plant, run161 = the distant station over the bright nebula).
SEQUENCES = {
    'run177-rotation': dict(dump='/tmp/x3-bottleX3-run177', frames='6392-6423',
                            roi=(790, 20, 1140, 335), track=(905, 55, 1095, 165), role='rotation'),
    'run201-a': dict(dump='/tmp/x3-bottleX3-run201', frames='10442-10473',
                     roi=(779, 364, 1129, 679), track=(859, 464, 1049, 579), role='rotation'),
    'run201-b': dict(dump='/tmp/x3-bottleX3-run201', frames='12010-12041',
                     roi=(749, 429, 1099, 744), track=(829, 529, 1019, 644), role='rotation'),
    # run206 (2026-09-21): the first bursts captured under the Run57 defaults, both pans at the
    # solar plant with the installed set live. 5730 is the slower pan, 6936 the fast one.
    'run206-slow': dict(dump='/tmp/x3-bottleX3-run206', frames='5730-5761',
                        roi=(629, 269, 979, 584), track=(709, 369, 899, 484), role='rotation'),
    'run206-fast': dict(dump='/tmp/x3-bottleX3-run206', frames='6936-6967',
                        roi=(904, 344, 1254, 659), track=(984, 444, 1174, 559), role='rotation'),
    'run177-stationary': dict(dump='/tmp/x3-bottleX3-run177', frames='5674-5705',
                              roi=(790, 20, 1140, 335), track=(905, 55, 1095, 165), role='control'),
    'run159-slow': dict(dump='/tmp/x3-bottleX3-run159', frames='4789-4820',
                        roi=(524, 199, 874, 514), track=(604, 299, 794, 414), role='control'),
    'run161-trail': dict(dump='/tmp/x3-bottleX3-run161', frames='5151-5182',
                         roi=(459, 344, 809, 659), track=(539, 444, 729, 559), role='trail'),
    # run209 (2026-09-21): the two bursts flown under the camera gate. 4421 is forward flight
    # (camera translation 136 -> 109 world units/frame, radial expansion 0.64-0.80 %/frame), 7799 a
    # roll. The forward ROI is the thin region the user reported crawling in.
    'run209-forward': dict(dump='/tmp/x3-bottleX3-run209', frames='4421-4452',
                           roi=(860, 10, 1260, 290), track=(940, 60, 1180, 200), role='forward'),
    'run209-roll': dict(dump='/tmp/x3-bottleX3-run209', frames='7799-7830',
                        roi=(860, 10, 1260, 290), track=(940, 60, 1180, 200), role='roll'),
}

# Section 15's stale-history witness: a 6x6 bright patch injected at the previous coordinate of the
# background site, about (950,115); the added display luma is read at (947,123).
WITNESS = dict(sequence='run177-rotation', frame=6401, site=(947, 123), patch=6, value=5.0,
               depths=(-1.0, 0.9995, 0.99995, 1.0))
CLIPPED_RESOLVE_CODES = 47.0  # section 15: what the ordinary clipped resolve admits at that site
SCORE_FROM = 15  # section 15 scores the last 16 of the 31 resolved frames


def box_minmax(a, radius):
    """Per-channel min and max over a (2*radius+1)^2 window of ``a`` (H+2r, W+2r, C).

    Separable, so 2*(2r+1) passes instead of (2r+1)^2 fetches; the result is the exact window
    extremum, which is what the candidate clip needs. Returns two (H, W, C) arrays.
    """
    n = 2 * radius + 1
    h = a.shape[0] - 2 * radius
    w = a.shape[1] - 2 * radius
    lo = np.minimum.reduce([a[:, i:i + w] for i in range(n)])
    hi = np.maximum.reduce([a[:, i:i + w] for i in range(n)])
    lo = np.minimum.reduce([lo[i:i + h] for i in range(n)])
    hi = np.maximum.reduce([hi[i:i + h] for i in range(n)])
    return lo, hi


def wide_box(cur, ys, xs, radius, weigh, k):
    """Weighted-domain min/max box of the current frame around the replay crop, edge-clamped."""
    h, w = ys.shape
    ry = np.clip(np.arange(ys[0, 0] - radius, ys[0, 0] + h + radius), 0, cur.shape[0] - 1)
    rx = np.clip(np.arange(xs[0, 0] - radius, xs[0, 0] + w + radius), 0, cur.shape[1] - 1)
    ext = weigh(cur[np.ix_(ry, rx)][..., :3].astype(np.float64), k)
    return box_minmax(ext, radius)


# Source patches applied to taa_resolve_replay.resolve. (1)-(3) add the camera-relative gate speed
# (the routed displacement measured against the camera path at the same pixel, which is what
# line_mask_ps.hlsl's gateClosure already computes both terms of); (4) adds the wide-box clip on the
# retained history; (5) exposes the new quantities for the witness.
PATCHES = (
    ('exp = np.ones_like(posx); ok = okcam.copy()',
     'camx, camy = posx.copy(), posy.copy()\n    exp = np.ones_like(posx); ok = okcam.copy()'),
    ('speed = np.hypot(posx - xs, posy - ys)',
     'speed = np.hypot(posx - xs, posy - ys)\n'
     '    camera_residual = np.hypot(posx - (camx - dil[..., 0]), posy - (camy - dil[..., 1]))'),
    ("tcl = np.clip((speed - tr['lo']) / (tr['hi'] - tr['lo']), 0, 1)",
     "trm = tm\n"
     "        gatespeed = camera_residual if opt.get('coherent') else speed\n"
     "        tcl = np.clip((gatespeed - tr['lo']) / (tr['hi'] - tr['lo']), 0, 1)"),
    ('trg = np.zeros((h, wd))',
     'trg = np.zeros((h, wd)); trm = np.zeros((h, wd), bool)'),
    ("relaxed = old_unclipped if tr.get('gamma') is None else"
     " np.clip(old_unclipped, mean - tr['gamma'] * sig, mean + tr['gamma'] * sig)",
     "relaxed = old_unclipped if tr.get('gamma') is None else"
     " np.clip(old_unclipped, mean - tr['gamma'] * sig, mean + tr['gamma'] * sig)\n"
     "        if opt.get('widebox'):\n"
     "            blo, bhi = wide_box(cur, ys, xs, opt['widebox'], weigh, k)\n"
     "            relaxed = np.clip(relaxed, blo, bhi)"),
    ('farw=farw, trg=trg)',
     'farw=farw, trg=trg, trm=trm, camera_residual=camera_residual, posx=posx, posy=posy)'),
)


def load_replay(dump, frames, roi):
    """Import taa_resolve_replay for one dump/crop and install the candidate terms."""
    os.environ['FR'] = frames
    sys.argv = ['replay', dump, *[str(v) for v in roi], 'noop']
    module = runpy.run_path(str(ROOT / 'tools/analysis/taa_resolve_replay.py'))
    source = inspect.getsource(module['resolve'])
    for old, new in PATCHES:
        assert source.count(old) == 1, old
        source = source.replace(old, new)
    ns = module['resolve'].__globals__
    ns['wide_box'] = wide_box
    exec(compile(source, '<lattice gate experiment>', 'exec'), ns)
    return module


def bilinear(a, x, y):
    x = np.clip(x, 0, a.shape[1] - 1.000001)
    y = np.clip(y, 0, a.shape[0] - 1.000001)
    xx = np.floor(x).astype(int)
    yy = np.floor(y).astype(int)
    fx = x - xx
    fy = y - yy
    return (a[yy, xx] * (1 - fx) * (1 - fy) + a[yy, xx + 1] * fx * (1 - fy)
            + a[yy + 1, xx] * (1 - fx) * fy + a[yy + 1, xx + 1] * fx * fy)


def routed_tracks(m, track_roi):
    """Material points followed by the per-frame homography fitted to the routed motion vectors.

    Same registration as section 15's tracked metrics: the camera-only path drifts from the truss,
    the routed fit does not. Returns per-frame (x, y) in crop coordinates and the fit residuals.
    """
    load, meta, frames = m['load'], m['meta'], m['frames']
    X0, Y0 = m['X0'], m['Y0']
    ys, xs = m['ys'], m['xs']
    tx0, ty0, tx1, ty1 = track_roi
    ry, rx = np.mgrid[ty0:ty1, tx0:tx1]
    pts = np.stack([rx / FRAME_W, ry / FRAME_H, np.ones_like(rx, float)], -1)
    tracks, residuals = [], []
    cumulative = np.eye(3)
    for f in frames[1:]:
        mot = load('motion', f, 'rgba32f', np.float32, 4)
        dep = load('depth', f, 'rgba32f', np.float32, 4)[..., 0]
        mv = mot[ys, xs]
        sel = (dep[ys, xs] >= 0) & (mv[..., 3] == 1)
        if sel.sum() < 64:
            residuals.append([float('nan')] * 2)
            tracks.append(tracks[-1] if tracks else (pts[..., 0] * FRAME_W - X0,
                                                     pts[..., 1] * FRAME_H - Y0))
            continue
        x = xs[sel] / FRAME_W
        y = ys[sel] / FRAME_H
        u = mv[..., 0][sel] + (meta[f]['j'][0] - .5) / FRAME_W
        v = mv[..., 1][sel] + (meta[f]['j'][1] - .5) / FRAME_H
        z0 = np.zeros_like(x)
        one = np.ones_like(x)
        A = np.zeros((2 * len(x), 8))
        A[::2] = np.stack([x, y, one, z0, z0, z0, -x * u, -y * u], -1)
        A[1::2] = np.stack([z0, z0, z0, x, y, one, -x * v, -y * v], -1)
        b = np.stack([u, v], -1).ravel()
        z = np.linalg.lstsq(A, b, rcond=None)[0]
        err = (A @ z - b).reshape(-1, 2) * [FRAME_W, FRAME_H]
        residuals.append(np.percentile(np.linalg.norm(err, axis=-1), [50, 99]).tolist())
        cumulative = np.linalg.inv(np.r_[z, 1].reshape(3, 3)) @ cumulative
        q = pts @ cumulative.T
        tracks.append((q[..., 0] / q[..., 2] * FRAME_W - X0, q[..., 1] / q[..., 2] * FRAME_H - Y0))
    return tracks, np.array(residuals, float)


def erode(mask, radius):
    a = np.pad(mask, radius, constant_values=False)
    h, w = mask.shape
    return np.logical_and.reduce([a[radius + dy:radius + dy + h, radius + dx:radius + dx + w]
                                  for dy in range(-radius, radius + 1)
                                  for dx in range(-radius, radius + 1)])


def region_speed(diags, field):
    """p50/p90 of a speed field over the fragmented-region pixel-frames of the burst."""
    vals = np.concatenate([d[field][d['trm']] for d in diags]) if diags else np.array([])
    if vals.size == 0:
        return [None, None]
    return [float(v) for v in np.percentile(vals, [50, 90])]


def run_sequence(name, spec, png_dir):
    m = load_replay(spec['dump'], spec['frames'], spec['roi'])
    load, frames = m['load'], m['frames']
    X0, Y0, X1, Y1 = m['X0'], m['Y0'], m['X1'], m['Y1']
    h, w = m['ys'].shape
    ev = m_exposure(m)

    def present(res, f):
        pad = load('taa', f, 'rgba16f', np.float16, 4)[Y0 - 1:Y1 + 1, X0 - 1:X1 + 1, :3]
        pad = pad.astype(np.float32)
        pad[1:-1, 1:-1] = res[..., :3]
        return 255 * (m['rcas'](m['agx'](pad, ev[f]), 2 ** -.75) @ m['LUMA'])

    deps = np.array([load('depth', f, 'rgba32f', np.float32, 4)[Y0:Y1, X0:X1, 0] for f in frames[1:]])
    tracks, residuals = routed_tracks(m, spec['track'])
    # Only the scored tail needs the material point inside the crop; a fast pan can carry it out
    # of the crop entirely, and then this burst has no material-tracked score at all.
    inside = np.logical_and.reduce([(x > 12) & (x < w - 12) & (y > 12) & (y < h - 12)
                                    for x, y in tracks[SCORE_FROM:]])
    material = np.array([bilinear((d >= 0).astype(float), x, y) for d, (x, y) in zip(deps, tracks)])
    support = (material.mean(0) > .08) & inside
    bg = np.array([erode(d < -.5, 3) for d in deps])
    bg[:, :, :12] = False
    bg[:, :, -12:] = False
    bg[:, :12] = False
    bg[:, -12:] = False

    out = {'dump': spec['dump'], 'frames': [frames[0], frames[-1]], 'role': spec['role'],
           'replay_roi': [X0, Y0, X1, Y1], 'track_roi': list(spec['track']),
           'support_pixels': int(support.sum()), 'background_pixels': int(bg.sum()),
           'scored_frames': len(frames) - 1 - SCORE_FROM,
           'routed_fit_residual_p50_p99_px': [float(np.nanmedian(residuals[:, 0])),
                                              float(np.nanmedian(residuals[:, 1]))],
           'variants': {}}

    raws, images = {}, {}
    for key in ('plain', *VARIANTS):
        start = time.monotonic()
        outs, diags, _ = m['run'](PLAIN if key == 'plain' else VARIANTS[key], 1)
        raws[key] = np.array([o.astype(np.float32) for o in outs])
        images[key] = np.array([present(o, f) for o, f in zip(outs, frames[1:])])
        if key == 'plain':
            plain_elapsed = time.monotonic() - start
            continue
        rms = gradient = None
        if support.any():
            tracked = np.array([bilinear(im, x, y) for im, (x, y) in zip(images[key], tracks)])
            tail = tracked[SCORE_FROM:]
            dev = tail - tail.mean(0)
            pair = support[:, 1:] & support[:, :-1]
            rms = float(np.sqrt((dev[:, support] ** 2).mean()))
            gradient = float((np.diff(tail, axis=2) ** 2)[:, pair].mean())
        delta_plain = images[key] - images['plain']
        rec = {
            'tracked_rms': rms,
            'tracked_gradient': gradient,
            'gate_share': float(np.mean([d['trg'].mean() for d in diags])),
            'region_pixel_frames': int(sum(int(d['trm'].sum()) for d in diags)),
            'region_screen_speed_p50_p90': region_speed(diags, 'speed'),
            'region_camera_relative_speed_p50_p90': region_speed(diags, 'camera_residual'),
            'camera_relative_speed_p50_p99': [float(v) for v in np.percentile(
                np.array([d['camera_residual'] for d in diags]), [50, 99])],
            'screen_speed_p50_p99': [float(v) for v in np.percentile(
                np.array([d['speed'] for d in diags]), [50, 99])],
            'background_trail_p99_vs_plain': float(np.percentile(np.abs(delta_plain[bg]), 99)),
            'background_trail_max_vs_plain': float(np.abs(delta_plain[bg]).max()),
            'elapsed_seconds': time.monotonic() - start,
        }
        if key != 'installed':
            d = raws[key] - raws['installed']
            rec['identical_to_installed'] = bool(np.array_equal(raws[key], raws['installed']))
            rec['max_abs_delta_vs_installed_hdr'] = float(np.abs(d).max())
            rec['differing_values_vs_installed'] = int((d != 0).sum())
            rec['total_values'] = int(d.size)
        out['variants'][key] = rec
    out['plain_elapsed_seconds'] = plain_elapsed
    base = out['variants']['installed']
    if base['tracked_rms'] is None:
        out['tracked_metrics'] = ('unavailable: no material point stays inside the replay crop '
                                  'over the scored frames')
    for key, rec in out['variants'].items():
        for field in ('tracked_rms', 'tracked_gradient'):
            rec[field + '_ratio'] = (None if base[field] is None or not base[field]
                                     else rec[field] / base[field])
    if png_dir is not None and name == WITNESS['sequence']:
        out['stale_patch'] = stale_witness(m, present, png_dir)
    return out


def m_exposure(m):
    import re
    ev = {}
    with open(m['D'] + m['log']) as fp:
        for line in fp:
            if line.startswith('hdr_frame device=1 '):
                hit = re.search(r'frame=(\d+).*?ev_adapted=([-\d.]+)', line)
                if hit and int(hit[1]) in m['frames']:
                    ev[int(hit[1])] = float(hit[2])
    return ev


def stale_witness(m, present, png_dir):
    """Section 15's injected stale-history patch, on real current colour/depth/motion."""
    from PIL import Image
    load, meta = m['load'], m['meta']
    X0, Y0 = m['X0'], m['Y0']
    h, w = m['ys'].shape
    f = WITNESS['frame']
    mm, mp = meta[f], meta[f - 1]
    cur = load('hdr', f, 'rgba16f', np.float16, 4).astype(np.float32)
    dep = load('depth', f, 'rgba32f', np.float32, 4)[..., 0].copy()
    mot = load('motion', f, 'rgba32f', np.float32, 4)
    hist = load('taa', f - 1, 'rgba16f', np.float16, 4).astype(np.float32)
    pdep = load('depth', f - 1, 'rgba32f', np.float32, 4)[..., 0].copy()
    age = np.full((h, w), 64.)
    resolve = m['resolve'].__globals__['resolve']

    def step(opt, history, previous_depth):
        return resolve(cur, dep, mot, history, previous_depth, age, mm['j'], mm['k'], mm['w'],
                       mm['R'], mp['R'], mm['P'], 1, opt)

    sx, sy = WITNESS['site']
    x, y = sx - X0, sy - Y0
    _, _, dg = step(VARIANTS['gate_open'], hist, pdep)
    px, py = int(np.floor(dg['posx'][y, x])), int(np.floor(dg['posy'][y, x]))
    n = WITNESS['patch']
    res = {'site': [sx, sy], 'previous_patch_origin': [px - 2, py - 2], 'patch': n,
           'clipped_resolve_codes': CLIPPED_RESOLVE_CODES, 'by_variant': {}}
    for key, opt in VARIANTS.items():
        clean = present(step(opt, hist, pdep)[0], f)
        worst = None
        per_depth = {}
        for z in WITNESS['depths']:
            hh = hist.copy()
            dd = pdep.copy()
            hh[py - 2:py - 2 + n, px - 2:px - 2 + n, :3] = WITNESS['value']
            dd[py - 2:py - 2 + n, px - 2:px - 2 + n] = z
            image = present(step(opt, hh, dd)[0], f)
            added = float(image[y, x] - clean[y, x])
            near = float(np.max(image[y - 2:y + 3, x - 2:x + 3] - clean[y - 2:y + 3, x - 2:x + 3]))
            per_depth[str(z)] = [added, near]
            if worst is None or added > worst[0]:
                worst = (added, near, image)
        res['by_variant'][key] = {'added_codes_at_site': worst[0], 'max_added_near_site': worst[1],
                                  'per_previous_depth': per_depth}
        crop = worst[2][max(y - 24, 0):y + 24, max(x - 24, 0):x + 24]
        img = np.clip(crop, 0, 255).astype(np.uint8)
        img = np.repeat(np.repeat(img, 6, 0), 6, 1)
        Image.fromarray(img).save(Path(png_dir) / ('stale_%s.png' % key))
    return res


# --- camera-path check on real data -----------------------------------------------------------------
# Truth for static routed geometry: the captured motion RG (previous UV, alpha 1) IS the previous
# position, so |camera prediction - routed| measures the camera path directly. The rotation-only path
# is what the installed build uploads (camera_far_plane_reprojection: no translation, no depth), so its
# residual is the camera-relative speed the installed mask program actually sees.
LO, HI = THIN['lo'], THIN['hi']
FAR_P22, FAR_P32 = 1.000003, -6.000018  # the replay's assumed projection rows (m22, m32)
GATE_RADIUS = 8  # the mask program's 17x17 fastest-neighbour window (R 3 + grow 5 in the replay)


def camera_centre(R, t):
    """World position of a camera whose view is world * R + t (row-vector engine convention)."""
    return -np.asarray(t, float) @ R.T


def gate_open_share(speed):
    """Share and mean of the gate openness after the 17x17 fastest-neighbour minimum, one frame."""
    open_ = np.clip(1 - (speed - LO) / (HI - LO), 0, 1)
    pad = np.pad(open_, GATE_RADIUS, mode='edge')
    h, w = open_.shape
    worst = np.min([pad[GATE_RADIUS + oy:GATE_RADIUS + oy + h, GATE_RADIUS + ox:GATE_RADIUS + ox + w]
                    for oy in range(-GATE_RADIUS, GATE_RADIUS + 1)
                    for ox in range(-GATE_RADIUS, GATE_RADIUS + 1)], 0)
    return float((worst > 0).mean()), float(worst.mean())


def percentiles(values, qs=(50, 90, 99)):
    if values.size == 0:
        return [None] * len(qs)
    return [float(v) for v in np.percentile(values, qs)]


def binned(values, key, edges):
    out = []
    for i, (lo, hi) in enumerate(zip(edges[:-1], edges[1:])):
        sel = (key >= lo) & ((key <= hi) if i == len(edges) - 2 else (key < hi))
        out.append({'range': [float(lo), float(hi)], 'pixels': int(sel.sum()),
                    'p50_p90_p99': percentiles(values[sel])})
    return out


def depth_scale_fit(path, nx, ny, d, mm, mp, px, py, sel, jx, jy, W, H,
                    grid=np.linspace(0.95, 1.03, 81)):
    """Best multiplicative view-z scale of one frame, and the residual it leaves.

    FAR_P22 / FAR_P32 are assumed constants (camera_state logs p00/p11/p20/p21 only) and
    camera-state-and-frame-routine.md warns that zf recovered from m22 carries about +-4 %, so the
    linearisation z = m32 / (d - m22) can carry a systematic scale. A scale shows up as a residual
    proportional to the camera translation; this separates it from the irreducible floor.
    """
    best, unity = (1.0, float('inf')), float('nan')
    for a in list(grid) + [1.0]:
        dd = FAR_P22 + (d.astype(np.float64) - FAR_P22) / a
        qx, qy, _ = path(nx, ny, dd, mm['R'], mp['R'], mm['P'], 1,
                         dict(tc=mm['T'], tp=mp['T'], Pp=mp['P']), 'full')
        r = float(np.median(np.hypot(px - ((qx * .5 + .5) * W + jx),
                                    py - ((.5 - qy * .5) * H + jy))[sel]))
        if a == 1.0:
            unity = r
        if r < best[1]:
            best = (float(a), r)
    return best + (unity,)


def camera_check(spec, scales=(2.0, 6.0, 10.0)):
    """Routed-truth residual of both camera paths over a burst, with radial and depth bins.

    SETA x N is synthesised by moving the previous camera N times further back along the same
    per-frame translation and reprojecting the SAME world points (recovered from the capture's depth):
    the corrected path stays exact by construction, so what grows is the residual the installed
    rotation-only path sees, which is what the gate closes on.
    """
    m = load_replay(spec['dump'], spec['frames'], spec['roi'])
    load, meta, frames = m['load'], m['meta'], m['frames']
    ys, xs, path = m['ys'], m['xs'], m['camera_previous_ndc']
    W, H = m['W'], m['H']
    fields = {'corrected': [], 'installed_rotation_only': [], 'screen': []}
    scaled = {n: [] for n in scales}
    ulp = {n: [] for n in (1.0,) + tuple(scales)}
    radius, depths, translations, fits = [], [], [], []
    shares = {'min_screen_corrected': [], 'min_screen_rotation': [], 'screen': []}
    for f in frames[1:]:
        mm, mp = meta[f], meta[f - 1]
        if 'T' not in mm or 'T' not in mp:
            continue
        d = load('depth', f, 'rgba32f', np.float32, 4)[ys, xs, 0]
        mv = load('motion', f, 'rgba32f', np.float32, 4)[ys, xs]
        jx, jy = mm['j']
        nx = 2 * (xs - jx) / W - 1
        ny = 1 - 2 * (ys - jy) / H
        cam = dict(tc=mm['T'], tp=mp['T'], Pp=mp['P'])
        Cc, Cp = camera_centre(mm['R'], mm['T']), camera_centre(mp['R'], mp['T'])
        translations.append(float(np.linalg.norm(Cc - Cp)))
        routed = mv[..., 3] == 1

        def screen(mode, c=cam, dd=d):
            qx, qy, _ = path(nx, ny, dd, mm['R'], mp['R'], mm['P'], 1, c, mode)
            return (qx * .5 + .5) * W + jx, (.5 - qy * .5) * H + jy
        nextd = np.nextafter(d, np.float32(1))  # one R32F step of the captured depth
        fx, fy = screen('full')
        rx, ry = screen('rotation')
        sel = routed & (d >= 0) & (d <= 1)
        px = np.where(routed, mv[..., 0] * W + jx - .5, xs.astype(float))
        py = np.where(routed, mv[..., 1] * H + jy - .5, ys.astype(float))
        sp = np.hypot(px - xs, py - ys)
        if sel.any():
            fields['corrected'].append(np.hypot(px - fx, py - fy)[sel])
            fields['installed_rotation_only'].append(np.hypot(px - rx, py - ry)[sel])
            fields['screen'].append(sp[sel])
            for n in (1.0,) + tuple(scales):
                back = dict(cam, tp=-(Cc + (Cp - Cc) * n) @ mp['R'])
                sx, sy = screen('full', back)
                if n in scaled:
                    scaled[n].append(np.hypot(sx - rx, sy - ry)[sel])
                ux, uy = screen('full', back, nextd)
                ulp[n].append(np.hypot(ux - sx, uy - sy)[sel])
            radius.append(np.hypot(xs - W / 2, ys - H / 2)[sel])
            depths.append(d[sel])
            if len(fits) < 5:  # the fit is stable frame to frame; five samples are enough
                fits.append(depth_scale_fit(path, nx, ny, d, mm, mp, px, py, sel, jx, jy, W, H))
        # Gate shares run on the whole crop: the 17x17 window reaches past the routed pixels.
        for label, (cx, cy) in (('min_screen_corrected', (fx, fy)), ('min_screen_rotation', (rx, ry))):
            rel = np.where(routed, np.hypot(px - cx, py - cy), 0.)
            shares[label].append(gate_open_share(np.minimum(sp, rel)))
        shares['screen'].append(gate_open_share(sp))
    cat = {k: np.concatenate(v) if v else np.array([]) for k, v in fields.items()}
    rad = np.concatenate(radius) if radius else np.array([])
    dep = np.concatenate(depths) if depths else np.array([])
    zv = np.array([]) if dep.size == 0 else FAR_P32 / (dep - FAR_P22)
    redges = np.percentile(rad, [0, 20, 40, 60, 80, 100]) if rad.size else [0] * 6
    dedges = np.percentile(zv, [0, 25, 50, 75, 100]) if zv.size else [0] * 5
    return {
        'dump': spec['dump'], 'frames': [frames[0], frames[-1]], 'roi': list(spec['roi']),
        'routed_pixel_frames': int(cat['corrected'].size),
        'camera_translation_world_units_per_frame': ([float(np.min(translations)),
                                                      float(np.max(translations))]
                                                     if translations else None),
        'residual_p50_p90_p99_px': {k: percentiles(v) for k, v in cat.items()},
        'corrected_residual_by_radial_bin': binned(cat['corrected'], rad, redges),
        'corrected_residual_by_depth_bin_view_z': binned(cat['corrected'], zv, dedges),
        'installed_residual_by_radial_bin': binned(cat['installed_rotation_only'], rad, redges),
        'installed_residual_by_depth_bin_view_z': binned(cat['installed_rotation_only'], zv, dedges),
        'seta_scaled_installed_residual_p50_p90_p99_px': {
            str(int(n)): percentiles(np.concatenate(v)) for n, v in scaled.items() if v},
        # What the CORRECTED path cannot beat under SETA x N: one R32F step of the captured depth,
        # reprojected with the scaled translation. It is the only term of the corrected residual that
        # grows with the camera translation.
        'seta_scaled_corrected_depth_step_residual_p50_p90_p99_px': {
            str(int(n)): percentiles(np.concatenate(v)) for n, v in ulp.items() if v},
        # [best scale, its per-frame median residual, the residual at scale 1]. A burst without
        # camera translation is insensitive to the scale and the fit then means nothing.
        'best_fit_view_z_scale_and_residual_p50_px': ([float(np.median([f[0] for f in fits])),
                                                       float(np.median([f[1] for f in fits])),
                                                       float(np.median([f[2] for f in fits]))]
                                                      if fits else None),
        'gate_open_share_and_mean_openness': {
            k: [float(np.mean([a for a, _ in v])), float(np.mean([b for _, b in v]))]
            for k, v in shares.items() if v},
    }

def verdicts(summary):
    """Apply section 6's acceptance to the measured numbers.

    The decision sequence is run177 rotation: it is the only burst whose registration is exact
    (routed fit p99 0.0024 px) and whose camera-relative gate actually opens. run161 supplies the
    background trail, run177 stationary and run159 slow the bit-identity controls.

    The stale-history patch is reported, not gated on: the user has since accepted a little
    ghosting if the crawl goes, so ghost magnitude is a trade-off beside the tracked-rms gain,
    and variants are ranked by that gain.
    """
    seq = summary['sequences']
    acc = summary['acceptance']
    out = {}
    decide = seq.get('run177-rotation')
    trail = seq.get('run161-trail')
    controls = [n for n, s in seq.items() if s['role'] == 'control']
    for key in VARIANTS:
        rec = {}
        if decide is not None:
            v = decide['variants'][key]
            if v['tracked_rms_ratio'] is not None:
                rec['tracked_rms'] = v['tracked_rms_ratio'] <= acc['tracked_rms_ratio_max']
                rec['tracked_gradient'] = (v['tracked_gradient_ratio']
                                           >= acc['tracked_gradient_ratio_min'])
                rec['tracked_rms_ratio'] = v['tracked_rms_ratio']
            if 'stale_patch' in decide:
                stale = decide['stale_patch']['by_variant'][key]['added_codes_at_site']
                rec['stale_patch_codes'] = stale
                rec['stale_patch_within_2x_clipped'] = stale <= acc['stale_patch_codes_max']
        if trail is not None:
            rec['run161_trail'] = (trail['variants'][key]['background_trail_p99_vs_plain']
                                   <= trail['variants']['installed']['background_trail_p99_vs_plain']
                                   + 1e-9)
        if controls:
            rec['controls_bit_identical'] = all(
                seq[n]['variants'][key].get('identical_to_installed', True) for n in controls)
        gated = ('tracked_rms', 'tracked_gradient', 'run161_trail', 'controls_bit_identical')
        rec['pass'] = all(rec[f] for f in gated if f in rec)
        out[key] = rec
    if decide is not None:
        out['_ranking_by_tracked_rms_gain'] = sorted(
            (k for k in VARIANTS), key=lambda k: out[k].get('tracked_rms_ratio', 1.0))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--output', type=Path,
                    default=ROOT / 'verification/results/lattice-gate-replay-2026-09-21.json')
    ap.add_argument('--png-dir', type=Path, default=Path('/tmp/x3-lattice-gate-replay'))
    ap.add_argument('--sequences', default=','.join(SEQUENCES))
    ap.add_argument('--camera-check', action='store_true',
                    help='routed-truth camera-path residual, bins and SETA scaling only (no resolve)')
    args = ap.parse_args(argv)
    args.png_dir.mkdir(parents=True, exist_ok=True)
    summary = {'generated': '2026-09-21', 'units': 'display luma codes after AgX+RCAS, no bloom',
               'history_cut_heuristics': 'not modelled (both off, Run57 launcher defaults)',
               'thin_region': THIN, 'history_kernel': 'Catmull-Rom (Keys -0.5)',
               'acceptance': {'tracked_rms_ratio_max': 0.80, 'tracked_gradient_ratio_min': 0.90,
                              'stale_patch_codes_max': 2 * CLIPPED_RESOLVE_CODES},
               'sequences': {}}
    if args.camera_check:
        summary['camera_path'] = ('rotation-only = the matrix the installed build uploads'
                                  ' (camera_far_plane_reprojection); corrected = depth- and'
                                  ' translation-aware, validated against the routed motion RG')
        for name in args.sequences.split(','):
            start = time.monotonic()
            summary['sequences'][name] = camera_check(SEQUENCES[name])
            print(name, 'camera check in %.1f s' % (time.monotonic() - start), flush=True)
            args.output.write_text(json.dumps(summary, indent=1) + '\n')
        return summary
    for name in args.sequences.split(','):
        start = time.monotonic()
        summary['sequences'][name] = run_sequence(name, SEQUENCES[name], args.png_dir)
        print(name, 'done in %.1f s' % (time.monotonic() - start), flush=True)
        summary['verdicts'] = verdicts(summary)
        args.output.write_text(json.dumps(summary, indent=1) + '\n')
    return summary


if __name__ == '__main__':
    main()
