#!/usr/bin/env python3
"""Host side of the sun-shadow apply quad fixture (docs/architecture/
legacy-sun-application.md, section 3.3; the "sunapply" mode of
motion_output_fixture.cpp). No Wine, no game.

* `expected_factor`: the CPU twin of src/temporal/sun_shadow_apply_ps.hlsl in
  float64 (AO linearization, view position, the frame's view -> sun rows,
  texel-snapped 3x3 PCF with the rotated kernel, receiver-plane and constant
  bias, exponent), plus an "ambiguous" mask: pixels whose result depends on a
  rounding call the GPU makes differently (a tap or the receiver within
  EPS_TEXEL of a texel boundary, a compare within EPS_DEPTH of equality, a
  2x2 quad across a depth discontinuity where dsx/dsy are not a plane fit,
  fine-versus-coarse derivatives changing the outcome). Ambiguous pixels are
  reported, never compared strictly.
* `analytic_shadow`: the hard shadow of the fixture's box on its plane along
  the sun, and the distance in map texels to the nearest classification
  change, for the edge tolerance.
* `compare_frame`: the readbacks against C * factor within one FP16 code.
"""
import math
import struct

EPS_TEXEL = 2e-3     # texel-boundary ambiguity of floor() between float32 (GPU) and float64
EPS_DEPTH = 1e-4     # compare-equality ambiguity in normalized sun depth
KERNEL = [(math.cos(k * math.pi / 8), math.sin(k * math.pi / 8)) for k in range(8)]


def unpack_rt2(data, width, height):
    """G32R32F bytes -> (d, s) float64 arrays."""
    import numpy as np
    values = np.frombuffer(data, dtype='<f4').astype(np.float64).reshape(height, width, 2)
    return values[..., 0], values[..., 1]


def unpack_map(data, size):
    import numpy as np
    return np.frombuffer(data, dtype='<f4').astype(np.float64).reshape(size, size)


def unpack_rgba16f(data, width, height):
    import numpy as np
    return np.frombuffer(data, dtype='<f2').astype(np.float64).reshape(height, width, 4)


def fp16_code(value):
    """The spacing of FP16 codes at |value| (subnormal spacing below 2^-14)."""
    import numpy as np
    magnitude = np.abs(value)
    exponent = np.floor(np.log2(np.maximum(magnitude, 2.0 ** -14)))
    return np.power(2.0, exponent - 10)


def _quad_derivative(value, axis):
    """Fine derivative along `axis` on 2x2 quads: partner minus self with the
    quad's sign (right - left, bottom - top) for both members."""
    import numpy as np
    swapped = value.copy()
    if axis == 1:
        swapped[:, 0::2], swapped[:, 1::2] = value[:, 1::2], value[:, 0::2]
    else:
        swapped[0::2, :], swapped[1::2, :] = value[1::2, :], value[0::2, :]
    sign = np.ones_like(value)
    if axis == 1:
        sign[:, 1::2] = -1.0
    else:
        sign[1::2, :] = -1.0
    return (swapped - value) * sign


def _coarse(value, axis):
    """Coarse derivative: the quad's top row (ddx) or left column (ddy) for all four members."""
    fine = _quad_derivative(value, axis)
    coarse = fine.copy()
    if axis == 1:
        coarse[1::2, :] = fine[0::2, :]
    else:
        coarse[:, 1::2] = fine[:, 0::2]
    return coarse


