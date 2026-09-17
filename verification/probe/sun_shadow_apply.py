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
* `expected_factor_cascades` / `compare_frame_cascades`: the same for
  src/temporal/sun_shadow_cascade_apply_ps.hlsl (docs/architecture/
  shadow-cascades.md, section 2): per-pixel selection of the first cascade
  containing the pixel, the blend band into the next one, the last cascade's
  fade to lit, absent cascades lit, per-cascade size and bias.
"""
import math
import struct

EPS_TEXEL = 2e-3     # texel-boundary ambiguity of floor() between float32 (GPU) and float64
EPS_DEPTH = 1e-4     # compare-equality ambiguity in normalized sun depth
KERNEL = [(math.cos(k * math.pi / 8), math.sin(k * math.pi / 8)) for k in range(8)]
# Texel convention (the programs' header comments): the replay rasterizes under
# D3D9, so map texel (i, j) holds the depth at map position (i, j) / N and is
# addressed at ((i, j) + 0.5) / N; the receiver looks up at suv = muv + 0.5 / N
# (nearest texel round(muv N)) and the receiver-plane term runs on tapUV - suv.
# `legacy_floor=True` is the rule of the programs before 2026-09-17 (texel
# floor(muv N), half a texel off), kept for captures made with those builds
# and as the sensitivity witness of the fixture's shift fit.


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


def expected_factor(d, s, sun_map, params, coarse=False, legacy_floor=False):
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
    half = 0.0 if legacy_floor else .5 / size
    su, sv = mu + half, mv + half
    texel_u, texel_v = np.floor(su * size), np.floor(sv * size)
    ambiguous = np.zeros(d.shape, dtype=bool)
    near_boundary = lambda x: np.minimum(x - np.floor(x), np.ceil(x) - x) < EPS_TEXEL
    ambiguous |= near_boundary(su * size) | near_boundary(sv * size)
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
            bias = np.where(planar, np.clip((tap_uv_u - su) * gu + (tap_uv_v - sv) * gv, -params['bias_max'], params['bias_max']), -params['bias_max'])
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
        other = expected_factor(d, s, sun_map, params, coarse=True, legacy_floor=legacy_floor)
        ambiguous |= other['factor'] != factor
    # Sentinel members make a quad's derivatives meaningless for its other
    # members (a share-free receiver still has a depth, so its quad is fine).
    sentinel = d < 0.0
    quad_sentinel = sentinel.copy()
    quad_sentinel[:, 0::2] |= sentinel[:, 1::2]; quad_sentinel[:, 1::2] |= sentinel[:, 0::2]
    quad_sentinel[0::2, :] |= quad_sentinel[1::2, :].copy(); quad_sentinel[1::2, :] |= quad_sentinel[0::2, :].copy()
    ambiguous |= quad_sentinel & valid
    return {'factor': factor, 'f': f, 'valid': valid, 'ambiguous': ambiguous & valid, 'sun': sun, 'z': z}


CASCADE_MARGIN, CASCADE_BAND = .95, .10   # shadow_cascade_select_margin / _blend_band (shadow_replay_projection.h)
EPS_SELECT = 1e-5                          # selection-threshold ambiguity in sun-space NDC (float32 rows on the GPU)


def expected_factor_cascades(d, s, maps, params, coarse=False, legacy_floor=False):
    """The cascade program's twin. `params`: m00 m11 m20 m21 m22 m32 exponent
    planar_step jitter_index, optional margin/band, and `cascades`: a list of
    dicts rows(12) bias_constant bias_max valid. `maps[i]` is cascade i's map
    (None while absent). Returns factor, f, valid, ambiguous, selected (index
    of the owning cascade, -1 outside every one), weights and per-cascade f."""
    import numpy as np
    height, width = d.shape
    m00, m11, m20, m21, m22, m32 = (params[k] for k in ('m00', 'm11', 'm20', 'm21', 'm22', 'm32'))
    margin, band_width = params.get('margin', CASCADE_MARGIN), params.get('band', CASCADE_BAND)
    cascades = params['cascades']
    count = len(cascades)
    z = m32 / (d - m22)
    i = np.arange(width, dtype=np.float64)[None, :].repeat(height, 0)
    j = np.arange(height, dtype=np.float64)[:, None].repeat(width, 1)
    view = [((i + .5) / width * 2.0 - 1.0 - m20) * z / m00, (1.0 - (j + .5) / height * 2.0 - m21) * z / m11, z]
    derivative = _coarse if coarse else _quad_derivative
    dpdx = [derivative(v, 1) for v in view]; dpdy = [derivative(v, 0) for v in view]
    step = np.abs(dpdx[2]) + np.abs(dpdy[2])
    steady = step < params['planar_step'] * np.abs(z)
    ambiguous = np.abs(step - params['planar_step'] * np.abs(z)) < .25 * params['planar_step'] * np.abs(z)
    sun, inside, band, reach = [], [], [], []
    for c in cascades:
        rows = c['rows']
        position = [rows[r * 4] * view[0] + rows[r * 4 + 1] * view[1] + rows[r * 4 + 2] * view[2] + rows[r * 4 + 3] for r in range(3)]
        m = np.maximum(np.abs(position[0]), np.abs(position[1]))
        sun.append(position); reach.append(m)
        inside.append(((m <= margin) & (position[2] >= 0.0) & (position[2] <= 1.0)).astype(np.float64))
        band.append(np.clip((m - (margin - band_width)) / band_width, 0.0, 1.0))
    chosen, open_ = [], np.ones(d.shape)
    for c in range(count):
        chosen.append(open_ * inside[c]); open_ = open_ * (1.0 - inside[c])
    covered = sum(chosen) > 0.0
    weights = []
    for c in range(count):
        last = c + 1 == count
        following = np.ones(d.shape) if last else inside[c + 1]
        w = chosen[c] * (1.0 - band[c] * following)
        if c:
            w = w + chosen[c - 1] * band[c - 1] * inside[c]
        weights.append(w)
    selected = np.full(d.shape, -1, dtype=np.int64)
    for c in range(count - 1, -1, -1):
        selected = np.where(chosen[c] > 0.0, c, selected)
    # A selection threshold within EPS of a pixel's position is the GPU's call
    # (the weights are continuous across the margin except at the depth range
    # and where the following cascade stops containing the pixel).
    for c in range(count):
        owner = np.zeros(d.shape, dtype=bool)
        for k in range(c + 1):
            owner |= chosen[k] > 0.0
        near = (np.abs(reach[c] - margin) < EPS_SELECT) | (np.abs(sun[c][2]) < EPS_DEPTH) | (np.abs(sun[c][2] - 1.0) < EPS_DEPTH)
        ambiguous |= near & (owner | (open_ > 0.0))
    cos_r, sin_r = KERNEL[int(params['jitter_index']) % 8]
    near_boundary = lambda x: np.minimum(x - np.floor(x), np.ceil(x) - x) < EPS_TEXEL
    shade = np.zeros(d.shape)
    per_cascade = []
    for c in range(count):
        cascade, sun_map = cascades[c], maps[c]
        if not cascade.get('valid', True) or sun_map is None:
            per_cascade.append(np.ones(d.shape)); continue
        size = sun_map.shape[0]
        rows, position = cascade['rows'], sun[c]
        active = weights[c] > 0.0
        mu, mv = position[0] * .5 + .5, .5 - position[1] * .5
        dot3 = lambda row, g: rows[row * 4] * g[0] + rows[row * 4 + 1] * g[1] + rows[row * 4 + 2] * g[2]
        dudx, dudy, dvdx, dvdy = .5 * dot3(0, dpdx), .5 * dot3(0, dpdy), -.5 * dot3(1, dpdx), -.5 * dot3(1, dpdy)
        dzdx, dzdy = dot3(2, dpdx), dot3(2, dpdy)
        det = dudx * dvdy - dvdx * dudy
        planar = (np.abs(det) > 1e-12) & steady
        inv = np.where(planar, 1.0 / np.where(planar, det, 1.0), 0.0)
        gu = (dzdx * dvdy - dzdy * dvdx) * inv
        gv = (dzdy * dudx - dzdx * dudy) * inv
        half = 0.0 if legacy_floor else .5 / size
        su, sv = mu + half, mv + half
        texel_u, texel_v = np.floor(su * size), np.floor(sv * size)
        local = near_boundary(su * size) | near_boundary(sv * size)
        lit = np.zeros(d.shape)
        for kj in (-1, 0, 1):
            for ki in (-1, 0, 1):
                ou, ov = cos_r * ki - sin_r * kj, sin_r * ki + cos_r * kj
                raw_u, raw_v = texel_u + .5 + ou, texel_v + .5 + ov
                if abs(ou - round(ou)) > 1e-9 or abs(ov - round(ov)) > 1e-9:
                    local |= near_boundary(raw_u) | near_boundary(raw_v)
                tap_u, tap_v = np.floor(raw_u), np.floor(raw_v)
                tap_uv_u, tap_uv_v = (tap_u + .5) / size, (tap_v + .5) / size
                bias = np.where(planar, np.clip((tap_uv_u - su) * gu + (tap_uv_v - sv) * gv, -cascade['bias_max'], cascade['bias_max']), -cascade['bias_max'])
                reference = position[2] + bias - cascade['bias_constant']
                iu = np.clip(np.nan_to_num(tap_u), 0, size - 1).astype(np.int64)
                iv = np.clip(np.nan_to_num(tap_v), 0, size - 1).astype(np.int64)
                sampled = sun_map[iv, iu]
                local |= np.abs(sampled - reference) < EPS_DEPTH
                lit += (sampled >= reference)
        f_c = lit / 9.0
        per_cascade.append(f_c)
        shade += np.where(active, weights[c] * (1.0 - f_c), 0.0)
        ambiguous |= local & active
    f = np.clip(1.0 - shade, 0.0, 1.0)
    valid = (d >= 0.0) & (s > 0.0) & covered
    base = np.clip(1.0 - (1.0 - f) * np.clip(s, 0.0, 1.0), 0.0, 1.0)
    shadowed = np.where(base > 0.0, np.power(np.maximum(base, 1e-30), params['exponent']), 0.0)
    factor = np.where(valid & (f < 1.0), shadowed, 1.0)
    if not coarse:
        other = expected_factor_cascades(d, s, maps, params, coarse=True, legacy_floor=legacy_floor)
        ambiguous |= other['factor'] != factor
    sentinel = d < 0.0
    quad_sentinel = sentinel.copy()
    quad_sentinel[:, 0::2] |= sentinel[:, 1::2]; quad_sentinel[:, 1::2] |= sentinel[:, 0::2]
    quad_sentinel[0::2, :] |= quad_sentinel[1::2, :].copy(); quad_sentinel[1::2, :] |= quad_sentinel[0::2, :].copy()
    ambiguous |= quad_sentinel & valid
    return {'factor': factor, 'f': f, 'valid': valid, 'ambiguous': ambiguous & valid, 'selected': selected, 'weights': weights,
            'per_cascade': per_cascade, 'band': band, 'z': z}


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
    the classification changes. With scene['light'] (a world position) the
    shadow is the POINT light's: every receiver's ray runs to that position
    (a hit counts only before the light); otherwise every ray is scene['sun']."""
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
    texel = 2.0 * scene.get('extent', 0.0) / size
    boxes = scene.get('boxes') or [scene['box']]  # the cascade scene has several casters
    if 'texel_map' in scene:
        texel = scene['texel_map']              # per pixel: the owning cascade's world texel
    light = scene.get('light')
    def lit_at(offset_u, offset_v):
        o = [world[c] + (scene['right'][c] * offset_u + scene['up'][c] * offset_v) * texel + sun[c] * 1e-4 for c in range(3)]
        lit = np.ones(z.shape, dtype=bool)
        direction = [light[c] - o[c] for c in range(3)] if light is not None else [np.full(z.shape, sun[c]) for c in range(3)]
        for box in boxes:
            t = _hit(o, direction, box[:3], box[3:])
            lit &= (t <= 0.0) | (t >= 1.0) if light is not None else t <= 0.0
        return lit
    lit = lit_at(0.0, 0.0)
    distance = np.zeros(d.shape, dtype=np.int64)
    for radius in (2, 1):
        change = np.zeros(d.shape, dtype=bool)
        for du in (-radius, 0, radius):
            for dvv in (-radius, 0, radius):
                if du or dvv:
                    change |= lit_at(float(du), float(dvv)) != lit
        distance = np.where(change, radius, distance)
    # Receivers on the plane (y = 0) versus on the box's own faces: the box-on-
    # plane shadow edge is the plane subset; a box face at its camera silhouette
    # is a non-planar quad whose fallback bias (the clamp) can light it.
    on_plane = np.abs(world[1]) < 1e-3 * max(1.0, max(abs(v) for v in scene['box']))
    return {'lit': lit, 'edge_distance': distance, 'on_plane': on_plane, 'world': world, 'lit_at': lit_at}


