#!/usr/bin/env python3
"""Double-precision oracle for the original bounded SM3 bloom kernels.

Image layout is rows of RGB(A) tuples, with no third-party dependency. The
game-space transfer functions come from agx_reference, not from a physical
lighting model. See docs/architecture/hdr-bloom-filter.md for filter equations
and sampling/state contracts. This reference does not model GPU FP16 rounding.
"""
from __future__ import annotations

from dataclasses import dataclass
import math

from agx_reference import DECODE_MODES, FP16_MAX, LUMA_WEIGHTS, decode

MAX_LEVELS = 6
MAX_DIMENSION = 16384
MAX_STRENGTH = 1.0
MAX_AUTHORED_GLOW_GAIN = 4.0


@dataclass(frozen=True)
class Params:
    levels: int = 5
    strength: float = 0.05
    threshold: float = 1.0
    knee: float = 0.5
    scatter: float = 0.7
    authored_glow_gain: float = 0.0
    highlight_gain: float = 0.05

    def validate(self):
        if type(self.levels) is not int or not 1 <= self.levels <= MAX_LEVELS:
            raise ValueError('levels must be an integer in [1,6]')
        for name, maximum in (('strength', MAX_STRENGTH), ('threshold', FP16_MAX),
                              ('knee', 1.0), ('scatter', 1.0),
                              ('authored_glow_gain', MAX_AUTHORED_GLOW_GAIN),
                              ('highlight_gain', 1.0)):
            v = getattr(self, name)
            if not math.isfinite(v) or not 0 <= v <= maximum:
                raise ValueError(f'invalid {name}')


def layout(width, height, levels=5):
    """Ceil-half chain, stopping after the first 1x1, including 1x1 input."""
    if any(type(v) is not int or not 1 <= v <= MAX_DIMENSION for v in (width, height)):
        raise ValueError('invalid dimensions')
    Params(levels=levels).validate()
    result = []
    for _ in range(levels):
        width, height = (width + 1) // 2, (height + 1) // 2
        result.append((width, height))
        if width == height == 1:
            break
    return result


def _validate_radiance(exposure, clamp_max, mode):
    if not math.isfinite(exposure) or not 0 < exposure <= FP16_MAX:
        raise ValueError('invalid exposure multiplier')
    if not math.isfinite(clamp_max) or mode not in DECODE_MODES:
        raise ValueError('invalid clamp or decode mode')


def exposed(rgb, exposure=1.0, clamp_max=0.0, mode='gamma2.2'):
    """Sanitize FP16 scene, decode, decoded-space clamp, expose, FP16 bound.

    NaN/negative/-inf become zero; +inf clamps to FP16 max. This intentionally
    defines the extraction scratch safety boundary. The original AgX scene
    contribution keeps its existing decode/clamp/exposure contract unchanged.
    """
    _validate_radiance(exposure, clamp_max, mode)
    if len(rgb) < 3:
        raise ValueError('RGB required')
    safe = tuple(min(v, FP16_MAX) if v >= 0 else 0.0 for v in rgb[:3])
    cap = min(clamp_max, FP16_MAX) if clamp_max > 0 else FP16_MAX
    return tuple(min(min(v, cap) * exposure, FP16_MAX) for v in decode(safe, mode))


def prefilter(rgb, params=Params(), exposure=1.0, clamp_max=0.0, mode='gamma2.2'):
    """Extract one decoded/exposed source sample BEFORE area reduction.

    Zero ``authored_glow_gain`` is the original RGB-only threshold branch and
    deliberately never observes alpha.  A positive gain uses saturated alpha
    as the native authored-glow mask and gates the threshold term by its
    complement.  RGB input has an implicit zero authored mask.
    """
    params.validate()
    e = exposed(rgb, exposure, clamp_max, mode)
    y = sum(c * w for c, w in zip(e, LUMA_WEIGHTS))
    t, k = params.threshold, params.threshold * params.knee
    q = min(max(y - t + k, 0.0), 2 * k)
    soft = q * (q / max(4 * k, 1e-10))
    weight = min(max(max(y - t, soft) / max(y, 1e-10), 0.0), 1.0)
    legacy = tuple(v * weight for v in e)
    if params.authored_glow_gain <= 0:
        return legacy
    raw_alpha = rgb[3] if len(rgb) >= 4 else 0.0
    alpha = min(raw_alpha, 1.0) if raw_alpha >= 0 else 0.0
    scale = (alpha * params.authored_glow_gain
             + (1 - alpha) * params.highlight_gain * weight)
    return tuple(min(v * scale, FP16_MAX) for v in e)


def _shape(image):
    height = len(image)
    width = len(image[0]) if height else 0
    layout(width, height, 1)
    if any(len(row) != width for row in image):
        raise ValueError('ragged image')
    if any(len(pixel) not in (3, 4) for row in image for pixel in row):
        raise ValueError('RGB or RGBA required')
    return width, height