def expected_factor(d, s, sun_map, params, coarse=False):
    """Per-pixel factor and the ambiguity mask. `params`: m00 m11 m20 m21
    m22 m32 rows(12) jitter_index exponent bias_constant bias_max planar_step."""
    import numpy as np
    height, width = d.shape
    size = sun_map.shape[0]
    m00, m11, m20, m21, m22, m32 = (params[k] for k in ('m00', 'm11', 'm20', 'm21', 'm22', 'm32'))
    rows = params['rows']
    z = m32 / (d - m22)
    i = np.arange(width, dtype=np.float64)[None, :].repeat(height, 0)
    j = np.arange(height, dtype=np.float64)[:, None].repeat(width, 1)
    ndc_x = (i + .5) / width * 2.0 - 1.0
    ndc_y = 1.0 - (j + .5) / height * 2.0
    px = (ndc_x - m20) * z / m00
    py = (ndc_y - m21) * z / m11
    sun = [rows[r * 4] * px + rows[r * 4 + 1] * py + rows[r * 4 + 2] * z + rows[r * 4 + 3] for r in range(3)]
    mu, mv = sun[0] * .5 + .5, .5 - sun[1] * .5
    derivative = _coarse if coarse else _quad_derivative
    dudx, dudy = derivative(mu, 1), derivative(mu, 0)
    dvdx, dvdy = derivative(mv, 1), derivative(mv, 0)
    dzdx, dzdy = derivative(sun[2], 1), derivative(sun[2], 0)
    dwx, dwy = derivative(z, 1), derivative(z, 0)
    det = dudx * dvdy - dvdx * dudy
    step = np.abs(dwx) + np.abs(dwy)
    planar = (np.abs(det) > 1e-12) & (step < params['planar_step'] * np.abs(z))
    inv = np.where(planar, 1.0 / np.where(planar, det, 1.0), 0.0)
    gu = (dzdx * dvdy - dzdy * dvdx) * inv
    gv = (dzdy * dudx - dzdx * dudy) * inv
    valid = (d >= 0.0) & (s > 0.0) & (mu >= 0.0) & (mu <= 1.0) & (mv >= 0.0) & (mv <= 1.0) & (sun[2] >= 0.0) & (sun[2] <= 1.0)
    texel_u, texel_v = np.floor(mu * size), np.floor(mv * size)
    ambiguous = np.zeros(d.shape, dtype=bool)
    near_boundary = lambda x: np.minimum(x - np.floor(x), np.ceil(x) - x) < EPS_TEXEL
    ambiguous |= near_boundary(mu * size) | near_boundary(mv * size)
    ambiguous |= (np.abs(mu) < EPS_TEXEL / size) | (np.abs(mu - 1) < EPS_TEXEL / size) | (np.abs(mv) < EPS_TEXEL / size) | (np.abs(mv - 1) < EPS_TEXEL / size)
    ambiguous |= (np.abs(sun[2]) < EPS_DEPTH) | (np.abs(sun[2] - 1) < EPS_DEPTH)
    # A quad whose depth step is near the planar threshold, or across a sentinel, has no reliable plane fit.
    ambiguous |= np.abs(step - params['planar_step'] * np.abs(z)) < .25 * params['planar_step'] * np.abs(z)
    cos_r, sin_r = KERNEL[int(params['jitter_index']) % 8]
    lit = np.zeros(d.shape, dtype=np.float64)
    for kj in (-1, 0, 1):
        for ki in (-1, 0, 1):
            ou, ov = cos_r * ki - sin_r * kj, sin_r * ki + cos_r * kj
            raw_u, raw_v = texel_u + .5 + ou, texel_v + .5 + ov
            if abs(ou - round(ou)) > 1e-9 or abs(ov - round(ov)) > 1e-9:
                ambiguous |= near_boundary(raw_u) | near_boundary(raw_v)
            tap_u, tap_v = np.floor(raw_u), np.floor(raw_v)
            tap_uv_u, tap_uv_v = (tap_u + .5) / size, (tap_v + .5) / size
            bias = np.where(planar, np.clip((tap_uv_u - mu) * gu + (tap_uv_v - mv) * gv, -params['bias_max'], params['bias_max']), -params['bias_max'])
            reference = sun[2] + bias - params['bias_constant']
            iu = np.clip(tap_u, 0, size - 1).astype(np.int64)
            iv = np.clip(tap_v, 0, size - 1).astype(np.int64)
            sampled = sun_map[iv, iu]
            ambiguous |= np.abs(sampled - reference) < EPS_DEPTH
            lit += (sampled >= reference)
    f = lit / 9.0
    base = np.clip(1.0 - (1.0 - f) * np.clip(s, 0.0, 1.0), 0.0, 1.0)
    shadowed = np.where(base > 0.0, np.power(np.maximum(base, 1e-30), params['exponent']), 0.0)
    factor = np.where(valid & (f < 1.0), shadowed, 1.0)
    # The derivative convention must not change the outcome.
    if not coarse:
        other = expected_factor(d, s, sun_map, params, coarse=True)
        ambiguous |= other['factor'] != factor
    # Sentinel members make a quad's derivatives meaningless for its other
    # members (a share-free receiver still has a depth, so its quad is fine).
    sentinel = d < 0.0
    quad_sentinel = sentinel.copy()
    quad_sentinel[:, 0::2] |= sentinel[:, 1::2]; quad_sentinel[:, 1::2] |= sentinel[:, 0::2]
    quad_sentinel[0::2, :] |= quad_sentinel[1::2, :].copy(); quad_sentinel[1::2, :] |= quad_sentinel[0::2, :].copy()
    ambiguous |= quad_sentinel & valid
    return {'factor': factor, 'f': f, 'valid': valid, 'ambiguous': ambiguous & valid, 'sun': sun, 'z': z}


