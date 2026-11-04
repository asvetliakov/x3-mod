#!/usr/bin/env python3
"""Offline nebula sky bake (docs/architecture/sector-background-modern.md, Decision B and answer 3).

numpy only. Two kinds of texture, picked by name from the winning catalogue member:

- nebula background parts, dds/nebula_<family>_background*.pck: decode (lod_atlas.decode_dds), keep the exact-zero
  mask, deband (self-guided filter, r = 8 at source resolution, eps for an 8/255 edge, applied only where the local
  17x17 range is below 12/255), resample 2x Catmull-Rom while the result stays within 4096x2048 texels, opt-in
  hue-locked detail (value noise along the base colour), ordered 8x8 Bayer dither, DXT1 re-encode with the zeros
  restored (a block holding a zero texel keeps a black endpoint, alpha 1 everywhere), no mips, the stock header
  fields kept, the X3ML mark payload 3 (sky nebula) in dwReserved1[0..2];
- painted star textures, dds/nebula_<family>_stars_<colour>_diff.pck: byte-identical texels and mips, header mark
  payload 2 (sky stars) with the descriptor of answer 1: slot = fnv1a32(family) & 0xff, lane = hue class of the
  colour token, K = bright-peak count // 8 (3x3 local maxima with luma > 96, sky_texture_stats.py's count).

Eligibility (answer 3): DXT1 parts with a short side of at least MIN_SIDE; 24/32-bit uncompressed parts only with
--sky-uncompressed dxt1 (AP's 861 stay stock by default); DXT3/DXT5 parts (alpha carriers) are never touched; a
loose-file winner is skipped (a catalogue member does not shadow it).

Used by lod_overlay.py --batch (the `sky` row group of the overlay, reuse keyed on inputs_sha256) and standalone:

  python3 tools/analysis/sky_textures.py --game DIR --out SCRATCH [--families a,b] [--jobs N]

writes SCRATCH/dds/<member>.dds (unpacked bake) and SCRATCH/sky-bake.json (rows, timings, metrics); never into the
game directory. verification/results/sector-background-textures/sky_bake_metrics.py checks the thresholds of
answer 4 on such a directory; sky_contact_sheet.py draws source beside bake.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import gzip
import hashlib
import json
import multiprocessing
import os
import re
import struct
import sys
import time
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
import lod_atlas  # noqa: E402

RECIPE_ID = 'sky-nebula-1'          # bump on any change of the recipe's output for the same inputs
TOOL = 'tools/analysis/sky_textures.py'
PAYLOAD_SKY_STARS, PAYLOAD_SKY_NEBULA = 2, 3      # lod_texture_mark.h descriptor bits 24-31
MAX_OUT = (4096, 2048)              # largest output (w, h) texel budget: 4096x2048 DXT1 loads (one AP mod part)
MIN_SIDE = 512                      # parts smaller than this are not sky backgrounds (64x64 fog-card fills)
DEFAULTS = dict(deband_radius=8, deband_edge=8.0, deband_range=12.0, deband_ramp=4.0, upsample=True,
                detail=False, detail_amplitude=0.04, detail_octaves=3, detail_period=64,
                dither=1.0, bracket_length=10.0, pca_spread=16.0, uncompressed='skip')
STAR_LANES = ('white', 'blue', 'red', 'orange', 'yellow', 'green', 'cyan')   # lane = index; anything else 7
LANE_OTHER = 7
BRIGHT_PEAK_LUMA = 96
_STARS = re.compile(r'dds/nebula_(?P<family>[a-z0-9_]+?)_stars_(?P<colour>[a-z0-9]+)_diff\.(?P<ext>pck|dds)')
_PART = re.compile(r'dds/nebula_(?P<family>[a-z0-9_]+?)_background(?P<rest>[a-z0-9_]*)\.(?P<ext>pck|dds)')
_PART_NO = re.compile(r'part_?(\d+)')

progress = None   # optional callable(row, done, total): lod_overlay / x3m-regenerate per-texture console line


def fnv1a32(data):
    """fog_families.fnv1a32 (the profile-id hash), repeated here so the bake does not import the fog stack."""
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


SKY_TOOL_FILES = ('sky_textures.py', 'lod_atlas.py')   # the bake's sources (lod_atlas: DDS decode, PCA encoder)


def tool_sha256():
    """sha256 over sky_textures.py then lod_atlas.py (the frozen x3m-regenerate bundles both beside the modules, as
    lod_overlay's): either change rebakes the sky textures. Outside lod_overlay.TOOL_FILES, so it never rebakes bodies."""
    h = hashlib.sha256()
    for name in SKY_TOOL_FILES:
        h.update((HERE / name).read_bytes())
    return h.hexdigest()


def classify(path):
    """('stars', family, colour) | ('part', family, part number) | None for a member path (any case, any
    separator, an addon/ or addon2/ prefix ignored)."""
    p = re.sub(r'^addon2?/', '', path.replace('\\', '/').lower())
    m = _STARS.fullmatch(p)
    if m:
        return 'stars', m['family'], m['colour']
    m = _PART.fullmatch(p)
    if m and '_stars' not in m['family']:
        n = _PART_NO.search(m['rest'])
        return 'part', m['family'], int(n[1]) if n else 0
    return None


def options_of(overrides=None):
    opts = dict(DEFAULTS)
    for k, v in (overrides or {}).items():
        if k not in DEFAULTS:
            raise ValueError(f'unknown sky option {k}')
        opts[k] = v
    return opts


def inputs_sha256(source, options, kind):
    """sha256(unpacked source DDS bytes || recipe id || kind || options): the reuse key. The whole DDS (header and
    every mip) rather than the decoded level 0, because a star texture's output is those bytes with a header mark."""
    h = hashlib.sha256(source)
    h.update(RECIPE_ID.encode())
    h.update(kind.encode())
    h.update(json.dumps(options, sort_keys=True).encode())
    return h.hexdigest()


def out_size(w, h, options=None):
    """(w, h) of the bake: 2x while the result stays within MAX_OUT texels per side and in total."""
    if options is not None and not options.get('upsample', True):
        return w, h
    if 2 * w <= MAX_OUT[0] and 2 * h <= MAX_OUT[0] and 4 * w * h <= MAX_OUT[0] * MAX_OUT[1]:
        return 2 * w, 2 * h
    return w, h


def eligibility(kind, data, options):
    """(eligible, reason, info) of a classified texture's unpacked DDS bytes."""
    try:
        w, h, mips, fmt = lod_atlas.dds_format(data)
    except lod_atlas.AtlasError as exc:
        return False, 'not_dds', dict(error=str(exc))
    info = dict(width=w, height=h, mips=mips, format=fmt if isinstance(fmt, str) else f'RGB{fmt[1]}')
    if kind == 'stars':
        return True, None, info
    if fmt in ('DXT3', 'DXT5'):
        return False, 'alpha_format', info
    if not isinstance(fmt, str) and options['uncompressed'] != 'dxt1':
        return False, 'uncompressed', info
    if min(w, h) < MIN_SIDE or w % 4 or h % 4:
        return False, 'small', info
    return True, None, info


# --- image stages ---------------------------------------------------------------------------------

def box_mean(img, r):
    """Mean over the (2r+1)^2 window clipped to the image (N-normalised, so the border is unbiased); float64
    integral image, (h, w) or (h, w, c)."""
    a = np.asarray(img, np.float64)
    h, w = a.shape[:2]

    def axis_sum(x, n, ax):
        c = np.cumsum(x, ax)
        pad = [(0, 0)] * x.ndim
        pad[ax] = (1, 0)
        c = np.pad(c, pad)
        i = np.arange(n)
        lo, hi = np.clip(i - r, 0, n), np.clip(i + r + 1, 0, n)
        return np.take(c, hi, ax) - np.take(c, lo, ax)
    s = axis_sum(axis_sum(a, h, 0), w, 1)
    ones = np.ones((h, w))
    cnt = axis_sum(axis_sum(ones, h, 0), w, 1)
    return s / (cnt[..., None] if a.ndim == 3 else cnt)


def local_range(lum8, r):
    """max - min of a uint8 (h, w) array over the (2r+1)^2 window clipped to the image."""
    win = 2 * r + 1
    out = []
    for f in (np.max, np.min):
        x = np.pad(lum8, ((r, r), (0, 0)), mode='edge')
        x = f(np.lib.stride_tricks.sliding_window_view(x, win, 0), -1)
        x = np.pad(x, ((0, 0), (r, r)), mode='edge')
        out.append(f(np.lib.stride_tricks.sliding_window_view(x, win, 1), -1))
    return out[0].astype(np.int16) - out[1].astype(np.int16)


def luma8(rgb8):
    return (0.299 * rgb8[..., 0] + 0.587 * rgb8[..., 1] + 0.114 * rgb8[..., 2]).astype(np.int32)


LUMA = np.array([0.299, 0.587, 0.114], np.float32)


def deband(rgb, rgb8, options):
    """Hue-locked deband: the self-guided filter (He et al.) on luma, r = deband_radius, eps = (deband_edge/255)^2,
    applied as a gain on RGB (hue and chroma ratios of every texel kept), blended in where the local (2r+1)^2 luma
    range is below deband_range/255 (full weight up to deband_range - deband_ramp) and ramped to zero over the r
    texels next to each border, so the edge columns and rows the parts meet at stay as the source has them."""
    r = int(options['deband_radius'])
    if r <= 0:
        return rgb
    eps = (options['deband_edge'] / 255.0) ** 2
    rng = local_range(np.clip(luma8(rgb8), 0, 255).astype(np.uint8), r)
    hi, ramp = options['deband_range'], max(options['deband_ramp'], 1e-6)
    wgt = np.clip((hi - rng) / ramp, 0.0, 1.0).astype(np.float32)
    h, w = wgt.shape
    border = np.minimum(np.minimum(np.arange(h), np.arange(h)[::-1])[:, None],
                        np.minimum(np.arange(w), np.arange(w)[::-1])[None, :])
    wgt *= np.clip(border / r, 0.0, 1.0)
    if not wgt.any():
        return rgb
    lum = rgb @ LUMA
    mean = box_mean(lum, r)
    var = box_mean(lum * lum, r) - mean * mean
    a = var / (var + eps)
    b = mean - a * mean
    q = box_mean(a, r) * lum + box_mean(b, r)
    out = lum + wgt * (q - lum)
    gain = np.where(lum > 1e-6, out / np.maximum(lum, 1e-6), 1.0).astype(np.float32)
    return rgb * gain[..., None]


def _catmull_weights(t):
    t2, t3 = t * t, t * t * t
    return np.array([(-t3 + 2 * t2 - t) / 2, (3 * t3 - 5 * t2 + 2) / 2, (-3 * t3 + 4 * t2 + t) / 2, (t3 - t2) / 2])


def upsample2(img):
    """2x Catmull-Rom (separable, texel centres aligned, edges clamped): out[2i] samples x = i - 1/4, out[2i+1]
    x = i + 1/4."""
    w_even, w_odd = _catmull_weights(0.75), _catmull_weights(0.25)   # taps i-2..i+1 and i-1..i+2

    def along(x, ax):
        n = x.shape[ax]
        idx = lambda k: np.take(x, np.clip(np.arange(n) + k, 0, n - 1), ax)
        m2, m1, z, p1, p2 = idx(-2), idx(-1), x, idx(1), idx(2)
        even = w_even[0] * m2 + w_even[1] * m1 + w_even[2] * z + w_even[3] * p1
        odd = w_odd[0] * m1 + w_odd[1] * z + w_odd[2] * p1 + w_odd[3] * p2
        out = np.stack([even, odd], ax + 1)
        shape = list(x.shape)
        shape[ax] = 2 * n
        return out.reshape(shape)
    img = img.astype(np.float32)
    if img.ndim == 2:
        return along(along(img, 0), 1)
    out = np.empty((2 * img.shape[0], 2 * img.shape[1], img.shape[2]), np.float32)
    for c in range(img.shape[2]):                     # one channel at a time: the worker peak stays low
        out[..., c] = along(along(img[..., c], 0), 1)
    return out


def value_noise(h, w, period, octaves, seed):
    """Smooth value noise in [-1, 1]: bilinear-smoothstep lattice values, octaves halving the period."""
    rng = np.random.default_rng(seed)
    total, norm = np.zeros((h, w), np.float32), 0.0
    for o in range(octaves):
        p = max(2, period >> o)
        gy, gx = h // p + 2, w // p + 2
        lat = rng.uniform(-1, 1, (gy, gx)).astype(np.float32)
        y, x = np.arange(h) / p, np.arange(w) / p
        y0, x0 = y.astype(int), x.astype(int)
        fy, fx = y - y0, x - x0
        fy, fx = (fy * fy * (3 - 2 * fy))[:, None], (fx * fx * (3 - 2 * fx))[None, :]
        a, b = lat[y0][:, x0], lat[y0][:, x0 + 1]
        c, d = lat[y0 + 1][:, x0], lat[y0 + 1][:, x0 + 1]
        amp = 0.5 ** o
        total += amp * ((a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy)
        norm += amp
    return total / norm


def add_detail(rgb, options, seed):
    """out = base * (1 + k n), n value noise, k = amplitude x (local luminance x gradient magnitude, normalised to
    its 99th percentile): hue and chroma ratios locked."""
    lum = rgb @ np.array([0.299, 0.587, 0.114], np.float32)
    gy, gx = np.gradient(box_mean(lum, 2))
    act = lum * np.hypot(gx, gy)
    ref = np.percentile(act, 99) if act.size else 0.0
    if ref <= 0:
        return rgb
    k = options['detail_amplitude'] * np.clip(act / ref, 0, 1)
    n = value_noise(rgb.shape[0], rgb.shape[1], int(options['detail_period']), int(options['detail_octaves']), seed)
    return rgb * (1 + (k * n)[..., None]).astype(np.float32)


BAYER8 = np.array([[0, 32, 8, 40, 2, 34, 10, 42], [48, 16, 56, 24, 50, 18, 58, 26],
                   [12, 44, 4, 36, 14, 46, 6, 38], [60, 28, 52, 20, 62, 30, 54, 22],
                   [3, 35, 11, 43, 1, 33, 9, 41], [51, 19, 59, 27, 49, 17, 57, 25],
                   [15, 47, 7, 39, 13, 45, 5, 37], [63, 31, 55, 23, 61, 29, 53, 21]], np.float32)


def bayer_offsets(h, w, amplitude):
    """Ordered 8x8 Bayer offsets in (-amplitude/2, +amplitude/2), one per texel."""
    t = ((BAYER8 + 0.5) / 64.0 - 0.5) * amplitude
    return np.tile(t, (h // 8 + 1, w // 8 + 1))[:h, :w]


# --- DXT1 -----------------------------------------------------------------------------------------

def _blocks(img):
    h, w = img.shape[:2]
    return img.reshape(h // 4, 4, w // 4, 4, -1).transpose(0, 2, 1, 3, 4).reshape(-1, 16, img.shape[-1])


def _pack(c0, c1, idx):
    bits = (idx.astype(np.uint32) << (2 * np.arange(16, dtype=np.uint32))).sum(1, dtype=np.uint64).astype(np.uint32)
    out = np.zeros((len(c0), 8), np.uint8)
    out[:, 0:2] = c0.astype('<u2').view(np.uint8).reshape(-1, 2)
    out[:, 2:4] = c1.astype('<u2').view(np.uint8).reshape(-1, 2)
    out[:, 4:8] = bits.astype('<u4').view(np.uint8).reshape(-1, 4)
    return out


_EXP5 = np.array([(c << 3) | (c >> 2) for c in range(32)], np.float32)   # lod_atlas._expand565
_EXP6 = np.array([(c << 2) | (c >> 4) for c in range(64)], np.float32)
_LO5 = (np.searchsorted(_EXP5, np.arange(256), side='right') - 1).astype(np.int32)   # largest code <= v
_LO6 = (np.searchsorted(_EXP6, np.arange(256), side='right') - 1).astype(np.int32)
_CORNER = np.array([[(k >> c) & 1 for c in range(3)] for k in range(8)], np.int32)
_LEVEL_INDEX = np.array([1, 3, 2, 0])          # palette level 0..3 along c1 -> c0 to the DXT1 index


def _cell(p):
    """(n, 3) float colours -> (n, 8) 565 codes and (n, 8, 3) expanded colours of the enclosing 565 cell."""
    v = np.clip(p, 0, 255).astype(np.int32)
    lo = np.stack([_LO5[v[:, 0]], _LO6[v[:, 1]], _LO5[v[:, 2]]], -1)
    codes = np.where(_CORNER[None] == 1, np.minimum(lo + 1, [31, 63, 31])[:, None, :], lo[:, None, :])
    exp = np.stack([_EXP5[codes[..., 0]], _EXP6[codes[..., 1]], _EXP5[codes[..., 2]]], -1)
    return (codes[..., 0] << 11 | codes[..., 1] << 5 | codes[..., 2]).astype(np.uint32), exp


def _bracket_endpoints(rgb, length):
    """Endpoints for low-spread blocks: the segment M +- length/2 along the block's axis (its principal axis, or the
    ray from black through the mean M when the block is flat, so the dither moves intensity, not hue) is replaced by
    the pair of corners of the two enclosing 565 cells (8 x 8 candidates) whose line passes closest to M with M
    inside the segment. Unlike rounding each end to the nearest code, a flat block keeps two distinct endpoints, so
    its palette brackets the colour and the ordered dither can mix the levels."""
    n = len(rgb)
    m = rgb.mean(1)
    d = rgb - m[:, None]
    cov = np.einsum('nki,nkj->nij', d, d)
    w, vec = np.linalg.eigh(cov)
    axis = vec[:, :, 2]
    norm = np.linalg.norm(m, axis=1, keepdims=True)
    ray = np.where(norm > 1e-3, m / np.maximum(norm, 1e-3), np.float32(1 / np.sqrt(3)))
    axis = np.where((np.sqrt(np.maximum(w[:, 2], 0) / 16) < 1.0)[:, None], ray, axis)
    axis = np.where((axis.sum(1) < 0)[:, None], -axis, axis)
    t = np.einsum('nkc,nc->nk', d, axis)
    mid, half = (t.max(1) + t.min(1)) / 2, np.maximum((t.max(1) - t.min(1)) / 2, length / 2)
    ca, ea = _cell(m + (mid - half)[:, None] * axis)
    cb, eb = _cell(m + (mid + half)[:, None] * axis)
    a, b = np.repeat(ea, 8, 1), np.tile(eb, (1, 8, 1))                   # (n, 64, 3)
    seg, rel = b - a, m[:, None, :] - a
    ss = np.maximum((seg * seg).sum(-1), 1e-6)
    rs = (rel * seg).sum(-1)
    tt = rs / ss
    cost = (rel * rel).sum(-1) - rs * rs / ss + (np.clip(tt, 0, 1) - tt) ** 2 * ss
    best = cost.argmin(1)
    i = np.arange(n)
    c1, c0 = np.repeat(ca, 8, 1)[i, best], np.tile(cb, (1, 8))[i, best]
    return np.maximum(c0, c1), np.minimum(c0, c1)


def _dithered_indices(rgb, c0, c1, offsets):
    """Index per texel: its position along the palette line c1 -> c0 in thirds plus the ordered-dither offset,
    rounded to the nearest of the four levels; equal endpoints (3-colour mode) use index 0 only."""
    pal = lod_atlas._palette(c0, c1, np.ones(len(c0), bool)).astype(np.float32)
    p0, p1 = pal[:, 0], pal[:, 1]
    seg = p0 - p1
    ss = np.maximum((seg * seg).sum(-1), 1e-6)
    t = np.einsum('nkc,nc->nk', rgb - p1[:, None], seg) / ss[:, None]
    idx = _LEVEL_INDEX[np.clip(np.rint(t * 3 + offsets), 0, 3).astype(np.int64)]
    idx[c0 == c1] = 0
    return idx


def _encode_plain(rgb, offsets, options):
    """Blocks without zero texels: PCA endpoints (lod_atlas._encode_color) where the block's luma spans more than
    pca_spread levels, bracketing endpoints elsewhere; dithered indices on both."""
    c0 = np.zeros(len(rgb), np.uint32)
    c1 = np.zeros(len(rgb), np.uint32)
    lum = rgb @ LUMA
    wide = (lum.max(1) - lum.min(1)) > options['pca_spread']
    if wide.any():
        enc = lod_atlas._encode_color(rgb[wide])
        c0[wide] = enc[:, 0].astype(np.uint32) | enc[:, 1].astype(np.uint32) << 8
        c1[wide] = enc[:, 2].astype(np.uint32) | enc[:, 3].astype(np.uint32) << 8
    if (~wide).any():
        c0[~wide], c1[~wide] = _bracket_endpoints(rgb[~wide], options['bracket_length'])
    return _pack(c0, c1, _dithered_indices(rgb, c0, c1, offsets))


def _encode_black_endpoint(rgb, zero, offsets):
    """Blocks holding exact-zero texels: 4-colour mode with c1 = 0 (black) and c0 the least-squares colour on the
    ray from black through the block's non-zero texels (whose levels are dithered along that ray, never down to
    black); zero texels take index 1. Every zero stays exactly 0, alpha stays 1."""
    nz = ~zero
    top = np.where(nz[:, :, None], rgb, 0).max(1)
    c0 = top
    for _ in range(2):                                             # assign, then refit the scale of c0
        q = lod_atlas._to565(c0)
        p0 = lod_atlas._expand565(q).astype(np.float32)
        t = np.einsum('nkc,nc->nk', rgb, p0) / np.maximum((p0 * p0).sum(-1), 1e-6)[:, None]
        f = np.clip(np.rint(t * 3), 1, 3) / 3 * nz
        den = (f * f).sum(1)
        fit = np.einsum('nk,nkc->nc', f, rgb) / np.maximum(den, 1e-6)[:, None]
        c0 = np.where((den > 0)[:, None], fit, top)
    q = lod_atlas._to565(c0)
    p0 = lod_atlas._expand565(q).astype(np.float32)
    t = np.einsum('nkc,nc->nk', rgb, p0) / np.maximum((p0 * p0).sum(-1), 1e-6)[:, None]
    idx = _LEVEL_INDEX[np.clip(np.rint(t * 3 + offsets), 1, 3).astype(np.int64)]
    idx[zero] = 1
    idx[q == 0] = 0                                                # c0 quantised to black: c0 == c1, index 0 only
    return _pack(q, np.zeros_like(q), idx)


def encode_dxt1(rgb255, zero, options=None, chunk=8192):
    """(h, w, 3) float 0..255 and an (h, w) exact-zero mask -> DXT1 level bytes (answer 3, stage 6): ordered 8x8
    Bayer dither of the palette level (amplitude `dither` palette steps, 1.0 = +-0.5 step), _encode_plain on blocks
    without zeros, a black-endpoint block where a zero sits, all-zero blocks c0 = c1 = 0 (opaque black). Alpha is 1
    on every texel."""
    options = options_of(options)
    h, w = rgb255.shape[:2]
    blocks = _blocks(np.asarray(rgb255, np.float32))                   # a copy (the transpose), clipped in place
    np.clip(blocks, 0, 255, out=blocks)
    zb = _blocks(zero[..., None])[:, :, 0]
    offsets = _blocks(bayer_offsets(h, w, options['dither'])[..., None])[:, :, 0]
    out = np.zeros((len(blocks), 8), np.uint8)
    anyz, allz = zb.any(1), zb.all(1)
    for sel_all, fn in ((np.flatnonzero(~anyz), lambda s: _encode_plain(blocks[s], offsets[s], options)),
                        (np.flatnonzero(anyz & ~allz), lambda s: _encode_black_endpoint(blocks[s], zb[s], offsets[s]))):
        for i in range(0, len(sel_all), chunk):
            sel = sel_all[i:i + chunk]
            out[sel] = fn(sel)
    return out.tobytes()                                           # all-zero blocks: already 8 zero bytes


# --- DDS ------------------------------------------------------------------------------------------

def mark_bytes(slot, lane, k, payload):
    """dwReserved1[0..2]: 'X3ML', version 1, slot | lane << 8 | K << 16 | payload << 24 (lod_texture_mark.h)."""
    for v in (slot, lane, k, payload):
        if not 0 <= v <= 255:
            raise ValueError(f'mark field {v} outside 0..255')
    return lod_atlas.DDS_MARK + struct.pack('<II', lod_atlas.DDS_MARK_VERSION, slot | lane << 8 | k << 16 | payload << 24)


def with_mark(data, mark):
    d = bytearray(data)
    d[lod_atlas.DDS_MARK_OFFSET:lod_atlas.DDS_MARK_OFFSET + len(mark)] = mark
    return bytes(d)


def dxt1_header(source, w, h, mark):
    """The source header with the DXT1 level-0 fields of a (w, h) single-level texture: flags, caps and every other
    field as the stock file has them (no mip chain: DDSD_MIPMAPCOUNT, DDSCAPS_MIPMAP and COMPLEX cleared when the
    source had more than one level), pixel format DXT1, the mark in the reserved words."""
    d = bytearray(source[:128])
    flags, = struct.unpack_from('<I', d, 8)
    _, _, mips, fmt = lod_atlas.dds_format(source)
    flags = (flags | 0x1007 | 0x80000) & ~0x8              # CAPS HEIGHT WIDTH PIXELFORMAT LINEARSIZE, no PITCH
    if mips > 1:
        flags &= ~0x20000
        caps, = struct.unpack_from('<I', d, 108)
        struct.pack_into('<I', d, 108, caps & ~0x400008)
        struct.pack_into('<I', d, 28, 0)
    struct.pack_into('<IIII', d, 8, flags, h, w, (w // 4) * (h // 4) * 8)
    if not isinstance(fmt, str):                            # uncompressed opt-in: a DXT1 pixel format
        struct.pack_into('<II4sIIIII', d, 76, 32, 4, b'DXT1', 0, 0, 0, 0, 0)
    d[lod_atlas.DDS_MARK_OFFSET:lod_atlas.DDS_MARK_OFFSET + len(mark)] = mark
    return bytes(d)


def store(dds, member):
    """Member bytes as the catalogue holds them: gzip (mtime 0) for a .pck, raw for a .dds."""
    return gzip.compress(dds, mtime=0) if member.lower().endswith('.pck') else dds


# --- bake -----------------------------------------------------------------------------------------

def part_mark(family, number):
    return mark_bytes(fnv1a32(family.encode('ascii', 'replace')) & 255, min(number, 255), 0, PAYLOAD_SKY_NEBULA)


def star_descriptor(family, colour, rgba):
    """(slot, lane, K) of answer 1 from the level-0 texels."""
    lum = 0.299 * rgba[..., 0] + 0.587 * rgba[..., 1] + 0.114 * rgba[..., 2]
    pad = np.pad(lum, 1, mode='edge')
    h, w = lum.shape
    mx = np.max(np.stack([pad[i:i + h, j:j + w] for i in range(3) for j in range(3)]), 0)
    peaks = int(((lum == mx) & (lum > BRIGHT_PEAK_LUMA)).sum())
    lane = STAR_LANES.index(colour) if colour in STAR_LANES else LANE_OTHER
    return fnv1a32(family.encode('ascii', 'replace')) & 255, lane, min(peaks // 8, 255), peaks


def bake_stars(source, family, colour):
    """(dds, info): the source with the payload-2 mark; texels and mips byte-identical."""
    rgba = lod_atlas.decode_dds(source).astype(np.float32)
    slot, lane, k, peaks = star_descriptor(family, colour, rgba)
    return with_mark(source, mark_bytes(slot, lane, k, PAYLOAD_SKY_STARS)), dict(
        slot=slot, lane=lane, k=k, peaks=peaks, payload=PAYLOAD_SKY_STARS)


def bake_part(source, family, number, options, seed_name='', timings=None):
    """(dds, info) of one nebula part (answer 3, stages 1-8); info carries the output size and encoder PSNR."""
    t = timings if timings is not None else {}
    clock = time.perf_counter()

    def lap(name):
        nonlocal clock
        now = time.perf_counter()
        t[name] = round(t.get(name, 0.0) + now - clock, 4)
        clock = now
    w, h, _, _ = lod_atlas.dds_format(source)
    rgb8 = lod_atlas.decode_dds(source)[..., :3]
    zero = ~rgb8.any(-1)
    rgb = rgb8.astype(np.float32) / 255.0
    lap('decode')
    rgb = deband(rgb, rgb8, options)
    lap('deband')
    ow, oh = out_size(w, h, options)
    if (ow, oh) != (w, h):
        rgb = upsample2(rgb)
        zero = np.repeat(np.repeat(zero, 2, 0), 2, 1)
    lap('resample')
    if options['detail']:
        seed = int.from_bytes(hashlib.sha256(seed_name.encode()).digest()[:8], 'little')
        rgb = add_detail(rgb, options, seed)
        lap('detail')
    rgb255 = np.clip(rgb, 0, 1, out=rgb)
    rgb255 *= 255.0
    del rgb
    rgb255[zero] = 0.0
    level = encode_dxt1(rgb255, zero, options)
    lap('encode')
    dds = dxt1_header(source, ow, oh, part_mark(family, number)) + level
    back = lod_atlas.decode_dds(dds)[..., :3].astype(np.float32)
    violations = int(back[zero].any(-1).sum())
    back -= rgb255
    np.square(back, out=back)
    nz = ~zero
    mse = float(back.sum(-1)[nz].sum() / (3 * nz.sum())) if nz.any() else 0.0
    lap('check')
    return dds, dict(width=ow, height=oh, encoder_psnr=psnr_of(mse), zero_violations=violations)


def psnr_of(mse):
    return 99.0 if mse <= 1e-12 else round(float(10 * np.log10(255.0 ** 2 / mse)), 2)


def bake_one(row, source, options):
    """Bake one classified row from its unpacked source bytes: dict(member, dds, stored, info, seconds, timings)."""
    t0 = time.perf_counter()
    timings = {}
    if row['kind'] == 'stars':
        dds, info = bake_stars(source, row['family'], row['colour'])
    else:
        dds, info = bake_part(source, row['family'], row['number'], options, row['member'], timings)
    t1 = time.perf_counter()
    stored = store(dds, row['member'])
    timings['store'] = round(time.perf_counter() - t1, 4)
    return dict(member=row['member'], dds=dds, stored=stored, info=info, timings=timings,
                seconds=round(time.perf_counter() - t0, 3))


def _bake_worker(args):
    row, source, options = args
    res = bake_one(row, source, options)
    if not row.get('keep_dds'):
        res.pop('dds')
    return res


# --- scan -----------------------------------------------------------------------------------------

def scan(assets, options, families=None):
    """(rows, skipped): every classified winning member of assets (sector_fog_census.Assets view, the caller's
    choice of layers), rows = eligible dicts(member, source, kind, family, colour|number, inputs_sha256, data),
    skipped = [(member, reason)]. families (a set of family names) restricts both kinds."""
    rows, skipped, seen = [], [], set()
    for key in sorted(assets.entries):
        base = re.sub(r'^addon2?/', '', key)
        if base in seen or not base.startswith('dds/nebula_'):
            continue
        seen.add(base)
        cands = assets.candidates(base)                    # the expansion namespace (a selected package) wins
        if not cands:
            continue
        e = cands[-1]                                      # the engine's winner: the highest layer
        c = classify(e['path'])
        if c is None:
            continue
        kind, family, extra = c
        if families is not None and family not in families:
            continue
        if 'loose' in e:
            skipped.append((e['path'], 'loose_file'))
            continue
        data = assets.read_entry(e)
        ok, reason, info = eligibility(kind, data, options)
        if not ok:
            skipped.append((e['path'], reason))
            continue
        row = dict(member=e['path'].replace('\\', '/'), source=e['source'], kind=kind, family=family, **info,
                   inputs_sha256=inputs_sha256(data, options if kind == 'part' else {}, kind), data=data)
        row['colour' if kind == 'stars' else 'number'] = extra
        rows.append(row)
    return rows, skipped


def bake_rows(rows, options, jobs=1, keep_dds=False):
    """Results in the order of rows. jobs > 1: a spawned process pool (peak 614 MiB RSS per worker at
    4096x2048, measured 2026-11-04); progress(row, done, total) as each finishes."""
    total, done, results = len(rows), 0, [None] * len(rows)
    args = [(dict({k: v for k, v in r.items() if k != 'data'}, keep_dds=keep_dds), r['data'], options) for r in rows]
    if jobs <= 1 or len(rows) <= 1:
        for i, a in enumerate(args):
            results[i] = _bake_worker(a)
            done += 1
            if progress is not None:
                progress(rows[i], done, total)
        return results
    ctx = multiprocessing.get_context('spawn')
    with concurrent.futures.ProcessPoolExecutor(max_workers=min(jobs, len(rows)), mp_context=ctx) as pool:
        futs = {pool.submit(_bake_worker, a): i for i, a in enumerate(args)}
        for f in concurrent.futures.as_completed(futs):
            i = futs[f]
            results[i] = f.result()
            done += 1
            if progress is not None:
                progress(rows[i], done, total)
    return results


def manifest_row(row, res, reused_from=None):
    """The overlay manifest's sky row (Decision B): member, source, inputs_sha256, member_sha256, metrics."""
    info = res['info']
    metrics = dict(kind=row['kind'], family=row['family'], width=info.get('width', row['width']),
                   height=info.get('height', row['height']), source_size=[row['width'], row['height']],
                   source_format=row['format'], bytes=len(res['stored']))
    for k in ('encoder_psnr', 'zero_violations', 'slot', 'lane', 'k', 'peaks', 'payload'):
        if k in info:
            metrics[k] = info[k]
    if 'seconds' in res:
        metrics['seconds'] = res['seconds']
    out = dict(member=row['member'], source=row['source'], inputs_sha256=row['inputs_sha256'],
               member_sha256=hashlib.sha256(res['stored']).hexdigest(), metrics=metrics)
    if reused_from is not None:
        out['reused_from'] = reused_from
    return out


# --- metrics (answer 4; shared by the verification scripts and the host test) ------------------------------

def box_down(img, f):
    if f == 1:
        return img.astype(np.float64)
    h, w = img.shape[:2]
    return img.astype(np.float64).reshape(h // f, f, w // f, f, -1).mean((1, 3))


def mean_run_x(rgb8):
    """Mean run of identical luma along x over non-black texels (sky_texture_stats.py runs_x)."""
    lum = luma8(rgb8.astype(np.int32))
    same = lum[:, 1:] == lum[:, :-1]
    nonblack = lum[:, 1:] > 0
    return float(nonblack.sum() / max(1, ((~same) & nonblack).sum()))


def plateau_share(rgb8):
    """Share of non-black 8x8 blocks holding at most two distinct colours."""
    h, w, _ = rgb8.shape
    b = rgb8[:h // 8 * 8, :w // 8 * 8].astype(np.int32)
    b = b.reshape(h // 8, 8, w // 8, 8, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 64, 3)
    key = b[..., 0] << 16 | b[..., 1] << 8 | b[..., 2]
    nonblack = key.max(1) > 0
    k = np.sort(key[nonblack], 1)
    distinct = 1 + (np.diff(k, axis=1) != 0).sum(1)
    return float((distinct <= 2).mean()) if len(k) else 0.0


def hue_deg(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    return np.degrees(np.arctan2(np.sqrt(3) * (g - b), 2 * r - g - b))


def wrap_diff(rgb):
    lum = rgb[..., :3].astype(np.float64) @ np.array([0.299, 0.587, 0.114])
    return float(np.abs(lum[:, 0] - lum[:, -1]).mean())


def metrics(src_dds, bake_dds):
    """Answer-4 metrics of a bake against its source, on the bake box-downsampled to the source size: plateau-block
    share and mean identical-luma run on that average rounded to 8 bits (both are 8-bit measures); PSNR over the
    source's non-zero texels and over all texels, mean hue shift on texels with source chroma > 8 and the
    wrap-column difference (mean |L(col 0) - L(col w-1)|) on the unrounded average (what a 2x minification
    samples); zero violations at bake resolution (a bake texel inside a source zero texel's footprint that is not 0)."""
    src = lod_atlas.decode_dds(src_dds)[..., :3]
    bake = lod_atlas.decode_dds(bake_dds)[..., :3]
    f = bake.shape[0] // src.shape[0]
    if bake.shape[0] != f * src.shape[0] or bake.shape[1] != f * src.shape[1]:
        raise ValueError(f'bake {bake.shape[:2]} is not an integer multiple of the source {src.shape[:2]}')
    zero = ~src.any(-1)
    zero_up = np.repeat(np.repeat(zero, f, 0), f, 1)
    d = box_down(bake, f)                                   # float box average: PSNR, hue, wrap
    down = np.clip(np.rint(d), 0, 255).astype(np.uint8)     # its 8-bit rounding: plateau and run
    s = src.astype(np.float64)
    nz = ~zero
    mse_nz = float(((s[nz] - d[nz]) ** 2).mean()) if nz.any() else 0.0
    mse_all = float(((s - d) ** 2).mean())
    chroma = src.max(-1).astype(np.int32) - src.min(-1).astype(np.int32)
    sel = chroma > 8
    if sel.any():
        dh = np.abs(hue_deg(s[sel]) - hue_deg(d[sel]))
        hue = float(np.minimum(dh, 360 - dh).mean())
    else:
        hue = 0.0
    return dict(source_size=[int(src.shape[1]), int(src.shape[0])], bake_size=[int(bake.shape[1]), int(bake.shape[0])],
                plateau_share_source=round(plateau_share(src), 4), plateau_share=round(plateau_share(down), 4),
                run_source=round(mean_run_x(src), 3), run=round(mean_run_x(down), 3),
                psnr=psnr_of(mse_nz), psnr_all=psnr_of(mse_all), hue_shift_deg=round(hue, 3),
                chroma_texels=int(sel.sum()), zero_texels=int(zero.sum()),
                zero_violations=int(bake[zero_up].any(-1).sum()),
                wrap_diff_source=round(wrap_diff(src), 3), wrap_diff=round(wrap_diff(d), 3))


# Answer-4 limits as redefined 2026-11-04 (coordinator; sector-background-modern.md, Implementation): the identical-luma
# run is informational only (it measures 8-bit repetition: an exact smooth ramp has runs of 8 px); the plateau share
# passes at <= 10 % or <= 90 % of the source's share; hue <= 2.0 deg (the measured DXT1 bound on dark low-chroma
# parts); PSNR, zero violations and the wrap-column change as before. A near-uniform texture (source plateau share
# >= near_uniform) measures flatness, not banding: it passes the plateau check only when the bake's plateau share does
# not exceed the source's and its identical-luma run is at most a third of the source's (so an untouched or
# nearest-neighbour bake still fails).
THRESHOLDS = dict(plateau_share=0.10, plateau_ratio=0.90, near_uniform=0.5, near_uniform_run=1 / 3, psnr=40.0, hue_shift_deg=2.0, zero_violations=0,
                  wrap_tolerance=0.5)


def failures(m, th=THRESHOLDS):
    """Threshold breaches of one metrics dict (empty: pass). The run is not a limit."""
    out = []
    plateau_cap = max(th['plateau_share'], th['plateau_ratio'] * m['plateau_share_source'])
    if m['plateau_share_source'] >= th['near_uniform']:
        if m['plateau_share'] > m['plateau_share_source'] or m['run'] > th['near_uniform_run'] * m['run_source']:
            out.append(f"near-uniform: plateau {m['plateau_share']:.4f} (source {m['plateau_share_source']:.4f}),"
                       f" run {m['run']:.2f} > source {m['run_source']:.2f} / 3")
    elif m['plateau_share'] > plateau_cap:
        out.append(f"plateau {m['plateau_share']:.4f} > max({th['plateau_share']}, {th['plateau_ratio']} x source"
                   f" {m['plateau_share_source']:.4f})")
    if m['psnr'] < th['psnr']:
        out.append(f"psnr {m['psnr']} < {th['psnr']}")
    if m['hue_shift_deg'] > th['hue_shift_deg']:
        out.append(f"hue {m['hue_shift_deg']} > {th['hue_shift_deg']}")
    if m['zero_violations'] > th['zero_violations']:
        out.append(f"zero violations {m['zero_violations']}")
    if abs(m['wrap_diff'] - m['wrap_diff_source']) > th['wrap_tolerance']:
        out.append(f"wrap {m['wrap_diff']} vs source {m['wrap_diff_source']}")
    return out


# --- CLI ------------------------------------------------------------------------------------------

def source_view(game, edition=None):
    """The game's catalogue view without our x3m-lod overlay slots (lod_overlay.original_assets), so a bake
    reads the stock or mod member, never its own previous output."""
    import lod_overlay
    assets, _ = lod_overlay.original_assets(Path(game), edition=edition)
    return assets


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--game', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True, help='scratch directory (refused inside --game)')
    ap.add_argument('--families', help='comma-separated family names (default: every family)')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 1))
    ap.add_argument('--edition', choices=('auto', 'ap', 'fl'), default='auto')
    ap.add_argument('--sky-detail', action='store_true', help='opt-in hue-locked detail synthesis (stage 5)')
    ap.add_argument('--sky-uncompressed', choices=('skip', 'dxt1'), default='skip')
    ap.add_argument('--dither', type=float, default=DEFAULTS['dither'],
                    help='ordered-dither amplitude in palette steps (default 1.0 = +-0.5 step)')
    a = ap.parse_args(argv)
    game, out = a.game.resolve(), a.out.resolve()
    if out == game or game in out.parents:
        raise SystemExit(f'--out {out} is inside the game directory; refusing')
    options = options_of(dict(detail=a.sky_detail, uncompressed=a.sky_uncompressed, dither=a.dither))
    fams = {f.strip().lower() for f in a.families.split(',') if f.strip()} if a.families else None
    t0 = time.perf_counter()
    assets = source_view(game, None if a.edition == 'auto' else a.edition)
    rows, skipped = scan(assets, options, fams)
    scan_s = time.perf_counter() - t0
    print(f'sky scan: {len(rows)} eligible ({sum(r["kind"] == "part" for r in rows)} parts,'
          f' {sum(r["kind"] == "stars" for r in rows)} star textures), {len(skipped)} skipped; {scan_s:.1f} s', flush=True)
    t1 = time.perf_counter()
    results = bake_rows(rows, options, a.jobs, keep_dds=True)
    bake_s = time.perf_counter() - t1
    (out / 'dds').mkdir(parents=True, exist_ok=True)
    record = []
    for r, res in zip(rows, results):
        name = Path(r['member']).with_suffix('.dds').name
        (out / 'dds' / name).write_bytes(res['dds'])
        mr = manifest_row(r, res)
        mr.update(file=f'dds/{name}', timings=res['timings'], source_bytes=len(r['data']),
                  source_stored_bytes=next(e['size'] for e in assets.candidates(r['member']) if e['source'] == r['source']))
        record.append(mr)
        print(f"{r['member']}: {r['kind']} {mr['metrics']['width']}x{mr['metrics']['height']}"
              f" {res['seconds']:.2f} s" + (f" encoder PSNR {res['info']['encoder_psnr']} dB" if 'encoder_psnr' in res['info'] else
                                           f" mark slot {res['info']['slot']} lane {res['info']['lane']} K {res['info']['k']}"),
              flush=True)
    (out / 'sky-bake.json').write_text(json.dumps(dict(
        tool=TOOL, recipe_id=RECIPE_ID, tool_sha256=tool_sha256(), game=str(game), options=options,
        families=sorted(fams) if fams else None, jobs=a.jobs, scan_s=round(scan_s, 2), bake_s=round(bake_s, 2),
        rows=record, skipped=[dict(member=m, reason=why) for m, why in skipped]), indent=1) + '\n')
    print(f'sky bake: {len(rows)} textures in {bake_s:.1f} s wall with {a.jobs} jobs; record {out / "sky-bake.json"}')
    return 0


if __name__ == '__main__':
    multiprocessing.freeze_support()
    sys.exit(main())
