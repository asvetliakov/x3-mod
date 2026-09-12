#!/usr/bin/env python3
"""Host reference for the AgX display transform of docs/architecture/hdr-scene-path.md §3.

Pure Python, double precision, no third-party modules (the analysis tools do
not use NumPy). ``src/temporal/agx.hlsl`` is the ``ps_3_0`` port; the two must
consume the same constants, which ``verification/analysis/test_agx_reference.py``
enforces by parsing ``src/temporal/agx.h`` and comparing every float against
this module.

Sources of the constants
------------------------
* Inset matrix ``M_IN``, outset matrix ``M_OUT``, ``MIN_EV``/``MAX_EV`` and the
  sixth-order sigmoid fit ``CONTRAST_COEFFICIENTS``: Benjamin Wrensch,
  *Minimal AgX Implementation* (iolite-engine.com blog, MIT), a reduction of
  Troy Sobotka's AgX (github.com/sobotka/AgX). Wrensch writes the matrices as
  GLSL column-major ``mat3`` constructors; they are transcribed here row-major
  acting on a column vector, which is how §3 prints them. Both are
  row-stochastic to about 1.4e-4, so a neutral input stays neutral.
* Look triples (``golden``: slope (1, 0.9, 0.5), power 0.8, saturation 0.8;
  ``punchy``: slope 1, power 1.35, saturation 1.4; ``none``: identity): the
  same source (its ``agxLook`` with ``AGX_LOOK`` 1 and 2), as an ASC CDL
  applied around Rec.709 luma (0.2126, 0.7152, 0.0722).
* Decode: §2 of the design, ``pow(max(x, 0), 2.2)`` extended above 1, the
  sRGB IEC 61966-2-1 piecewise EOTF extended above 1, or identity.

Blender 4.x and Godot 4.3 ship the fuller Rec.2020-inset AgX with their own
sigmoid fits, not these numbers; §3 names the minimal form deliberately because
it fits ``ps_3_0`` without a LUT. Nothing in this module is compiled or wired
into the renderer; it is the oracle the stage-2 shader will be measured against.

The output of :func:`agx` is *display encoded* (the outset matrix output is the
value to store in a plain ``A8R8G8B8`` target with ``SRGBWRITEENABLE=FALSE``);
do not apply a further ``pow(2.2)`` to it.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from typing import Sequence

# --- Constants (see the module docstring for provenance) ---------------------
M_IN = (
    (0.842479062253094, 0.0784335999999992, 0.0792237451477643),
    (0.0423282422610123, 0.878468636469772, 0.0791661274605434),
    (0.0423756549057051, 0.0784336, 0.879142973793104),
)
M_OUT = (
    (1.19687900512017, -0.0980208811401368, -0.0990297440797205),
    (-0.0528968517574562, 1.15190312990417, -0.0989611408712536),
    (-0.0529716355144438, -0.0980434501171241, 1.15107367264116),
)
MIN_EV = -12.47393
MAX_EV = 4.026069
LOG_FLOOR = 1e-10
# Highest power first: 15.5 x^6 - 40.14 x^5 + 31.96 x^4 - 6.868 x^3 + 0.4298 x^2 + 0.1191 x - 0.00232
CONTRAST_COEFFICIENTS = (15.5, -40.14, 31.96, -6.868, 0.4298, 0.1191, -0.00232)
LUMA_WEIGHTS = (0.2126, 0.7152, 0.0722)
# look name -> (slope rgb, offset rgb, power rgb, saturation)
LOOKS = {
    'none': ((1.0, 1.0, 1.0), (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), 1.0),
    'golden': ((1.0, 0.9, 0.5), (0.0, 0.0, 0.0), (0.8, 0.8, 0.8), 0.8),
    'punchy': ((1.0, 1.0, 1.0), (0.0, 0.0, 0.0), (1.35, 1.35, 1.35), 1.4),
}
DECODE_MODES = ('gamma2.2', 'srgb', 'none')
DECODE_GAMMA = 2.2
FP16_MAX = 65504.0  # what the host uploads as the clamp when X3M_HDR_CLAMP is unset


# --- Small vector helpers ----------------------------------------------------
def _clamp(x: float, lo: float, hi: float) -> float:
    return lo if x < lo else hi if x > hi else x


def mat3_mul(m: Sequence[Sequence[float]], v: Sequence[float]) -> tuple:
    """Row-major 3x3 times a column vector: three dot products, as the shader does."""
    return tuple(m[r][0] * v[0] + m[r][1] * v[1] + m[r][2] * v[2] for r in range(3))


def luma(v: Sequence[float]) -> float:
    return LUMA_WEIGHTS[0] * v[0] + LUMA_WEIGHTS[1] * v[1] + LUMA_WEIGHTS[2] * v[2]


# --- Stages ------------------------------------------------------------------
def decode(v: Sequence[float], mode: str = 'gamma2.2') -> tuple:
    """Engine-space value -> scene-linear, per §2. Extended above 1 by the same curve."""
    if mode == 'none':
        return tuple(float(c) for c in v)
    if mode == 'gamma2.2':
        return tuple(max(float(c), 0.0) ** DECODE_GAMMA for c in v)
    if mode == 'srgb':
        out = []
        for c in v:
            c = max(float(c), 0.0)
            out.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
        return tuple(out)
    raise ValueError(f'unknown decode mode {mode!r}; expected one of {DECODE_MODES}')


def log_encode(c: float) -> float:
    """Single channel: clamp(log2(max(c, 1e-10)), MIN_EV, MAX_EV) normalised to 0..1."""
    ev = _clamp(math.log2(max(c, LOG_FLOOR)), MIN_EV, MAX_EV)
    return (ev - MIN_EV) / (MAX_EV - MIN_EV)


def log_decode(t: float) -> float:
    """Inverse of :func:`log_encode` on the clamped range: 0..1 -> 2**MIN_EV .. 2**MAX_EV."""
    return 2.0 ** (MIN_EV + _clamp(t, 0.0, 1.0) * (MAX_EV - MIN_EV))


def contrast(x: float) -> float:
    """Sixth-order polynomial fit of the AgX sigmoid, evaluated on the 0..1 log encoding."""
    x2 = x * x
    x4 = x2 * x2
    a6, a5, a4, a3, a2, a1, a0 = CONTRAST_COEFFICIENTS
    return a6 * x4 * x2 + a5 * x4 * x + a4 * x4 + a3 * x2 * x + a2 * x2 + a1 * x + a0


def look(v: Sequence[float], name: str = 'none') -> tuple:
    """ASC CDL (slope, offset, power) plus saturation about Rec.709 luma."""
    if name not in LOOKS:
        raise ValueError(f'unknown look {name!r}; expected one of {sorted(LOOKS)}')
    slope, offset, power, sat = LOOKS[name]
    y = luma(v)
    graded = tuple(max(v[i] * slope[i] + offset[i], 0.0) ** power[i] for i in range(3))
    return tuple(y + sat * (graded[i] - y) for i in range(3))


def agx_core(v: Sequence[float]) -> tuple:
    """Inset, log encode, contrast: AgX before the look and the outset."""
    v = mat3_mul(M_IN, v)
    return tuple(contrast(log_encode(c)) for c in v)


def output(v: Sequence[float]) -> tuple:
    """Outset and saturate. The result is already display encoded."""
    return tuple(_clamp(c, 0.0, 1.0) for c in mat3_mul(M_OUT, v))


def agx(rgb_linear: Sequence[float], exposure_ev: float = 0.0, look_name: str = 'none', **kw) -> tuple:
    """Scene-linear Rec.709 RGB -> display-encoded RGB in 0..1.

    ``look`` is accepted as a keyword alias of ``look_name`` so the call reads
    ``agx(rgb, exposure_ev=0.0, look="base")``; ``"base"`` means ``"none"``.
    """
    name = kw.pop('look', look_name)
    if kw:
        raise TypeError(f'unexpected arguments {sorted(kw)}')
    if name == 'base':
        name = 'none'
    scale = 2.0 ** exposure_ev
    v = tuple(float(c) * scale for c in rgb_linear)
    return output(look(agx_core(v), name))


def tonemap_engine(rgb_engine: Sequence[float], exposure: float = 1.0, decode_mode: str = 'gamma2.2',
                   look_name: str = 'none', clamp_max: float = 0.0) -> tuple:
    """The full fragment: decode(engine) -> optional X3M_HDR_CLAMP -> exposure -> AgX.

    ``exposure`` is the multiplier ``exp2(EV_adapted)`` (not an EV). ``clamp_max``
    <= 0 is the switch default (off): the shader always applies ``min`` and the
    host uploads ``FP16_MAX`` in that case, so the reference does the same.
    """
    v = decode(rgb_engine, decode_mode)
    limit = min(clamp_max, FP16_MAX) if clamp_max > 0 else FP16_MAX
    v = tuple(min(c, limit) for c in v)
    v = tuple(c * exposure for c in v)
    return output(look(agx_core(v), look_name))


# --- Tables ------------------------------------------------------------------
def ramp_values(start: float = 0.001, stop: float = 64.0, steps_per_octave: int = 4) -> list:
    """Geometric ramp from ``start`` to ``stop`` inclusive, ``steps_per_octave`` samples per doubling."""
    octaves = math.log2(stop / start)
    count = int(round(octaves * steps_per_octave))
    return [start * 2.0 ** (i * octaves / count) for i in range(count + 1)]


def ramp_table(look_name: str = 'none', exposure_ev: float = 0.0) -> dict:
    rows = []
    for x in ramp_values():
        neutral = agx((x, x, x), exposure_ev, look_name)
        red = agx((x, 0.0, 0.0), exposure_ev, look_name)
        green = agx((0.0, x, 0.0), exposure_ev, look_name)
        blue = agx((0.0, 0.0, x), exposure_ev, look_name)
        rows.append({
            'input': round(x, 9),
            'neutral': round(neutral[0], 6),
            'neutral_spread': round(max(neutral) - min(neutral), 9),
            'red': [round(c, 6) for c in red],
            'green': [round(c, 6) for c in green],
            'blue': [round(c, 6) for c in blue],
        })
    return {
        'source': 'tools/analysis/agx_reference.py',
        'look': look_name,
        'exposure_ev': exposure_ev,
        'constants': {
            'min_ev': MIN_EV, 'max_ev': MAX_EV, 'log_floor': LOG_FLOOR,
            'inset': [list(r) for r in M_IN], 'outset': [list(r) for r in M_OUT],
            'contrast': list(CONTRAST_COEFFICIENTS), 'luma': list(LUMA_WEIGHTS),
        },
        'mid_grey_display': round(agx((0.18,) * 3)[0], 6),
        'white_display': round(agx((1.0,) * 3)[0], 6),
        'rows': rows,
    }


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--look', default='none', choices=sorted(LOOKS))
    parser.add_argument('--ev', type=float, default=0.0, help='exposure in EV applied before AgX')
    parser.add_argument('--out', default=str(Path(__file__).resolve().parents[2] / 'verification/results/agx-ramp.json'))
    parser.add_argument('--no-write', action='store_true')
    args = parser.parse_args(argv)
    table = ramp_table(args.look, args.ev)
    print(f'AgX ramp, look={args.look}, EV={args.ev:+.2f}; mid-grey 0.18 -> {table["mid_grey_display"]:.4f}, '
          f'1.0 -> {table["white_display"]:.4f}')
    print(f'{"input":>12} {"neutral":>9} {"spread":>10} {"red R":>8} {"green G":>8} {"blue B":>8}')
    for row in table['rows']:
        print(f'{row["input"]:12.6f} {row["neutral"]:9.5f} {row["neutral_spread"]:10.2e} '
              f'{row["red"][0]:8.5f} {row["green"][1]:8.5f} {row["blue"][2]:8.5f}')
    if not args.no_write:
        Path(args.out).write_text(json.dumps(table, indent=1, sort_keys=True) + '\n')
        print(f'wrote {args.out}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