def _hit(o, direction, box_min, box_max):
    """Nearest positive hit of the plane y = 0 and the box, or -1 (the fixture's law)."""
    import numpy as np
    best = np.full(o[0].shape, -1.0)
    plane = (direction[1] < 0.0) & (o[1] > 0.0)
    best = np.where(plane, -o[1] / np.where(direction[1] != 0.0, direction[1], 1.0), best)
    enter = np.full(o[0].shape, -1e30); leave = np.full(o[0].shape, 1e30)
    outside = np.zeros(o[0].shape, dtype=bool)
    for k in range(3):
        parallel = np.abs(direction[k]) < 1e-12
        safe = np.where(parallel, 1.0, direction[k])
        t0 = (box_min[k] - o[k]) / safe; t1 = (box_max[k] - o[k]) / safe
        lo, hi = np.minimum(t0, t1), np.maximum(t0, t1)
        enter = np.where(parallel, enter, np.maximum(enter, lo)); leave = np.where(parallel, leave, np.minimum(leave, hi))
        outside |= parallel & ((o[k] < box_min[k]) | (o[k] > box_max[k]))
    hit = (~outside) & (enter <= leave) & (leave > 0.0)
    t = np.where(enter > 0.0, enter, leave)
    return np.where(hit & ((best < 0.0) | (t < best)), t, best)


def analytic_shadow(d, s, params, scene, size):
    """Hard shadow (1 lit, 0 shadowed) of every receiver pixel and the
    smallest sun-space offset in texels (1, 2 or 0 = none within 2) at which
    the classification changes."""
    import numpy as np
    height, width = d.shape
    m00, m11, m20, m21, m22, m32 = (params[k] for k in ('m00', 'm11', 'm20', 'm21', 'm22', 'm32'))
    z = m32 / (d - m22)
    i = np.arange(width, dtype=np.float64)[None, :].repeat(height, 0)
    j = np.arange(height, dtype=np.float64)[:, None].repeat(width, 1)
    ndc_x = (i + .5) / width * 2.0 - 1.0; ndc_y = 1.0 - (j + .5) / height * 2.0
    dv = ((ndc_x - m20) / m00, (ndc_y - m21) / m11, np.ones_like(z))
    axes = (scene['cam_right'], scene['cam_up'], scene['cam_forward'])
    world = [scene['camera'][c] + z * sum(axes[a][c] * dv[a] for a in range(3)) for c in range(3)]
    sun = scene['sun']
    texel = 2.0 * scene['extent'] / size
    box_min, box_max = scene['box'][:3], scene['box'][3:]
    def lit_at(offset_u, offset_v):
        o = [world[c] + (scene['right'][c] * offset_u + scene['up'][c] * offset_v) * texel + sun[c] * 1e-4 for c in range(3)]
        t = _hit(o, [np.full(z.shape, sun[c]) for c in range(3)], box_min, box_max)
        return t <= 0.0
    lit = lit_at(0.0, 0.0)
    distance = np.zeros(d.shape, dtype=np.int64)
    for radius in (2, 1):
        change = np.zeros(d.shape, dtype=bool)
        for du in (-radius, 0, radius):
            for dvv in (-radius, 0, radius):
                if du or dvv:
                    change |= lit_at(float(du), float(dvv)) != lit
        distance = np.where(change, radius, distance)
    return {'lit': lit, 'edge_distance': distance}