def compare_frame(before, after, d, s, sun_map, params, scene=None, strict_box=True):
    """The readbacks (H x W x 4 float64 from FP16) against before * factor.
    Returns counts: compared pixels, violations beyond one FP16 code, the
    worst error in codes, identity pixels that changed, ambiguous pixels, and
    (with `scene`) the analytic edge record. `ok` requires no hard-shadow
    disagreement beyond two texels of an analytic edge; with strict_box=False
    only the plane receivers are held to that (the box faces' silhouette
    quads are reported under box_beyond_two_texels)."""
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
        plane = analytic['on_plane']
        record['analytic'] = {'mismatch': int(np.count_nonzero(mismatch)), 'within_one_texel': int(np.count_nonzero(within_one)),
                              'within_two_texels': int(np.count_nonzero(mismatch & (analytic['edge_distance'] == 2))),
                              'beyond_two_texels': int(np.count_nonzero(far)), 'shadowed_analytic': int(np.count_nonzero(strict & ~analytic['lit'])),
                              'plane_mismatch': int(np.count_nonzero(mismatch & plane)), 'plane_within_one_texel': int(np.count_nonzero(within_one & plane)),
                              'plane_beyond_two_texels': int(np.count_nonzero(far & plane)), 'box_beyond_two_texels': int(np.count_nonzero(far & ~plane))}
        record['ok'] = record['ok'] and (record['analytic']['beyond_two_texels'] if strict_box else record['analytic']['plane_beyond_two_texels']) == 0
    return record


