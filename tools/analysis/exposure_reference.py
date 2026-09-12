#!/usr/bin/env python3
"""Host reference for the exposure model of docs/architecture/hdr-scene-path.md §3
and its stage-2 space-aware meter.

Pure functions, double precision, no third-party modules. Names and parameter
orders are the ones the C++ port (``src/renderer/exposure.h``) uses, so the two
stay mechanical mirrors (verification/analysis/test_exposure_port.py compiles
the port natively and replays it through this module).

Pipeline, in the order the frame runs it (§3, §4 step 5, "Stage 2
implementation" -> the space-aware meter):

    level 0     v = log2(clamp(luma(decode(scene.rgb)), METER_FLOOR, meter_clip))
    tiles       4x4 reductions of (mean v, max v) until no axis exceeds TILE_MAX
                                                          -> the tile image, read back
    weight      w = edge + (1 - edge) * (1 + cos(pi * r / r_corner)) / 2   (r: distance from the centre)
    lit         tile.mean >= log2(meter_bg)                (the black sky is excluded)
    neutral     lit < meter_min_lit * tiles                (nothing to meter: EV 0)
    d           = log2(key) - weighted median(lit tile means)
    ev_key      = (d if d >= 0 else d * key_pull) + ev_offset   (neutral: ev_offset)
    ev_limit    = log2(white_target * TONEMAP_WHITE) - p99(tile max)     (unweighted)
    ev_fresh    = clamp(min(ev_key, ev_limit), ev_min, ev_max)
    ev_target   = ev_fresh if |ev_fresh - ev_target| > ev_deadband else ev_target   (held)
    tau         = tau_down if ev_target < ev_adapted else tau_up
    ev_adapted += (ev_target - ev_adapted) * (1 - exp2(-dt / (tau * ln 2)))
    exposure    = exp2(ev_adapted)
    k           = exposure                       # TAA luminance weighting 1 / (1 + k * luma)

Why not the plain log-average of the whole frame (the first stage-2 rule):
a space scene is mostly black. Its geometric mean sits at the meter floor
(run 15 of bottle X3: luma_mean median 0.0017 over 132 metered frames), the
key rule then asks for +6..+8 EV and a lit station is blown out. The tile
statistic meters the lit content only (the median of the lit tiles maps to
the key, the lift in full), pulls a bright full frame down only gently
(``key_pull``: a white menu at 1.0 lands at -0.62 EV, not at mid-grey), and
uses the unweighted p99 of tile maxima to limit the fresh target
(``ev_limit``). This is not a bound on every displayed pixel: metering clips
input luminance, the brightest percentile can be excluded, and EV bounds,
adaptation and the dead band can exceed the freshly computed limit. Lit
tiles are centre-weighted (a raised cosine, ``edge_weight`` at the corners)
to reduce edge influence on the key statistic; the highlight statistic stays
unweighted. The dead band holds the target while the
freshly metered one stays within ``ev_deadband`` of it (the held target
equals the adapted EV once settled), so small changes while turning do not
drift the exposure; adaptation still converges exactly to the held target.
``avg_log_l`` (the old statistic) is still reported for continuity.

``exp2(-dt / (tau * ln 2))`` is ``exp(-dt / tau)`` written with the single-slot
SM3 instruction; ``tau`` is therefore an ordinary first-order time constant
(63.2 % of the way after ``tau`` seconds). ``dt`` is clamped to
[DT_MIN, DT_MAX] before the step so a hitch cannot jump the exposure. The
tonemap of frame n consumes the EV adapted at frame n-1.

Switch defaults: key 0.18 (``X3M_HDR_KEY``), EV offset 0 (``X3M_HDR_EV``),
tau_up 0.4 s / tau_down 1.2 s (``X3M_HDR_ADAPT_UP/DOWN``), meter clip 64,
meter floor 1e-4, EV range -3..+2 (``X3M_HDR_EV_MIN/MAX``), background floor
1/512 (``X3M_HDR_METER_BG``), minimum lit fraction 1 % (``X3M_HDR_METER_MIN_LIT``),
white target 0.9 of the AgX white (``X3M_HDR_WHITE_TARGET``), key pull 0.25
(``X3M_HDR_KEY_PULL``), dead band 0.25 EV (``X3M_HDR_EV_DEADBAND``), edge
weight 0.35 (``X3M_HDR_METER_EDGE_WEIGHT``).
"""
from __future__ import annotations