def compare_frame(before, after, d, s, sun_map, params, scene=None):
    """The readbacks (H x W x 4 float64 from FP16) against before * factor.
    Returns counts: compared pixels, violations beyond one FP16 code, the
    worst error in codes, identity pixels that changed, ambiguous pixels, and
    (with `scene`) the analytic edge record."""
    import numpy as np
    expected = expected_factor(d, s, sun_map, params)
    factor, valid, ambiguous = expected['factor'], expected['valid'], expected['ambiguous']
    strict = valid & ~ambiguous
    reference = before[..., :3] * factor[..., None]
    codes = np.abs(after[..., :3] - reference) / fp16_code(reference)
    identity = (factor == 1.0)
    identity_changed = int(np.count_nonzero(identity & np.any(after != before, axis=-1)))
    worst = float(np.max(np.where(strict[..., None], codes, 0.0))) if np.any(strict) else 0.0
    violations = int(np.count_nonzero(strict & np.any(codes > 1.0 + 1e-9, axis=-1)))
    alpha_changed = int(np.count_nonzero(after[..., 3] != before[..., 3]))
    record = {'valid': int(np.count_nonzero(valid)), 'compared': int(np.count_nonzero(strict)), 'ambiguous': int(np.count_nonzero(ambiguous)),
              'violations': violations, 'worst_codes': worst, 'identity_changed': identity_changed, 'alpha_changed': alpha_changed,
              'shadowed': int(np.count_nonzero(strict & (expected['f'] < 1.0))), 'penumbra': int(np.count_nonzero(strict & (expected['f'] > 0.0) & (expected['f'] < 1.0))),
              'ok': violations == 0 and identity_changed == 0 and alpha_changed == 0}
    if scene is not None:
        analytic = analytic_shadow(d, s, params, scene, sun_map.shape[0])
        hard = expected['f'] >= 1.0
        mismatch = strict & (hard != analytic['lit'])
        far = mismatch & (analytic['edge_distance'] == 0)
        within_one = mismatch & (analytic['edge_distance'] == 1)
        record['analytic'] = {'mismatch': int(np.count_nonzero(mismatch)), 'within_one_texel': int(np.count_nonzero(within_one)),
                              'within_two_texels': int(np.count_nonzero(mismatch & (analytic['edge_distance'] == 2))),
                              'beyond_two_texels': int(np.count_nonzero(far)), 'shadowed_analytic': int(np.count_nonzero(strict & ~analytic['lit']))}
        record['ok'] = record['ok'] and record['analytic']['beyond_two_texels'] == 0
    return record


def parse_params(fields):
    """The fixture's SUNAPPLY line fields -> params and scene dicts."""
    triple = lambda key: tuple(float(v) for v in fields[key].split(','))
    params = {k: float(fields[k]) for k in ('m00', 'm11', 'm20', 'm21', 'm22', 'm32', 'exponent', 'bias_constant', 'bias_max', 'planar_step')}
    params['rows'] = tuple(float(v) for v in fields['rows'].split(','))
    params['jitter_index'] = int(fields['jitter_index'])
    scene = {k: triple(k) for k in ('camera', 'cam_right', 'cam_up', 'cam_forward', 'sun', 'right', 'up', 'forward', 'center')}
    scene['extent'] = float(fields['extent']); scene['depth_half'] = float(fields['depth_half'])
    scene['box'] = tuple(float(v) for v in fields['box'].split(','))
    return params, scene


def half_bytes(values):
    """Float sequence -> little-endian FP16 bytes (tests)."""
    return struct.pack('<%de' % len(values), *values)


# ---- the twin on a run's F8 capture (docs/verification/directional-shadows.md,
# "Run 36 session B (run106)"): the pass's inputs come from the session log, the
# RT2, map and HDR dumps from the capture directory. No Wine, no game.
PRODUCTION_M22, PRODUCTION_M32 = 1.000003, -6.0000184   # ao_default_m22/m32 (motion_output.cpp)
PRODUCTION_BIAS = dict(bias_constant=.001, bias_max=.01, planar_step=.05)  # SunShadowApplyParams defaults
LINE_FIELDS = __import__('re').compile(r'(\w+)=([^\s]+)')


def line_fields(line):
    return dict(LINE_FIELDS.findall(line))


def parse_apply_params(fields):
    """A `sun_shadow_apply_params` line (capture frames; motion_output.cpp
    run_sun_shadow_apply) -> the params dict of expected_factor plus the
    frame's raster size, map size and pixel jitter."""
    params = {k: float(fields[k]) for k in ('m00', 'm11', 'm20', 'm21', 'm22', 'm32', 'exponent', 'bias_max', 'planar_step')}
    params['bias_constant'] = float(fields['bias'])
    params['rows'] = tuple(float(v) for v in fields['rows'].split(','))
    params['jitter_index'] = int(fields['jitter_index'])
    if len(params['rows']) != 12:
        raise ValueError('rows: expected 12 values, got %d' % len(params['rows']))
    extra = dict(width=int(fields['width']), height=int(fields['height']), map=int(fields['map']),
                 jitter_px=(float(fields['jitter_x']), float(fields['jitter_y'])), texel=float(fields['texel']))
    if extra['map'] and abs(extra['texel'] * extra['map'] - 1.0) > 1e-6:
        raise ValueError('texel %g does not match map %d' % (extra['texel'], extra['map']))
    return params, extra