SHIFT_STEPS = (-.75, -.5, -.25, 0.0, .25, .5, .75)


def fit_shift(f, select, lit_at):
    """The map-space shift, in texels of each pixel's own cascade, that best
    explains the f >= 0.5 shadow against the analytic one: the (u, v) offset
    on SHIFT_STEPS^2 with the fewest disagreements when the analytic shadow
    is evaluated at the receiver displaced by it (+u = the basis' right, +v =
    minus its up). A lookup that is half a texel off shows as (+-0.5, +-0.5);
    0.25-texel steps resolve it on a few hundred edge pixels. Returns the
    offset, its count and the count at (0, 0)."""
    import numpy as np
    half = f >= 0.5
    best = None
    counts = {}
    for dv in SHIFT_STEPS:
        for du in SHIFT_STEPS:
            count = int(np.count_nonzero(select & (half != lit_at(du, -dv))))
            counts[(du, dv)] = count
            if best is None or count < counts[best] or (count == counts[best] and abs(du) + abs(dv) < abs(best[0]) + abs(best[1])):
                best = (du, dv)
    return {'shift': list(best), 'mismatch': counts[best], 'mismatch_at_zero': counts[(0.0, 0.0)],
            'grid': [[counts[(du, dv)] for du in SHIFT_STEPS] for dv in SHIFT_STEPS]}