import math
import sys
from pathlib import Path
from typing import Iterable, Sequence

sys.path.insert(0, str(Path(__file__).resolve().parent))
from agx_reference import decode, luma, MAX_EV  # noqa: E402

KEY = 0.18
EV_OFFSET = 0.0
EV_MIN = -3.0
EV_MAX = 2.0
TAU_UP = 0.4
TAU_DOWN = 1.2
DT_MIN = 1.0 / 240.0
DT_MAX = 1.0 / 5.0
METER_FLOOR = 1e-4
METER_CLIP = 64.0
METER_BG = 1.0 / 512.0
METER_MIN_LIT = 0.01
WHITE_TARGET = 0.9
KEY_PULL = 0.25
EV_DEADBAND = 0.25
EDGE_WEIGHT = 0.35
TONEMAP_WHITE = 2.0 ** MAX_EV      # the AgX input that maps to full white (16.29)
TILE_MAX = 128                     # the chain stops when no tile-image axis exceeds this
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


def reduce_tiles(level: Sequence[float], width: int, height: int, factor: int = 4, tile_max: int = TILE_MAX) -> tuple:
    """Emulate the GPU reduction chain: ``factor``x per axis per level, edge-clamped taps.

    ``level`` is row-major ``width*height`` of level-0 values. Each level is
    ``ceil(w/factor)`` by ``ceil(h/factor)`` and every output texel averages
    (mean channel) and maximises (max channel) ``factor*factor`` taps whose
    coordinates are clamped to the source (the CLAMP sampler state), so odd
    sizes weight edge texels a little more than :func:`reduce_mean` would.
    At least one reduction runs (the GPU's level 0 folds the first one); the
    chain then stops when neither axis exceeds ``tile_max``; returns
    ``(means, maxes, tile_width, tile_height)``. Dimensions that are powers
    of ``factor`` reduce exactly to the mean.
    """
    if width <= 0 or height <= 0 or len(level) != width * height or tile_max < 1:
        raise ValueError('level size does not match width*height')
    means, maxes = list(level), list(level)
    first = True   # level 0 always folds one reduction (the GPU never stores the full-resolution level)
    while first or width > tile_max or height > tile_max:
        first = False
        nw, nh = -(-width // factor), -(-height // factor)
        out_mean, out_max = [], []
        for y in range(nh):
            for x in range(nw):
                acc, peak = 0.0, -math.inf
                for ty in range(factor):
                    sy = min(y * factor + ty, height - 1)
                    for tx in range(factor):
                        sx = min(x * factor + tx, width - 1)
                        acc += means[sy * width + sx]
                        peak = max(peak, maxes[sy * width + sx])
                out_mean.append(acc / (factor * factor))
                out_max.append(peak)
        means, maxes, width, height = out_mean, out_max, nw, nh
    return means, maxes, width, height


def reduce_chain(level: Sequence[float], width: int, height: int, factor: int = 4) -> float:
    """The mean channel reduced all the way to 1x1 (the stage-2 1x1 meter; ``avg_log_l`` of a tile image)."""
    return reduce_tiles(level, width, height, factor, 1)[0][0]


def tile_weights(width: int, height: int, edge_weight: float = EDGE_WEIGHT) -> list:
    """Centre weights of a ``width`` x ``height`` tile image (row-major): a raised cosine of the
    distance from the frame centre, 1 at the centre and ``edge_weight`` at the corners."""
    edge = _clamp(edge_weight, 0.0, 1.0)
    corner = math.sqrt(0.5)
    out = []
    for y in range(height):
        for x in range(width):
            dx, dy = (x + 0.5) / width - 0.5, (y + 0.5) / height - 0.5
            r = min(1.0, math.sqrt(dx * dx + dy * dy) / corner)
            out.append(edge + (1.0 - edge) * 0.5 * (1.0 + math.cos(math.pi * r)))
    return out


def meter_statistics(means: Sequence[float], maxes: Sequence[float], meter_bg: float = METER_BG,
                     meter_min_lit: float = METER_MIN_LIT, meter_floor: float = METER_FLOOR,
                     weights: Sequence[float] | None = None) -> dict:
    """The space-aware statistic of one tile image (log2 units).

    ``avg_log_l``: the mean of every tile mean (continuity with the 1x1 meter);
    ``lit``/``lit_fraction``: tiles whose mean is at least ``log2(meter_bg)``
    (by count); ``lit_median_log``: the weighted median of the lit tiles'
    means (the first value of the ascending order at which the cumulative
    weight reaches half the lit weight; every tile weighs 1 without
    ``weights``), the value the key rule maps to the key; ``lit_mean_log``:
    their weighted arithmetic mean (the geometric mean of the lit
    luminance); ``p99_max_log``: the tile maxima's unweighted 99th percentile
    (index ``tiles * 99 // 100`` of the ascending order); ``neutral``: fewer
    than ``meter_min_lit`` of the tiles are lit. Non-finite tiles read as the
    floor, as the port does.
    """
    if not means or len(means) != len(maxes) or (weights is not None and len(weights) != len(means)):
        raise ValueError('empty tile image or channel mismatch')
    floor_log = math.log2(meter_floor)
    bg_log = math.log2(meter_bg)
    means = [m if math.isfinite(m) else floor_log for m in means]
    maxes = [m if math.isfinite(m) else floor_log for m in maxes]
    tiles = len(means)
    if weights is None:
        weights = [1.0] * tiles
    lit_samples = sorted((m, w) for m, w in zip(means, weights) if m >= bg_log)
    lit = len(lit_samples)
    lit_weight = math.fsum(w for _, w in lit_samples)
    median = floor_log
    if lit and lit_weight > 0:
        cumulative, median = 0.0, lit_samples[-1][0]
        for value, w in lit_samples:
            cumulative += w
            if cumulative >= lit_weight * 0.5:
                median = value
                break
    ordered_max = sorted(maxes)
    return {
        'tiles': tiles, 'lit': lit,
        'avg_log_l': math.fsum(means) / tiles,
        'lit_fraction': lit / tiles,
        'lit_weight': lit_weight,
        'lit_median_log': median,
        'lit_mean_log': math.fsum(m * w for m, w in lit_samples) / lit_weight if lit and lit_weight > 0 else floor_log,
        'p99_max_log': ordered_max[min(tiles - 1, tiles * 99 // 100)],
        'neutral': lit < meter_min_lit * tiles,
    }


def meter_image(pixels: Sequence[Sequence[float]], width: int, height: int, decode_mode: str = 'gamma2.2',
                meter_clip: float = METER_CLIP, meter_floor: float = METER_FLOOR,
                meter_bg: float = METER_BG, meter_min_lit: float = METER_MIN_LIT, tile_max: int = TILE_MAX,
                edge_weight: float = EDGE_WEIGHT) -> dict:
    """Whole-image metering: the tile image and its centre-weighted statistic, plus ``meter_clipped_fraction`` and ``level0``."""
    if len(pixels) != width * height:
        raise ValueError('pixel count does not match width*height')
    logs = [meter_level0(p, decode_mode, meter_clip, meter_floor) for p in pixels]
    means, maxes, tw, th = reduce_tiles(logs, width, height, 4, tile_max)
    stats = meter_statistics(means, maxes, meter_bg, meter_min_lit, meter_floor, tile_weights(tw, th, edge_weight))
    clipped = sum(1 for p in pixels if meter_clipped(p, decode_mode, meter_clip))
    stats.update({'meter_clipped_fraction': clipped / len(pixels), 'level0': logs,
                  'tile_means': means, 'tile_maxes': maxes, 'tile_width': tw, 'tile_height': th})
    return stats


# --- Target ------------------------------------------------------------------
def ev_key(lit_median_log: float, key: float = KEY, ev_offset: float = EV_OFFSET, key_pull: float = KEY_PULL) -> float:
    """The key rule on the lit median: the lift towards the key in full, the pull down scaled by ``key_pull``."""
    if not (key > 0) or not math.isfinite(lit_median_log):
        raise ValueError('key must be positive and the median finite')
    d = math.log2(key) - lit_median_log
    pull = _clamp(key_pull, 0.0, 1.0)
    return (d if d >= 0 else d * pull) + ev_offset


def ev_limit(p99_max_log: float, white_target: float = WHITE_TARGET, ev_max: float = EV_MAX) -> float:
    """The highlight limit: the EV at which the tile maxima's 99th percentile reaches ``white_target`` of the AgX white."""
    if not (white_target > 0) or not math.isfinite(p99_max_log):
        return ev_max
    return math.log2(white_target * TONEMAP_WHITE) - p99_max_log


def exposure_target(stats: dict, key: float = KEY, ev_offset: float = EV_OFFSET,
                    ev_min: float = EV_MIN, ev_max: float = EV_MAX,
                    white_target: float = WHITE_TARGET, key_pull: float = KEY_PULL) -> dict:
    """``ev_key``, ``ev_limit`` and the clamped ``ev_target`` = min of the two."""
    key_ev = ev_offset if stats['neutral'] else ev_key(stats['lit_median_log'], key, ev_offset, key_pull)
    limit = ev_limit(stats['p99_max_log'], white_target, ev_max)
    return {'ev_key': key_ev, 'ev_limit': limit, 'ev_target': _clamp(min(key_ev, limit), ev_min, ev_max)}


def ev_target(stats: dict, key: float = KEY, ev_offset: float = EV_OFFSET,
              ev_min: float = EV_MIN, ev_max: float = EV_MAX,
              white_target: float = WHITE_TARGET, key_pull: float = KEY_PULL) -> float:
    """EV the adaptation drives towards for one tile statistic."""
    return exposure_target(stats, key, ev_offset, ev_min, ev_max, white_target, key_pull)['ev_target']


def uniform_statistics(log_l: float, tiles: int = 256) -> dict:
    """The statistic of a uniform lit frame (every tile mean and max = ``log_l``): the key rule alone applies."""
    return meter_statistics([log_l] * tiles, [log_l] * tiles)


def apply_deadband(held: float | None, fresh: float, ev_deadband: float = EV_DEADBAND) -> float:
    """The held target moves to ``fresh`` only when the two differ by more than the dead band (``held`` None: no target yet)."""
    if held is None or not math.isfinite(held):
        return fresh
    return fresh if abs(fresh - held) > max(0.0, ev_deadband) else held


# --- Adaptation --------------------------------------------------------------
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
    """``X3M_HDR_EXPOSURE=auto|manual``: manual forces ``X3M_HDR_EV_MANUAL`` and skips the chain."""
    if mode == 'manual':
        if not math.isfinite(ev_manual):
            raise ValueError('manual EV must be finite')
        return ev_manual
    if mode == 'auto':
        return ev_adapted
    raise ValueError(f'unknown exposure mode {mode!r}; expected one of {EXPOSURE_MODES}')


def simulate(stats_frames: Sequence[dict], dt_frames: Sequence[float], ev_initial: float = 0.0,
             key: float = KEY, ev_offset: float = EV_OFFSET, ev_min: float = EV_MIN, ev_max: float = EV_MAX,
             tau_up: float = TAU_UP, tau_down: float = TAU_DOWN,
             white_target: float = WHITE_TARGET, key_pull: float = KEY_PULL, ev_deadband: float = EV_DEADBAND,
             targets: list | None = None) -> list:
    """Run the frame loop on per-frame tile statistics (or bare log2 values, read as uniform frames):
    returns the EV each frame's tonemap consumes (the previous frame's state). ``targets``, when given
    a list, receives the held target after each frame's step."""
    if len(stats_frames) != len(dt_frames):
        raise ValueError('one dt per frame')
    ev, held = ev_initial, None
    consumed = []
    for stats, dt in zip(stats_frames, dt_frames):
        if not isinstance(stats, dict):
            stats = uniform_statistics(float(stats))
        consumed.append(ev)
        fresh = ev_target(stats, key, ev_offset, ev_min, ev_max, white_target, key_pull)
        held = apply_deadband(held, fresh, ev_deadband)
        if targets is not None:
            targets.append(held)
        ev = adapt(ev, held, dt, tau_up, tau_down, ev_min, ev_max)
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