def reconstruct_params(camera, frame_line, basis, width, height):
    """Before the params line existed (run106): the pass's inputs from the
    `camera_state` line (p00/p11/p20/p21), the `motion_output_frame` line
    (jitter in pixels, jitter_index) and the `shadow_replay_map_basis` rows,
    with the production constants. The jitter enters m20/m21 in NDC as the
    fixed run_sun_shadow_apply does (+2 jx / width, -2 jy / height)."""
    jx, jy = float(frame_line['jitter_x']), float(frame_line['jitter_y'])
    params = dict(m00=float(camera['p00']), m11=float(camera['p11']),
                  m20=float(camera['p20']) + 2.0 * jx / width, m21=float(camera['p21']) - 2.0 * jy / height,
                  m22=PRODUCTION_M22, m32=PRODUCTION_M32, exponent=1.0, jitter_index=int(frame_line['jitter_index']),
                  rows=tuple(float(v) for v in basis['rows'].split(',')), **PRODUCTION_BIAS)
    if len(params['rows']) != 12:
        raise ValueError('basis rows: expected 12 values')
    return params


def frame_params(log_path, frame, device=1):
    """The pass's inputs of one capture frame from the session log: the
    `sun_shadow_apply_params` line when present, otherwise reconstructed.
    Returns (params, extra, source) with source 'params_line' or 'reconstructed'."""
    key = ' device=%d frame=%d ' % (device, frame)
    found = {}
    with open(log_path, errors='replace') as handle:
        for line in handle:
            for tag in ('sun_shadow_apply_params', 'camera_state', 'motion_output_frame', 'shadow_replay_map_basis', 'motion_output_depth_readback'):
                if line.startswith(tag) and key in line:
                    found[tag] = line_fields(line)
    if 'sun_shadow_apply_params' in found:
        params, extra = parse_apply_params(found['sun_shadow_apply_params'])
        return params, extra, 'params_line'
    for tag in ('camera_state', 'motion_output_frame', 'shadow_replay_map_basis', 'motion_output_depth_readback'):
        if tag not in found:
            raise ValueError('frame %d: no %s line' % (frame, tag))
    width, height = int(found['motion_output_depth_readback']['width']), int(found['motion_output_depth_readback']['height'])
    basis = found['shadow_replay_map_basis']
    params = reconstruct_params(found['camera_state'], found['motion_output_frame'], basis, width, height)
    extra = dict(width=width, height=height, map=int(basis['size']),
                 jitter_px=(float(found['motion_output_frame']['jitter_x']), float(found['motion_output_frame']['jitter_y'])), texel=1.0 / int(basis['size']))
    return params, extra, 'reconstructed'


def load_capture(directory, device, frame, width, height, size):
    """The frame's RT2 (d, s), map and, when present, HDR luminance."""
    from pathlib import Path
    base = Path(directory)
    d, s = unpack_rt2((base / ('depth_%d_%d.rg32f' % (device, frame))).read_bytes(), width, height)
    sun_map = unpack_map((base / ('shadow_map_%d_%d.r32f' % (device, frame))).read_bytes(), size)
    hdr = base / ('hdr_%d_%d.rgba16f' % (device, frame))
    luminance = None
    if hdr.exists():
        rgba = unpack_rgba16f(hdr.read_bytes(), width, height)
        luminance = .2126 * rgba[..., 0] + .7152 * rgba[..., 1] + .0722 * rgba[..., 2]
    return d, s, sun_map, luminance