def sum_shift_fits(fits):
    """The fit over several frames (their grids summed): one frame's straight
    edges sit at one sub-texel phase of the map grid, which alone moves its
    minimum by up to half a texel either way; frames at evenly spread phases
    average that out."""
    total = [[sum(f['grid'][j][i] for f in fits) for i in range(len(SHIFT_STEPS))] for j in range(len(SHIFT_STEPS))]
    best = min(((total[j][i], abs(SHIFT_STEPS[i]) + abs(SHIFT_STEPS[j]), i, j) for j in range(len(SHIFT_STEPS)) for i in range(len(SHIFT_STEPS))))
    centre = len(SHIFT_STEPS) // 2
    return {'shift': [SHIFT_STEPS[best[2]], SHIFT_STEPS[best[3]]], 'mismatch': best[0], 'mismatch_at_zero': total[centre][centre], 'frames': len(fits), 'grid': total}


def compare_frame_cascades(before, after, d, s, maps, params, scene, extents):
    """The cascade quad's readbacks against before * factor within one FP16
    code, plus per owning cascade: the pixels it owns, its shadowed pixels, the
    edge record against the analytic hard shadow with that cascade's own world
    texel (band pixels take the coarser following cascade's): `edge_beyond_one`
    counts plane receivers whose f >= 0.5 classification disagrees with the
    analytic shadow further than one texel from an analytic edge;
    `interior_wrong` counts plane receivers at least two texels inside a shadow
    or a lit area whose f is not 0 (9/9 shadowed, 0 allowed) or 1: a lit gap or
    a double darkening in a blend band would show there. `monotone`: f lies
    between the two blended cascades' f everywhere.
    A scene with 'light' and 'landings' (the sun at finite distance): the
    analytic shadow is the point light's, and `regions` holds, per landing
    (the plane receivers within five box half-sizes of the point a box's
    shadow lands on, or the box stands on), the owning cascade, the f >= 0.5 mismatches beyond
    one texel against the point-light shadow and against the PARALLEL shadow of
    the owning cascade's own sun (what an orthographic map can show at best),
    and the predicted residual h r / D of the orthographic map in texels."""
    import numpy as np
    expected = expected_factor_cascades(d, s, maps, params)
    factor, valid, ambiguous, selected, f = expected['factor'], expected['valid'], expected['ambiguous'], expected['selected'], expected['f']
    strict = valid & ~ambiguous
    reference = before[..., :3] * factor[..., None]
    codes = np.abs(after[..., :3] - reference) / fp16_code(reference)
    identity_changed = int(np.count_nonzero((factor == 1.0) & np.any(after != before, axis=-1)))
    worst = float(np.max(np.where(strict[..., None], codes, 0.0))) if np.any(strict) else 0.0
    violations = int(np.count_nonzero(strict & np.any(codes > 1.0 + 1e-9, axis=-1)))
    alpha_changed = int(np.count_nonzero(after[..., 3] != before[..., 3]))
    count = len(params['cascades'])
    sizes = [m.shape[0] if m is not None else None for m in maps]
    texels = [2.0 * extents[c] / sizes[c] if sizes[c] else None for c in range(count)]
    # The texel that bounds a pixel's edge error: its owner's, or the following
    # cascade's while the band blends into it; an absent cascade has no edge.
    texel_map = np.zeros(d.shape)
    for c in range(count):
        mine = selected == c
        coarser = texels[c + 1] if c + 1 < count and texels[c + 1] else texels[c]
        in_band = expected['band'][c] > 0.0
        if texels[c]:
            texel_map = np.where(mine, np.where(in_band & (coarser is not None), coarser or 0.0, texels[c]), texel_map)
    scene = dict(scene, texel_map=np.where(texel_map > 0.0, texel_map, 1.0))
    analytic = analytic_shadow(d, s, params, scene, 1)
    plane = analytic['on_plane'] & strict & (texel_map > 0.0)
    half = f >= 0.5
    mismatch = plane & (half != analytic['lit'])
    beyond_one = mismatch & (analytic['edge_distance'] != 1)
    # The caster's contact line is an edge too (the constant bias lights the
    # receivers within about a texel of the wall they touch), but the texel
    # offsets above run across the light, not along it: keep three texels clear
    # of every box footprint.
    world = analytic['world']
    contact = np.zeros(d.shape, dtype=bool)
    for box in scene['boxes']:
        if box[1] > 1e-3 * max(1.0, abs(box[4])):
            continue                                  # a caster above the plane touches nothing
        dx = np.maximum(np.maximum(box[0] - world[0], world[0] - box[3]), 0.0)
        dz = np.maximum(np.maximum(box[2] - world[2], world[2] - box[5]), 0.0)
        contact |= np.hypot(dx, dz) < 3.0 * scene['texel_map']
    interior = plane & (analytic['edge_distance'] == 0) & ~contact
    low, high = np.ones(d.shape), np.zeros(d.shape)
    for c in range(count):
        touched = expected['weights'][c] > 0.0
        low = np.where(touched, np.minimum(low, expected['per_cascade'][c]), low)
        high = np.where(touched, np.maximum(high, expected['per_cascade'][c]), high)
    fading = np.zeros(d.shape, dtype=bool)   # the last cascade's band (and a band into an absent cascade) blends with lit
    fading |= (selected == count - 1) & (expected['band'][count - 1] > 0.0)
    high = np.where(fading, 1.0, high)
    monotone = int(np.count_nonzero(valid & ((f < low - 1e-12) | (f > np.maximum(high, low) + 1e-12))))
    per = {}
    for c in range(count):
        mine = selected == c
        band_c = mine & (expected['band'][c] > 0.0)
        # Interior of a band: weighted f must still be the hard value unless the band fades to lit.
        solid = interior & mine & ~(fading & mine)
        wrong = solid & (np.where(analytic['lit'], f != 1.0, f != 0.0))
        per[str(c)] = {'owned': int(np.count_nonzero(valid & mine)), 'band': int(np.count_nonzero(valid & band_c)), 'shadowed': int(np.count_nonzero(strict & mine & (f < 1.0))),
                       'shadowed_analytic': int(np.count_nonzero(plane & mine & ~analytic['lit'])), 'edge_mismatch': int(np.count_nonzero(mismatch & mine)),
                       'edge_beyond_one': int(np.count_nonzero(beyond_one & mine)), 'interior_wrong': int(np.count_nonzero(wrong)),
                       'band_shadowed': int(np.count_nonzero(strict & band_c & (f < 1.0))), 'texel_world': texels[c]}
    record = {'valid': int(np.count_nonzero(valid)), 'compared': int(np.count_nonzero(strict)), 'ambiguous': int(np.count_nonzero(ambiguous)), 'violations': violations,
              'worst_codes': worst, 'identity_changed': identity_changed, 'alpha_changed': alpha_changed, 'shadowed': int(np.count_nonzero(strict & (f < 1.0))),
              'penumbra': int(np.count_nonzero(strict & (f > 0.0) & (f < 1.0))), 'monotone_violations': monotone, 'cascades': per,
              'edge_beyond_one': int(np.count_nonzero(beyond_one)), 'interior_wrong': sum(v['interior_wrong'] for v in per.values())}
    if len(scene['boxes']) > 1:
        # The second caster alone (the sun-column occluder): plane receivers lit
        # by the first box's law and shadowed with both, and how many of them
        # the quad darkened.
        first_only = analytic_shadow(d, s, params, dict(scene, boxes=scene['boxes'][:1]), 1)
        alone = plane & first_only['lit'] & ~analytic['lit']
        record['second_caster'] = {'pixels': int(np.count_nonzero(alone)), 'interior': int(np.count_nonzero(alone & interior)),
                                   'darkened': int(np.count_nonzero(alone & (f < 0.5))), 'interior_dark': int(np.count_nonzero(alone & interior & (f == 0.0)))}
    if scene.get('landings'):
        parallel_beyond = np.zeros(d.shape, dtype=bool)
        for c in range(count):  # the parallel shadow of each cascade's own sun, on the pixels it owns
            flat = analytic_shadow(d, s, params, dict(scene, light=None, sun=scene['suns'][c]), 1)
            parallel_beyond |= (selected == c) & plane & (half != flat['lit']) & (flat['edge_distance'] != 1)
        light, cam = scene['light'], scene['camera']
        distance = math.sqrt(sum((light[k] - cam[k]) ** 2 for k in range(3)))
        record['regions'] = []
        for gx, gz, lift, half_size in scene['landings']:
            near = plane & (np.hypot(world[0] - gx, world[2] - gz) < 5.0 * half_size)
            owners = [int(np.count_nonzero(near & (selected == c))) for c in range(count)]
            owner = max(range(count), key=lambda c: owners[c])
            rel = [gx - cam[0], -cam[1], gz - cam[2]]
            view = [sum(rel[k] * scene[axis][k] for k in range(3)) for axis in ('cam_right', 'cam_up', 'cam_forward')] + [1.0]
            rows = params['cascades'][owner]['rows']
            lateral = extents[owner] * math.hypot(sum(rows[k] * view[k] for k in range(4)), sum(rows[4 + k] * view[k] for k in range(4)))
            to_light = [light[0] - gx, light[1], light[2] - gz]
            height = lift * to_light[1] / math.sqrt(sum(v * v for v in to_light))
            record['regions'].append({'landing': [gx, gz], 'pixels': int(np.count_nonzero(near)), 'owned': owners, 'owner': owner, 'shadowed_point': int(np.count_nonzero(near & ~analytic['lit'])),
                                      'shadowed_quad': int(np.count_nonzero(near & ~half)), 'mismatch_point': int(np.count_nonzero(near & mismatch)),
                                      'beyond_one_point': int(np.count_nonzero(near & beyond_one)), 'beyond_one_parallel': int(np.count_nonzero(near & parallel_beyond)),
                                      'caster_height': height, 'lateral_offset': lateral, 'light_distance': distance,
                                      'residual_units': height * lateral / distance, 'residual_texels': height * lateral / distance / texels[owner]})
    # The half-texel witness: the best-fit shift of this program's shadow, and of
    # the pre-fix lookup rule on the same maps (the sensitivity of the fit).
    edge = plane & (analytic['edge_distance'] != 0) & ~contact
    record['shift_fit'] = dict(fit_shift(f, edge, analytic['lit_at']), edge_pixels=int(np.count_nonzero(edge)))
    legacy = expected_factor_cascades(d, s, maps, params, legacy_floor=True)
    record['shift_fit_legacy_rule'] = fit_shift(legacy['f'], edge & legacy['valid'] & ~legacy['ambiguous'], analytic['lit_at'])
    record['ok'] = violations == 0 and identity_changed == 0 and alpha_changed == 0 and monotone == 0
    return record


