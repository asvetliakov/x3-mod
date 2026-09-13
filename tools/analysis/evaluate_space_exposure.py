#!/usr/bin/env python3
"""Offline policy study on one logged post-TAA FP16 frame per F8 burst.

Requires NumPy. Does not modify production policy or run the game. The capture
is engine-encoded, unweighted, unexposed RGB; decode gamma 2.2 exactly once.
See docs/architecture/space-exposure-policy.md for the proved boundary and
limitations. Candidate constants below are experiments, not renderer defaults.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re

import numpy as np

import exposure_reference as reference
import agx_reference as agx


def reduce_tiles(logs):
    """Vectorized reference 4x4 edge-clamped mean/max chain, including level 0."""
    means, maxima = np.asarray(logs, dtype=float), np.asarray(logs, dtype=float)
    first = True
    while first or max(means.shape) > reference.TILE_MAX:
        first = False
        h, w = means.shape
        pad = ((0, (-h) % 4), (0, (-w) % 4))
        shape = ((h + 3) // 4, 4, (w + 3) // 4, 4)
        means = np.pad(means, pad, mode='edge').reshape(shape).mean(axis=(1, 3))
        maxima = np.pad(maxima, pad, mode='edge').reshape(shape).max(axis=(1, 3))
    return means, maxima


def smoothstep(value, low=.01, high=.10):
    t = min(1., max(0., (value - low) / (high - low)))
    return t * t * (3. - 2. * t)


def restrained_target(stats):
    """R1: a dim authored-content anchor, quarter-strength, +/- half a stop.

    Smooth confidence removes the hard 1%-lit branch. The dim anchor .01 is
    an artistic hypothesis, not a recovered light calibration. A separate
    quarter-strength p99-max guard has no positive lift of its own.
    """
    confidence = smoothstep(stats['lit_fraction'])
    correction = .25 * (math.log2(.01) - stats['lit_median_log'])
    key = confidence * min(.5, max(-.5, correction))
    guard = min(0., max(-.5, .25 * (math.log2(.9) - stats['p99_max_log'])))
    return min(key, guard) if guard < 0 else key


def broad_highlight_target(means):
    """R2: no positive lift; gently darken broad bright fields, at most .5 EV.

    p95 of tile geometric means ignores spatially sparse bright pixels. This
    may still dim an intentionally large bright planet or nebula; it is an
    alternative to test, not automatically the correct artistic treatment.
    """
    values = np.sort(np.asarray(means).ravel())
    p95 = values[min(len(values) - 1, len(values) * 95 // 100)]
    return min(0., max(-.5, .25 * (math.log2(.18) - float(p95))))


def statistics(logs):
    means, maxima = reduce_tiles(logs)
    h, w = means.shape
    stats = reference.meter_statistics(means.ravel().tolist(), maxima.ravel().tolist(),
                                       weights=reference.tile_weights(w, h))
    return stats, means, maxima


def targets(stats, means):
    return {'current': reference.ev_target(stats), 'fixed0': 0.,
            'restrained': restrained_target(stats), 'broad_highlight': broad_highlight_target(means)}


def display_rgb(linear, ev):
    """AgX look=none vectorization; exported for independent scalar checks."""
    inset = matrix_rgb(linear * 2. ** ev, agx.M_IN)
    log = np.clip((np.log2(np.maximum(inset, agx.LOG_FLOOR)) - agx.MIN_EV)
                  / (agx.MAX_EV - agx.MIN_EV), 0, 1)
    curved = np.maximum(np.polyval(agx.CONTRAST_COEFFICIENTS, log), 0)
    return np.clip(matrix_rgb(curved, agx.M_OUT), 0, 1)


def luma_rgb(rgb):
    return sum(rgb[..., i] * agx.LUMA_WEIGHTS[i] for i in range(3))


def matrix_rgb(rgb, matrix):
    # Explicit channels also avoid platform BLAS floating-point status noise.
    return np.stack([sum(rgb[..., i] * row[i] for i in range(3)) for row in matrix], axis=-1)


def quantiles(values):
    if not len(values):
        return None
    return dict(zip(('p50', 'p90', 'p95', 'p99', 'max'),
                    map(float, np.quantile(values, [.5, .9, .95, .99, 1]))))


def capture_rows(snapshot):
    paths = sorted(snapshot.glob('taa_1_*.rgba16f'), key=lambda p: int(p.stem.rsplit('_', 1)[1]))
    frames = [int(p.stem.rsplit('_', 1)[1]) for p in paths]
    selected = {frame for i, frame in enumerate(frames) if i == 0 or frame != frames[i-1] + 1}
    records = {}
    log, = snapshot.glob('*.log')
    with log.open() as stream:
        for line in stream:
            kind = line.split(' ', 1)[0]
            if kind not in ('hdr_frame', 'motion_output_frame', 'motion_output_taa_readback', 'motion_output_depth_readback'):
                continue
            fields = dict(re.findall(r'(\w+)=(\S+)', line))
            frame = int(fields.get('frame', -1))
            if frame in selected and fields.get('device') == '1':
                records.setdefault(frame, {})[kind] = fields
    rows = []
    for frame in sorted(selected):
        context = records[frame]
        record, hdr, motion = (context[k] for k in ('motion_output_taa_readback', 'hdr_frame', 'motion_output_frame'))
        assert record['result'] == '00000000' and record['format'] == 'rgba16f_row_major'
        assert hdr['decode'] == 'gamma2.2' and hdr['look'] == 'none' and hdr['tonemapped'] == '1'
        assert motion['taa_resolved'] == '1' and motion['taa_hdr'] == '1'
        w, h = int(record['width']), int(record['height'])
        path = snapshot / record['file']
        assert path.name == f'taa_1_{frame}.rgba16f' and path.stat().st_size == int(record['bytes']) == w*h*8
        raw = path.read_bytes()
        rgb = np.frombuffer(raw, dtype='<f2').reshape(h, w, 4)[..., :3].astype(float)
        assert np.isfinite(rgb).all()
        linear = np.maximum(rgb, 0) ** 2.2
        luma = luma_rgb(linear)
        logs = np.log2(np.clip(luma, reference.METER_FLOOR, reference.METER_CLIP))
        stats, means, maxima = statistics(logs)
        depth_record = context['motion_output_depth_readback']
        assert depth_record['result'] == '00000000' and depth_record['file'] == f'depth_1_{frame}.r32f'
        depth = np.fromfile(snapshot / depth_record['file'], dtype='<f4').reshape(h, w)
        assert np.isfinite(depth).all()
        valid_depth = depth >= 0
        evs = targets(stats, means)
        displays = {}
        for policy, ev in evs.items():
            mapped = display_rgb(linear, ev)
            displays[policy] = {'ev': ev, 'luma': quantiles(luma_rgb(mapped).ravel()),
                                'rgb_ge_099_fraction': float(np.mean(np.max(mapped, axis=2) >= .99))}
        rows.append({'snapshot': snapshot.name, 'frame': frame, 'file': path.name,
                     'sha256': hashlib.sha256(raw).hexdigest(), 'width': w, 'height': h,
                     'statistics': stats, 'targets': evs, 'decoded_luma': quantiles(luma.ravel()),
                     'valid_depth_fraction': float(valid_depth.mean()),
                     'valid_depth_luma': quantiles(luma[valid_depth]),
                     'sentinel_luma': quantiles(luma[~valid_depth]),
                     'display': displays,
                     'lagged_logged_statistics': {k: float(hdr[k]) for k in
                        ('lit_fraction', 'luma_lit', 'luma_p99', 'avg_log_l', 'ev')}})
    return rows


def synthetic_rows():
    """Counterexamples in decoded luminance, not hypothetical physical units."""
    result = []
    for name, kind in [('black', 'black'), ('dim_nebula', 'nebula'),
                       ('one_star_per_tile', 'stars'), ('broad_white_planet', 'planet'),
                       ('small_flash', 'flash'), ('lit_0.9_percent', 'low'), ('lit_1.1_percent', 'high')]:
        pixels = np.full((768, 1280), 1e-4)
        if kind == 'nebula': pixels[:] = .01
        if kind == 'stars': pixels[::16, ::16] = 64
        if kind == 'planet': pixels[:, :640] = 1
        if kind == 'flash': pixels[160:192, 160:192] = 64
        if kind in ('low', 'high'):
            # Exact whole 16x16 tiles below/above the installed 1% count gate.
            count = 34 if kind == 'low' else 43
            for n in range(count): pixels[:16, n*16:(n+1)*16] = .01
        stats, means, maxima = statistics(np.log2(pixels))
        result.append({'name': name, 'lit_fraction': stats['lit_fraction'],
                       'p99_tile_max': 2.**stats['p99_max_log'], 'targets': targets(stats, means)})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshots', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = {'scope': 'Offline current-image targets and AgX transforms; not a temporal rerender or gameplay acceptance.',
              'captures': [row for snapshot in args.snapshots for row in capture_rows(snapshot)],
              'synthetic': synthetic_rows()}
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')


if __name__ == '__main__':
    main()