def frame_report(d, s, sun_map, params, luminance=None, region=None, columns=64, lines=24):
    """The twin on real inputs: receiver-minus-map residual on valid pixels
    (nearest texel, no bias), the factor statistics, the HDR darkening of
    shadowed pixels against same-surface lit neighbours within 4 px (and the
    lit-lit control), and an ASCII mask of `region` (y0, y1, x0, x1) or the
    valid bounding box: '#' factor < .6, '+' < .85, '-' < .999, '.' 1."""
    import numpy as np
    out = expected_factor(d, s, sun_map, params)
    factor, f, valid, ambiguous, sun, z = out['factor'], out['f'], out['valid'], out['ambiguous'], out['sun'], out['z']
    size = sun_map.shape[0]
    mu, mv = sun[0] * .5 + .5, .5 - sun[1] * .5
    tu = np.clip(np.floor(np.nan_to_num(mu) * size).astype(np.int64), 0, size - 1)
    tv = np.clip(np.floor(np.nan_to_num(mv) * size).astype(np.int64), 0, size - 1)
    difference = sun[2] - sun_map[tv, tu]
    covered = valid & (sun_map[tv, tu] < 1.0)
    report = {'valid': int(valid.sum()), 'ambiguous': int(ambiguous.sum()), 'covered': int(covered.sum())}
    if valid.any():
        residual = difference[valid]
        report.update(residual_p05_p25_p50_p75_p95=[float(v) for v in np.percentile(residual, [5, 25, 50, 75, 95])],
                      residual_std=float(residual.std()), residual_std_covered=float(difference[covered].std()) if covered.any() else None,
                      f_below_0_9=float((f[valid] < .9).mean()), factor_mean=float(factor[valid].mean()),
                      factor_below_0_7=float((factor[valid] < .7).mean()))
    if luminance is not None:
        good = valid & ~ambiguous
        report['darkening'] = {}
        for label, select in (('shadowed', good & (f <= 3.0 / 9 + 1e-9)), ('control', good & (f == 1.0))):
            ratio = np.full(d.shape, np.nan); predicted = np.full(d.shape, np.nan)
            for dy in range(-4, 5):
                for dx in range(-4, 5):
                    if dx == 0 and dy == 0:
                        continue
                    shifted = lambda a: np.roll(np.roll(a, dy, 0), dx, 1)
                    ok = select & shifted(good) & (shifted(f) == 1.0) & (np.abs(shifted(z) - z) < .01 * np.abs(z)) & (np.abs(shifted(s) - s) < .05) & np.isnan(ratio)
                    ratio[ok] = luminance[ok] / np.maximum(shifted(luminance)[ok], 1e-6); predicted[ok] = factor[ok]
            have = ~np.isnan(ratio)
            if have.any():
                report['darkening'][label] = dict(pairs=int(have.sum()), ratio_p25_p50_p75=[float(v) for v in np.percentile(ratio[have], [25, 50, 75])],
                                                  predicted_p25_p50_p75=[float(v) for v in np.percentile(predicted[have], [25, 50, 75])])
    if region is None and valid.any():
        ys, xs = np.nonzero(valid)
        region = (int(ys.min()), int(ys.max()) + 1, int(xs.min()), int(xs.max()) + 1)
    mask = []
    if region is not None:
        y0, y1, x0, x1 = region
        block = np.where(valid, factor, np.nan)[y0:y1, x0:x1]
        bh, bw = max(1, block.shape[0] // lines), max(1, block.shape[1] // columns)
        for j in range(0, block.shape[0], bh):
            row = ''
            for i in range(0, block.shape[1], bw):
                cell = block[j:j + bh, i:i + bw]
                if np.all(np.isnan(cell)):
                    row += ' '
                else:
                    m = np.nanmean(cell)
                    row += '#' if m < .6 else '+' if m < .85 else '-' if m < .999 else '.'
            mask.append(row.rstrip())
    report['mask'] = mask
    report['region'] = region
    return report


def main(argv=None):
    import argparse
    import json
    parser = argparse.ArgumentParser(description='Run the sun-shadow apply twin on a capture frame.')
    parser.add_argument('--log', required=True, help='session log with the frame\'s lines')
    parser.add_argument('--capture', required=True, help='directory with depth_/shadow_map_/hdr_ dumps')
    parser.add_argument('--frame', type=int, required=True)
    parser.add_argument('--device', type=int, default=1)
    parser.add_argument('--region', help='y0,y1,x0,x1 of the ASCII mask')
    parser.add_argument('--bias', type=float, help='override the constant bias')
    parser.add_argument('--no-jitter', action='store_true', help='drop the jitter term from m20/m21 (the run106 pass)')
    args = parser.parse_args(argv)
    params, extra, source = frame_params(args.log, args.frame, args.device)
    if args.bias is not None:
        params['bias_constant'] = args.bias
    if args.no_jitter:
        params['m20'] -= 2.0 * extra['jitter_px'][0] / extra['width']; params['m21'] += 2.0 * extra['jitter_px'][1] / extra['height']
    d, s, sun_map, luminance = load_capture(args.capture, args.device, args.frame, extra['width'], extra['height'], extra['map'])
    region = tuple(int(v) for v in args.region.split(',')) if args.region else None
    report = frame_report(d, s, sun_map, params, luminance, region)
    mask = report.pop('mask')
    print(json.dumps(dict(frame=args.frame, source=source, params={k: v for k, v in params.items() if k != 'rows'}, **report), indent=1))
    print('\n'.join(mask))


if __name__ == '__main__':
    main()