def parse_cascade_params(fields):
    """The fixture's SUNAPPLY_CASCADES line -> the cascade twin's params, the
    scene, and per cascade the extent and the frame its map was replayed on."""
    triple = lambda key: tuple(float(v) for v in fields[key].split(','))
    params = {k: float(fields[k]) for k in ('m00', 'm11', 'm20', 'm21', 'm22', 'm32', 'exponent', 'planar_step')}
    params['jitter_index'] = int(fields['jitter_index'])
    count = int(fields['cascades'])
    params['cascades'] = [{'rows': tuple(float(v) for v in fields['rows%d' % c].split(',')), 'bias_constant': float(fields['bias%d' % c]),
                           'bias_max': float(fields['bias_max%d' % c]), 'valid': fields['valid%d' % c] == '1'} for c in range(count)]
    for c in params['cascades']:
        if len(c['rows']) != 12:
            raise ValueError('cascade rows: expected 12 values')
    scene = {k: triple(k) for k in ('camera', 'cam_right', 'cam_up', 'cam_forward', 'sun', 'right', 'up')}
    scene['boxes'] = [tuple(float(v) for v in text.split(',')) for text in fields['boxes'].split(';')]
    scene['box'] = scene['boxes'][0]
    if 'light' in fields:  # the sun at finite distance: its position, every cascade's own sun, the shadows' landing points
        scene['light'] = triple('light')
        scene['suns'] = [tuple(float(v) for v in text.split(',')) for text in fields['suns'].split(';')]
        scene['landings'] = [tuple(float(v) for v in text.split(',')) for text in fields['landings'].split(';')]
    extents = [float(fields['extent%d' % c]) for c in range(count)]
    map_frames = [int(fields['map_frame%d' % c]) for c in range(count)]
    return params, scene, extents, map_frames


