#!/usr/bin/env python3
"""Host reference for the exposure model of docs/architecture/hdr-scene-path.md §3.

Pure functions, double precision, no third-party modules. Names and parameter
orders are the ones the design gives so the C++ port (``src/renderer/exposure.h``,
stage 2) is mechanical. Nothing here is wired into the renderer.

Pipeline, in the order the frame runs it (§3 and §4 step 5):

    level 0     L = luma(decode(scene.rgb));  out = log2(clamp(L, METER_FLOOR, meter_clip))
    levels 1..n out = mean of the 16 taps of a 4x4 block          -> avg_log_l  (1x1 R32F)
    ev_target   = clamp(log2(key) - avg_log_l + ev_offset, ev_min, ev_max)
    tau         = tau_down if ev_target < ev_adapted else tau_up
    ev_adapted += (ev_target - ev_adapted) * (1 - exp2(-dt / (tau * ln 2)))
    exposure    = exp2(ev_adapted)
    k           = exposure                       # TAA luminance weighting 1 / (1 + k * luma)

``exp2(-dt / (tau * ln 2))`` is ``exp(-dt / tau)`` written with the single-slot
SM3 instruction; ``tau`` is therefore an ordinary first-order time constant
(63.2 % of the way after ``tau`` seconds). ``dt`` is clamped to
[DT_MIN, DT_MAX] before the step so a hitch cannot jump the exposure. The
tonemap of frame n consumes the EV adapted at frame n-1.

Switch defaults (§3): key 0.18 (``X3M_HDR_KEY``), EV offset 0
(``X3M_HDR_EV_OFFSET``), tau_up 0.4 s / tau_down 1.2 s
(``X3M_HDR_ADAPT_UP/DOWN``), meter clip 64 (``Lmeter_clip``), meter floor 1e-4.
``EV_MIN``/``EV_MAX`` are not numbered in the original §3 text; +-8 EV is
chosen here (a 256x range either way, far beyond the 0.01..64 luminance span
the meter clamps to) and recorded in §3.
"""
from __future__ import annotations

import math
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))
from agx_reference import decode, luma  # noqa: E402

KEY = 0.18
EV_OFFSET = 0.0
EV_MIN = -8.0
EV_MAX = 8.0
TAU_UP = 0.4
TAU_DOWN = 1.2
DT_MIN = 1.0 / 240.0
DT_MAX = 1.0 / 5.0
METER_FLOOR = 1e-4
METER_CLIP = 64.0
LN2 = math.log(2.0)
EXPOSURE_MODES = ('auto', 'manual')


def _clamp(x: float, lo: float, hi: float) -> float:
    return lo if x < lo else hi if x > hi else x


# --- Metering ----------------------------------------------------------------
def meter_level0(rgb_engine: Sequence[float], decode_mode: str = 'gamma2.2',
                 meter_clip: float = METER_CLIP, meter_floor: float = METER_FLOOR) -> float:
    """Level-0 texel: log2 of the clamped decoded luminance of one scene pixel."""
    L = luma(decode(rgb_engine, decode_mode))
    return math.log2(_clamp(L, meter_floor, meter_clip))


def meter_clipped(rgb_engine: Sequence[float], decode_mode: str = 'gamma2.2',
                  meter_clip: float = METER_CLIP) -> bool:
    """True when the pixel hit ``meter_clip`` (feeds ``meter_clipped_fraction`` in the log line)."""
    return luma(decode(rgb_engine, decode_mode)) > meter_clip


def reduce_mean(values: Iterable[float]) -> float:
    """Exact arithmetic mean of the level-0 texels: the ideal the chain approximates."""
    values = list(values)
    if not values:
        raise ValueError('empty meter input')
    return math.fsum(values) / len(values)