def _area_weights(index, source_size, output_size):
    lo = index * source_size / output_size
    hi = min((index + 1) * source_size / output_size, source_size)
    base = math.floor(lo)
    weights = [max(min(i + 1, hi) - max(i, lo), 0.0) for i in range(base, base + 3)]
    total = sum(weights)
    return [(min(i, source_size - 1), w / total)
            for i, w in zip(range(base, base + 3), weights)]


def downsample(image):
    """Exact pixel-area average, with normalized overlap at odd dimensions."""
    width, height = _shape(image)
    ow, oh = (width + 1) // 2, (height + 1) // 2
    xs = [_area_weights(x, width, ow) for x in range(ow)]
    ys = [_area_weights(y, height, oh) for y in range(oh)]
    return [[tuple(min(sum(image[iy][ix][c] * wx * wy
                           for ix, wx in xs[x] for iy, wy in ys[y]), FP16_MAX)
                   for c in range(3)) for x in range(ow)] for y in range(oh)]


def bilinear(image, u, v):
    """D3D normalized texel-center convention and clamp addressing."""
    height, width = len(image), len(image[0])
    x, y = u * width - 0.5, v * height - 0.5
    ix, iy = math.floor(x), math.floor(y)
    fx, fy = x - ix, y - iy
    return tuple(sum(image[min(max(iy + dy, 0), height - 1)]
                          [min(max(ix + dx, 0), width - 1)][c] * wx * wy
                     for dx, wx in ((0, 1 - fx), (1, fx))
                     for dy, wy in ((0, 1 - fy), (1, fy))) for c in range(3))


def tent(image, width, height):
    """Normalized nine-bilinear-tap tent, same UV convention as the shaders."""
    layout(width, height, 1)
    sw, sh = _shape(image)
    taps = [(dx, dy, wx * wy) for dx, wx in ((-1, .25), (0, .5), (1, .25))
            for dy, wy in ((-1, .25), (0, .5), (1, .25))]
    def pixel(x, y):
        samples = [(bilinear(image, (x + .5) / width + dx / sw,
                            (y + .5) / height + dy / sh), w) for dx, dy, w in taps]
        return tuple(min(sum(v[c] * w for v, w in samples), FP16_MAX) for c in range(3))
    return [[pixel(x, y) for x in range(width)] for y in range(height)]


def tent_four_fetch(image, u, v):
    """Verification companion for HLSL fusion; the main oracle keeps 9 taps."""
    height, width = len(image), len(image[0])
    def axis(uv, size):
        position = uv * size - .5
        base = math.floor(position)
        f = position - base
        ld, rd = 3 - 2 * f, 1 + 2 * f
        return (((base - .5 + (2 - f) / ld) / size, ld / 4),
                ((base + 1.5 + f / rd) / size, rd / 4))
    samples = [(bilinear(image, x, y), wx * wy)
               for x, wx in axis(u, width) for y, wy in axis(v, height)]
    return tuple(min(sum(p[c] * w for p, w in samples), FP16_MAX) for c in range(3))


def bloom(image, params=Params(), exposure=1.0, clamp_max=0.0, mode='gamma2.2'):
    """Return full-resolution exposed-linear bloom WITHOUT additive strength."""
    params.validate()
    width, height = _shape(image)
    _validate_radiance(exposure, clamp_max, mode)
    current = [[prefilter(pixel, params, exposure, clamp_max, mode) for pixel in row]
               for row in image]
    chain = []
    for _ in layout(width, height, params.levels):
        current = downsample(current)
        chain.append(current)
    current = chain[-1]
    for fine in reversed(chain[:-1]):
        reconstruction = tent(current, len(fine[0]), len(fine))
        s = params.scatter
        current = [[tuple(min((1 - s) * a + s * b, FP16_MAX) for a, b in zip(p, q))
                    for p, q in zip(frow, rrow)] for frow, rrow in zip(fine, reconstruction)]
    return tent(current, width, height)


def composite(image, params=Params(), exposure=1.0, clamp_max=0.0, mode='gamma2.2'):
    """Exposed scene + strength*bloom; carry original alpha; no FP16 store.

    Feed RGB straight to AgX's inset/core with exposure=1 and decode=none.
    Do not apply decoded-space clamp to this sum.
    """
    filtered = bloom(image, params, exposure, clamp_max, mode)
    cap = min(clamp_max, FP16_MAX) if clamp_max > 0 else FP16_MAX
    def base(pixel):
        return tuple(min(v, cap) * exposure for v in decode(pixel[:3], mode))
    return [[tuple(a + params.strength * b for a, b in
                   zip(base(p), q)) + tuple(p[3:])
             for p, q in zip(row, brow)] for row, brow in zip(image, filtered)]