def parse_params(fields):
    """The fixture's SUNAPPLY line fields -> params and scene dicts."""
    triple = lambda key: tuple(float(v) for v in fields[key].split(','))
    params = {k: float(fields[k]) for k in ('m00', 'm11', 'm20', 'm21', 'm22', 'm32', 'exponent', 'bias_constant', 'bias_max', 'planar_step')}
    params['rows'] = tuple(float(v) for v in fields['rows'].split(','))
    params['jitter_index'] = int(fields['jitter_index'])
    scene = {k: triple(k) for k in ('camera', 'cam_right', 'cam_up', 'cam_forward', 'sun', 'right', 'up', 'forward', 'center')}
    scene['extent'] = float(fields['extent']); scene['depth_half'] = float(fields['depth_half'])
    scene['box'] = tuple(float(v) for v in fields['box'].split(','))
    for key in ('wide', 'scale', 'bias_units', 'clamp_texels', 'texel_world'):  # the wide configuration (absent on the original record)
        if key in fields:
            scene[key] = float(fields[key])
    return params, scene


def half_bytes(values):
    """Float sequence -> little-endian FP16 bytes (tests)."""
    return struct.pack('<%de' % len(values), *values)


# ---- the twin on a run's F8 capture (docs/verification/directional-shadows.md,
# "Run 36 session B (run106)"): the pass's inputs come from the session log, the
# RT2, map and HDR dumps from the capture directory. No Wine, no game.
PRODUCTION_M22, PRODUCTION_M32 = 1.000003, -6.0000184   # ao_default_m22/m32 (motion_output.cpp)
PRODUCTION_BIAS = dict(bias_constant=.001, bias_max=.01, planar_step=.05)  # SunShadowApplyParams defaults (= the resolved default below at 250 / 512 / 1024)
LINE_FIELDS = __import__('re').compile(r'(\w+)=([^\s]+)')
# The world-unit bias (sun_shadow_apply_pass.h, sun_shadow_apply_bias): the
# constant is bias_units plus one world texel (2 extent / size) over the map's
# depth range 2 depth_half; the receiver-plane clamp (and non-planar fallback)
# is BIAS_CLAMP_TEXELS world texels over the same range. The defaults
# 0.53571875 + 0.48828125 = 1.024 and 20.97152 x 0.48828125 = 10.24 resolve
# to exactly 0.001 / 0.01 at the default cascade. Double arithmetic, one
# rounding to float32 per value.
BIAS_UNITS_DEFAULT, BIAS_CLAMP_TEXELS = 0.53571875, 20.97152