def reduce_chain(level: Sequence[float], width: int, height: int, factor: int = 4) -> float:
    """Emulate the GPU reduction chain: ``factor``x per axis per level, edge-clamped taps.

    ``level`` is row-major ``width*height``. Each level is ``ceil(w/factor)`` by
    ``ceil(h/factor)`` and every output texel averages ``factor*factor`` taps
    whose coordinates are clamped to the source (the CLAMP sampler state), so
    odd sizes weight edge texels a little more than :func:`reduce_mean` would.
    Dimensions that are powers of ``factor`` reduce exactly to the mean.
    """
    if width <= 0 or height <= 0 or len(level) != width * height:
        raise ValueError('level size does not match width*height')
    level = list(level)
    while width > 1 or height > 1:
        nw, nh = -(-width // factor), -(-height // factor)
        out = []
        for y in range(nh):
            for x in range(nw):
                acc = 0.0
                for ty in range(factor):
                    sy = min(y * factor + ty, height - 1)
                    for tx in range(factor):
                        sx = min(x * factor + tx, width - 1)
                        acc += level[sy * width + sx]
                out.append(acc / (factor * factor))
        level, width, height = out, nw, nh
    return level[0]


def meter_image(pixels: Sequence[Sequence[float]], decode_mode: str = 'gamma2.2',
                meter_clip: float = METER_CLIP, meter_floor: float = METER_FLOOR) -> dict:
    """Whole-image metering: ``avg_log_l`` (exact mean) plus ``meter_clipped_fraction``."""
    logs = [meter_level0(p, decode_mode, meter_clip, meter_floor) for p in pixels]
    clipped = sum(1 for p in pixels if meter_clipped(p, decode_mode, meter_clip))
    return {'avg_log_l': reduce_mean(logs), 'meter_clipped_fraction': clipped / len(pixels),
            'level0': logs}


# --- Adaptation --------------------------------------------------------------
def ev_target(avg_log_l: float, key: float = KEY, ev_offset: float = EV_OFFSET,
              ev_min: float = EV_MIN, ev_max: float = EV_MAX) -> float:
    """EV that maps the metered geometric-mean luminance onto ``key``."""
    if not (key > 0) or not math.isfinite(avg_log_l):
        raise ValueError('key must be positive and avg_log_l finite')
    return _clamp(math.log2(key) - avg_log_l + ev_offset, ev_min, ev_max)


def clamp_dt(dt: float, dt_min: float = DT_MIN, dt_max: float = DT_MAX) -> float:
    """QPC interval between boundary hits, clamped so a hitch or a loading pause cannot step the EV."""
    if not math.isfinite(dt):
        return dt_max
    return _clamp(dt, dt_min, dt_max)


def adapt_rate(dt: float, tau: float) -> float:
    """Fraction of the remaining distance covered in ``dt``: 1 - exp2(-dt / (tau ln 2)) == 1 - exp(-dt / tau)."""
    if not (tau > 0):
        raise ValueError('tau must be positive')
    return 1.0 - 2.0 ** (-dt / (tau * LN2))


def adapt(ev_adapted: float, ev_target_value: float, dt: float,
          tau_up: float = TAU_UP, tau_down: float = TAU_DOWN,
          ev_min: float = EV_MIN, ev_max: float = EV_MAX) -> float:
    """One adaptation step. ``dt`` is clamped here; the result is clamped to [ev_min, ev_max].

    The rate constant is chosen by the direction of travel: ``tau_down`` when the
    target is darker (lower EV) than the current state, ``tau_up`` otherwise.
    """
    dt = clamp_dt(dt)
    tau = tau_down if ev_target_value < ev_adapted else tau_up
    ev = ev_adapted + (ev_target_value - ev_adapted) * adapt_rate(dt, tau)
    return _clamp(ev, ev_min, ev_max)


def exposure_multiplier(ev: float) -> float:
    """``exposure = exp2(EV)``, the scale applied to scene-linear before AgX."""
    return 2.0 ** ev


def resolve_ev(mode: str, ev_manual: float, ev_adapted: float) -> float:
    """``X3M_HDR_EXPOSURE=auto|manual``: manual forces ``X3M_HDR_EV`` and skips the chain."""
    if mode == 'manual':
        if not math.isfinite(ev_manual):
            raise ValueError('manual EV must be finite')
        return ev_manual
    if mode == 'auto':
        return ev_adapted
    raise ValueError(f'unknown exposure mode {mode!r}; expected one of {EXPOSURE_MODES}')


def simulate(avg_log_l_frames: Sequence[float], dt_frames: Sequence[float], ev_initial: float = 0.0,
             key: float = KEY, ev_offset: float = EV_OFFSET, ev_min: float = EV_MIN, ev_max: float = EV_MAX,
             tau_up: float = TAU_UP, tau_down: float = TAU_DOWN) -> list:
    """Run the frame loop: returns the EV each frame's tonemap consumes (the previous frame's state)."""
    if len(avg_log_l_frames) != len(dt_frames):
        raise ValueError('one dt per frame')
    ev = ev_initial
    consumed = []
    for avg_log_l, dt in zip(avg_log_l_frames, dt_frames):
        consumed.append(ev)
        ev = adapt(ev, ev_target(avg_log_l, key, ev_offset, ev_min, ev_max), dt, tau_up, tau_down, ev_min, ev_max)
    return consumed


# --- TAA luminance weighting -------------------------------------------------
def taa_k(exposure: float) -> float:
    """``k`` uploaded to the resolve: the adapted exposure multiplier itself. 0 is the identity."""
    if not math.isfinite(exposure) or exposure < 0:
        raise ValueError('exposure must be finite and non-negative')
    return exposure


def luma_weight(luma_value: float, k: float) -> float:
    """``w = 1 / (1 + k * luma)`` applied to every tap before the neighbourhood statistics."""
    return 1.0 / (1.0 + k * luma_value)


def weight_color(rgb: Sequence[float], k: float) -> tuple:
    w = luma_weight(luma(rgb), k)
    return tuple(c * w for c in rgb)


def unweight_color(rgb_weighted: Sequence[float], k: float) -> tuple:
    """Inverse of :func:`weight_color`: ``c / (1 - k * luma(c_weighted))``."""
    return tuple(c / (1.0 - k * luma(rgb_weighted)) for c in rgb_weighted)