def resolve_bias(bias_units, extent, depth_half, size, clamp_texels=BIAS_CLAMP_TEXELS):
    import numpy as np
    texel = 2.0 * extent / size
    constant = (bias_units + texel) / (2.0 * depth_half)
    return {'bias_constant': float(np.float32(constant)), 'bias_max': float(np.float32(clamp_texels * texel / (2.0 * depth_half))), 'texel_world': float(np.float32(texel))}


def line_fields(line):
    return dict(LINE_FIELDS.findall(line))


def parse_apply_params(fields):
    """A `sun_shadow_apply_params` line (capture frames; motion_output.cpp
    run_sun_shadow_apply) -> the params dict of expected_factor plus the
    frame's raster size, map size and pixel jitter."""
    if 'cascades' in fields:
        return parse_apply_cascade_params(fields)
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
    # Since the tunable cascade: the world-unit inputs the line resolved the
    # bias from; the printed normalized values must be their resolution.
    if 'bias_units' in fields:
        for key in ('bias_units', 'texel_world', 'extent', 'depth_half'):
            extra[key] = float(fields[key])
        extra['clamp_texels'] = float(fields.get('clamp_texels', BIAS_CLAMP_TEXELS))
        resolved = resolve_bias(extra['bias_units'], extra['extent'], extra['depth_half'], extra['map'], extra['clamp_texels'])
        for key, printed in (('bias_constant', params['bias_constant']), ('bias_max', params['bias_max']), ('texel_world', extra['texel_world'])):
            if abs(resolved[key] - printed) > 1e-6 * max(1.0, abs(printed)) + 1e-9:
                raise ValueError('%s %.9g does not resolve from bias_units %g extent %g depth_half %g map %d (%.9g)'
                                 % (key, printed, extra['bias_units'], extra['extent'], extra['depth_half'], extra['map'], resolved[key]))
    return params, extra


def parse_apply_cascade_params(fields):
    """The cascade form of the line (cascades= and, per cascade i, valid<i>
    map<i> map_frame<i> bias<i> bias_max<i> texel_world<i> extent<i>
    depth_light<i> depth_behind<i> rows<i>): params for
    expected_factor_cascades; every printed bias must resolve from the line's
    own world-unit inputs."""
    params = {k: float(fields[k]) for k in ('m00', 'm11', 'm20', 'm21', 'm22', 'm32', 'exponent', 'planar_step', 'margin', 'band')}
    params['jitter_index'] = int(fields['jitter_index'])
    count = int(fields['cascades'])
    bias_units, clamp_texels = float(fields['bias_units']), float(fields['clamp_texels'])
    params['cascades'], cascades = [], []
    for c in range(count):
        rows = tuple(float(v) for v in fields['rows%d' % c].split(','))
        if len(rows) != 12:
            raise ValueError('rows%d: expected 12 values, got %d' % (c, len(rows)))
        entry = {'rows': rows, 'bias_constant': float(fields['bias%d' % c]), 'bias_max': float(fields['bias_max%d' % c]), 'valid': fields['valid%d' % c] == '1'}
        detail = {'map': int(fields['map%d' % c]), 'map_frame': int(fields['map_frame%d' % c]), 'texel_world': float(fields['texel_world%d' % c]),
                  'extent': float(fields['extent%d' % c]), 'depth_light': float(fields['depth_light%d' % c]), 'depth_behind': float(fields['depth_behind%d' % c])}
        resolved = resolve_bias(bias_units, detail['extent'], .5 * (detail['depth_light'] + detail['depth_behind']), detail['map'], clamp_texels)
        for key, printed in (('bias_constant', entry['bias_constant']), ('bias_max', entry['bias_max']), ('texel_world', detail['texel_world'])):
            if abs(resolved[key] - printed) > 1e-6 * max(1.0, abs(printed)) + 1e-9:
                raise ValueError('cascade %d: %s %.9g does not resolve (%.9g)' % (c, key, printed, resolved[key]))
        params['cascades'].append(entry); cascades.append(detail)
    extra = dict(width=int(fields['width']), height=int(fields['height']), jitter_px=(float(fields['jitter_x']), float(fields['jitter_y'])),
                 bias_units=bias_units, clamp_texels=clamp_texels, cascades=cascades, map=cascades[0]['map'])
    return params, extra


def load_cascade_maps(directory, device, frame, extra, params):
    """F8 writes every present cascade's map on the capture frame itself
    (shadow_map<i>_<device>_<frame>.r32f), the retained far one included."""
    from pathlib import Path
    maps = []
    for c, detail in enumerate(extra['cascades']):
        path = Path(directory) / ('shadow_map%d_%d_%d.r32f' % (c, device, frame))
        maps.append(unpack_map(path.read_bytes(), detail['map']) if params['cascades'][c]['valid'] and path.exists() else None)
    return maps


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


def frame_report(d, s, sun_map, params, luminance=None, region=None, columns=64, lines=24, legacy_floor=False):
    """The twin on real inputs: receiver-minus-map residual on valid pixels
    (nearest texel, no bias), the factor statistics, the HDR darkening of
    shadowed pixels against same-surface lit neighbours within 4 px (and the
    lit-lit control), and an ASCII mask of `region` (y0, y1, x0, x1) or the
    valid bounding box: '#' factor < .6, '+' < .85, '-' < .999, '.' 1."""
    import numpy as np
    out = expected_factor(d, s, sun_map, params, legacy_floor=legacy_floor)
    factor, f, valid, ambiguous, sun, z = out['factor'], out['f'], out['valid'], out['ambiguous'], out['sun'], out['z']
    size = sun_map.shape[0]
    mu, mv = sun[0] * .5 + .5, .5 - sun[1] * .5
    nearest = 0.0 if legacy_floor else .5  # the texel holding the receiver's position: round(muv N) under the D3D9 raster convention
    tu = np.clip(np.floor(np.nan_to_num(mu) * size + nearest).astype(np.int64), 0, size - 1)
    tv = np.clip(np.floor(np.nan_to_num(mv) * size + nearest).astype(np.int64), 0, size - 1)
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
    parser.add_argument('--legacy-floor', action='store_true', help='the lookup rule of builds before 2026-09-17 (texel floor(muv N), no half-texel offset)')
    args = parser.parse_args(argv)
    params, extra, source = frame_params(args.log, args.frame, args.device)
    if args.bias is not None:
        params['bias_constant'] = args.bias
    if args.no_jitter:
        params['m20'] -= 2.0 * extra['jitter_px'][0] / extra['width']; params['m21'] += 2.0 * extra['jitter_px'][1] / extra['height']
    if 'cascades' in params:
        import numpy as np
        d, s = unpack_rt2((__import__('pathlib').Path(args.capture) / ('depth_%d_%d.rg32f' % (args.device, args.frame))).read_bytes(), extra['width'], extra['height'])
        out = expected_factor_cascades(d, s, load_cascade_maps(args.capture, args.device, args.frame, extra, params), params, legacy_floor=args.legacy_floor)
        valid = out['valid']
        print(json.dumps(dict(frame=args.frame, source=source, cascades=extra['cascades'], valid=int(valid.sum()), ambiguous=int(out['ambiguous'].sum()),
                              owned=[int((valid & (out['selected'] == c)).sum()) for c in range(len(params['cascades']))],
                              f_below_0_9=float((out['f'][valid] < .9).mean()) if valid.any() else None,
                              factor_mean=float(out['factor'][valid].mean()) if valid.any() else None), indent=1))
        return
    d, s, sun_map, luminance = load_capture(args.capture, args.device, args.frame, extra['width'], extra['height'], extra['map'])
    region = tuple(int(v) for v in args.region.split(',')) if args.region else None
    report = frame_report(d, s, sun_map, params, luminance, region, legacy_floor=args.legacy_floor)
    mask = report.pop('mask')
    print(json.dumps(dict(frame=args.frame, source=source, params={k: v for k, v in params.items() if k != 'rows'}, **report), indent=1))
    print('\n'.join(mask))


if __name__ == '__main__':
    main()
