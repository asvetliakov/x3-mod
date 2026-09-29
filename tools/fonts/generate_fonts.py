#!/usr/bin/env python3
"""Generate X3AP bitmap fonts (`F\\<Name><S*d>.abc/.tga`) at integer density d.

Format and constraints: docs/reverse-engineering/font-rendering.md sections 1 and 4; design and
metric mapping: docs/architecture/font-assets.md. Sources are the OFL fonts bundled under
assets/fonts/; the stock code sets and layout targets come from tools/fonts/stock_fonts.json
(derived facts, written by extract_stock_codes.py), so no game install is needed.

  python3 tools/fonts/generate_fonts.py [--out build/fonts] [--density 2 3] [--font Tahoma ...]
      [--preview DIR] [--report FILE]

Needs Pillow with FreeType (variable-font support).
"""
import argparse
import hashlib
import json
import math
import struct
import unicodedata
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / 'assets' / 'fonts'
STOCK = Path(__file__).with_name('stock_fonts.json')

NOTO = 'NotoSans[wdth,wght].ttf'
EXO2 = 'Exo2[wght].ttf'
SS = 4  # supersampling per axis; a 4x4 box gives 17 coverage levels (stock: 15-16)

# Output name -> source instance. 'Width': 'fit' solves Noto's wdth axis so the mean a-z advance
# matches the stock font at the stock cap height; any remainder is taken by a horizontal scale.
# Weights match the stock stem/cap ratio after the horizontal fit (Tahoma 1/8, Zekton 2/16,
# Harrier 2/14): Exo 2 Medium's 0.145 x Zekton's x-scale ~0.87 gives ~0.126.
# 'floor' is the family's default stroke-width floor (see _stroke_floor / _grey_floor). Tahoma:
# Noto SemiBold (600) with the grey floor, chosen from the minified previews (font-assets.md,
# "Readability under minification": even stem weight after the 1.6x / 1.33x bilinear
# minification, antialiased edges kept); the LARGE family keeps Medium with the binary floor.
SOURCES = {
    'Tahoma': {'file': NOTO, 'axes': {'Weight': 600, 'Width': 'fit'}, 'floor': 'grey'},
    'Zekton': {'file': EXO2, 'axes': {'Weight': 500}, 'floor': 'binary'},
    'ZektonES': {'file': EXO2, 'axes': {'Weight': 500}, 'floor': 'binary'},
    'Harrier': {'file': EXO2, 'axes': {'Weight': 500}, 'floor': 'binary'},
}
FALLBACK = {'file': NOTO, 'axes': {'Weight': 400, 'Width': 'fit'}}

# Experiment switches (tools/fonts/preview_minified.py); None / default = the family default:
#   weight      override the Weight axis of the generated family (advance re-fitted: Noto via
#               wdth, Exo 2 via the horizontal scale)
#   hinted      FreeType hinted rendering at the target pixel size, no supersampling (the
#               hinter snaps stems; the horizontal scale is not applied to the image)
#   floor_mode  None = family default; 'binary' (floor on the binarised 4x mask), 'grey'
#               (grey dilation by the width the thinnest stem/bar lacks; edges stay
#               antialiased), 'none'
#   gamma       store alpha = coverage ** (1 / gamma)
OPTS = {'weight': None, 'hinted': False, 'floor_mode': None, 'gamma': 1.0}

# Stock Tahoma13's baked black shadow at d = 1, solved from its H, g and o cells (alpha right of
# a 1-px stem 0.65 / 0.45, below the stem end 0.16 / 0.27 / 0.16, then 0.04 / 0.08): coverage
# convolved with these taps {(dx, dy): weight}, clamped to 1, composited under the glyph.
SHADOW_D1 = {(1, 0): 0.38, (2, 0): 0.29, (0, 1): 0.12, (1, 1): 0.19, (2, 1): 0.16,
             (0, 2): 0.04, (1, 2): 0.08}


# --- sfnt helpers (cmap coverage and version for provenance) --------------------------------

def _tables(b):
    n = struct.unpack_from('>H', b, 4)[0]
    return {b[12 + 16 * i:16 + 16 * i].decode('latin1'): struct.unpack_from('>II', b, 20 + 16 * i)
            for i in range(n)}


def cmap_codes(path):
    """Code points with a non-zero glyph in the Unicode cmap (format 12 or 4)."""
    b = Path(path).read_bytes()
    off = _tables(b)['cmap'][0]
    subs = {}
    for i in range(struct.unpack_from('>H', b, off + 2)[0]):
        pid, eid, so = struct.unpack_from('>HHI', b, off + 4 + 8 * i)
        subs[(pid, eid)] = off + so
    codes = set()
    for key in ((3, 10), (0, 4), (0, 6)):
        if key in subs and struct.unpack_from('>H', b, subs[key])[0] == 12:
            t = subs[key]
            for g in range(struct.unpack_from('>I', b, t + 12)[0]):
                s, e, gid = struct.unpack_from('>III', b, t + 16 + 12 * g)
                codes.update(c for c in range(s, e + 1) if gid + c - s)
            return codes
    t = subs.get((3, 1)) or subs.get((0, 3))
    seg = struct.unpack_from('>H', b, t + 6)[0] // 2
    ends = struct.unpack_from('>%dH' % seg, b, t + 14)
    starts = struct.unpack_from('>%dH' % seg, b, t + 16 + 2 * seg)
    deltas = struct.unpack_from('>%dh' % seg, b, t + 16 + 4 * seg)
    ro_at = t + 16 + 6 * seg
    for i in range(seg):
        ro = struct.unpack_from('>H', b, ro_at + 2 * i)[0]
        for c in range(starts[i], ends[i] + 1):
            if c == 0xffff:
                continue
            if ro == 0:
                gid = (c + deltas[i]) & 0xffff
            else:
                gid = struct.unpack_from('>H', b, ro_at + 2 * i + ro + 2 * (c - starts[i]))[0]
                gid = (gid + deltas[i]) & 0xffff if gid else 0
            if gid:
                codes.add(c)
    return codes


def font_version(path):
    b = Path(path).read_bytes()
    off = _tables(b)['name'][0]
    _, count, strings = struct.unpack_from('>HHH', b, off)
    for i in range(count):
        pid, eid, lang, nid, ln, so = struct.unpack_from('>6H', b, off + 6 + 12 * i)
        if nid == 5 and pid == 3:
            return b[off + strings + so:off + strings + so + ln].decode('utf-16-be')
    return '?'


# --- font instances ---------------------------------------------------------------------------

def _axis_names(font):
    return [a['name'].decode() if isinstance(a['name'], bytes) else a['name']
            for a in font.get_variation_axes()]


def _open(path, size, axes):
    f = ImageFont.truetype(str(path), size)
    names = _axis_names(f)
    f.set_variation_by_axes([axes[n] for n in names])
    return f


def _ink(font, text):
    """Ink box (l, t, r, b) relative to the left-baseline origin, from the rendered mask."""
    l, t, r, b = font.getbbox(text, anchor='ls')
    pad = int(font.size) + 2
    im = Image.new('L', (r - l + 2 * pad, b - t + 2 * pad))
    ImageDraw.Draw(im).text((pad - l, pad - t), text, font=font, fill=255, anchor='ls')
    box = im.getbbox()
    if not box:
        return None
    return box[0] - (pad - l), box[1] - (pad - t), box[2] - (pad - l), box[3] - (pad - t)


class Instance:
    """A source font scaled so that H's cap height is `cap` output pixels, rendered at SS x."""

    def __init__(self, spec, cap, adv_target):
        self.path = ASSETS / spec['file']
        self.floor_mode = OPTS['floor_mode'] or spec.get('floor', 'binary')
        axes = dict(spec['axes'])
        ref = 1000.0

        def natural(axes_):
            f = _open(self.path, ref, axes_)
            c = -_ink(f, 'H')[1]
            return f, c, sum(f.getlength(chr(o)) for o in range(97, 123)) / 26 / c

        if axes.get('Width') == 'fit':
            lo, hi = 62.5, 100.0  # Noto Sans wdth range
            want = adv_target / cap
            for _ in range(30):
                mid = (lo + hi) / 2
                axes['Width'] = mid
                if natural(axes)[2] > want:
                    hi = mid
                else:
                    lo = mid
            axes['Width'] = round((lo + hi) / 2, 2)
        f, c, adv_per_cap = natural(axes)
        self.axes = axes
        self.px = cap * ref / c  # pixel em size at output resolution
        self.ss = 1 if OPTS['hinted'] else SS
        self.font = _open(self.path, self.px * self.ss, axes)
        # advances come from the 1000-px instance: hinted advances at the render size are
        # quantised to whole supersampled pixels and bias the rounded layout advances
        self.ref, self.ref_scale = f, self.px / ref
        self.fx = adv_target / (adv_per_cap * cap)
        self._fit_rounded(adv_target)
        self.codes = cmap_codes(self.path)
        self.cap_ss = -_ink(self.font, 'H')[1]  # cap height at render size
        self._thin = None

    def thinnest(self):
        """(stem, bar) at render size: thinnest vertical stroke of l i ! | and bar of - T."""
        if self._thin is None:
            def run(ch, vertical):
                l, t, r, b = self.font.getbbox(ch, anchor='ls')
                im = Image.new('L', (r - l + 8, b - t + 8))
                ImageDraw.Draw(im).text((4 - l, 4 - t), ch, font=self.font, fill=255, anchor='ls')
                a = np.asarray(im, np.float64) / 255
                if not vertical:
                    a = a.T
                rows = [row.sum() for row in a if row.max() > 0.5]
                return sorted(rows)[len(rows) // 2] if rows else 0.0
            stem = min(run(c, True) for c in 'li!|' if ord(c) in self.codes)
            bar = min(run(c, False) for c in '-T' if ord(c) in self.codes)
            self._thin = (stem, bar)
        return self._thin
    def _fit_rounded(self, target):
        """Nudge fx (within +-5 %) so the mean of the *rounded* a-z advances hits the target.

        Hinted advances are whole supersampled pixels, so the mean is a step function of fx
        with many exact .5 ties; a grid scan (ties to the unrounded fit) is robust where a
        bisection is not.
        """
        base = self.fx
        lengths = [self.ref.getlength(chr(o)) * self.ref_scale for o in range(97, 123)]
        grid = [base * (0.95 + 0.0001 * i) for i in range(1001)]
        self.fx = min(grid, key=lambda v: (abs(sum(math.floor(x * v + 0.5) for x in lengths) / 26
                                               - target), abs(v - base)))

    def advance(self, ch):
        return self.ref.getlength(ch) * self.ref_scale * self.fx


# --- glyph rendering --------------------------------------------------------------------------

class Glyph:
    __slots__ = ('code', 'A', 'B', 'C', 'alpha', 'rgb', 'squeezed')


def _shadow_taps(d):
    """SHADOW_D1 magnified to density d: each tap spreads over a d x d block, weight / d^2."""
    return [(dx * d + i, dy * d + j, w / (d * d))
            for (dx, dy), w in SHADOW_D1.items() for i in range(d) for j in range(d)]


def _squeeze(img, y0, y1, n0, n1):
    """Resample rows [y0, y1) of `img` into [n0, n1) (same x), clearing the rest of the span."""
    strip = img.crop((0, y0, img.width, y1)).resize((img.width, n1 - n0), Image.BOX)
    img.paste(0, (0, min(y0, n0), img.width, max(y1, n1)))
    img.paste(strip, (0, n0))


def _fit_band(cov, base_y, top_y, bottom_y, cap_y):
    """Keep the ink inside the engine's blit band [top_y, bottom_y) of the canvas.

    The blit is S*d - 1 rows high, so ink outside the band is cut (font-rendering.md 1.3). Marks
    above the cap line (accents on capitals) are squeezed vertically into the rows between the
    band top and the cap line; below the baseline, the descender part is squeezed into the rows
    left under it. Stock Zekton26 and Tahoma13 draw compact accents the same way; the letter
    body between cap line and baseline is never touched. Returns True when a squeeze happened.
    """
    box = cov.getbbox()
    if not box:
        return False
    done = False
    if box[1] < top_y and top_y < cap_y:
        _squeeze(cov, box[1], cap_y, top_y, cap_y)
        done = True
    if box[3] > bottom_y and base_y < bottom_y:
        _squeeze(cov, base_y, box[3], base_y, bottom_y)
        done = True
    return done


def _runs(mask):
    """(line, start, end) of every run of True along axis 1 of a 2-D bool array."""
    p = np.zeros((mask.shape[0], mask.shape[1] + 2), np.int8)
    p[:, 1:-1] = mask
    dif = np.diff(p, axis=1)
    rs, cs = np.nonzero(dif == 1)
    _, ce = np.nonzero(dif == -1)
    return rs, cs, ce


def _straight(starts, r, c0):
    """True when the run starting at (r, c0) continues a straight edge in row r-1 or r+1."""
    for rr in (r - 1, r + 1):
        if 0 <= rr < starts.shape[0]:
            for c in (c0 - 1, c0, c0 + 1):
                if 0 <= c < starts.shape[1] and starts[rr, c] >= 0 and abs(starts[rr, c] - c0) <= 1:
                    return True
    return False


def _floor_axis(mask, grid, origin, n_min, place):
    """Widen/snap the runs of `mask` along axis 1 (see _stroke_floor).

    grid: canvas px per output texel along the axis; origin: canvas coordinate of a texel
    boundary; runs narrower than n_min + 1 texels that continue a straight edge in a
    neighbouring line are redrawn as max(n_min, round(len / grid)) whole texels from the nearest
    boundary; other runs narrower than n_min texels (diagonals, curves) are widened in place.
    place(lo, hi, c0, c1, snapped) may move the new span (to keep a baseline); returns (lo, hi).
    """
    out = mask.copy()
    rs, cs, ce = _runs(mask)
    starts = np.full(mask.shape, -1, np.int32)
    for r, c0, c1 in zip(rs, cs, ce):
        starts[r, c0:c1] = c0
    for r, c0, c1 in zip(rs, cs, ce):
        n = (c1 - c0) / grid
        if n >= n_min + 1 or (n >= n_min and not _straight(starts, r, c0)):
            continue
        if _straight(starts, r, c0):
            k = round((c0 - origin) / grid)
            x0 = origin + k * grid
            x1 = x0 + max(n_min, round(n)) * grid
        else:
            x0 = c0
            x1 = c0 + n_min * grid
        lo, hi = place(int(math.ceil(x0 - 0.5)), int(math.ceil(x1 - 0.5)), c0, c1,
                       _straight(starts, r, c0))
        out[r, c0:c1] = False
        out[r, max(lo, 0):hi] = True
    return out


def _stroke_floor(cov, pad, col, d, base_y, top_y, bottom_y, SS=SS):
    """Stroke-width rule (font-assets.md): every stroke at least d whole texels.

    A 1-px stock stroke is d texels at density d, and a stroke with fewer full texels can vanish
    when the text texture is minified with nearest sampling (font-rendering.md section 5). On
    the supersampled canvas (`col` canvas px per output column, SS per output row, column 0 of
    the glyph at x = `pad`, output rows at `top_y` + k*SS):

    1. binarise at half coverage and move the ink so its left edge sits on x = `pad` (the
       whole-texel snap of A; at most one supersampled pixel);
    2. vertical strokes (horizontal runs): a run that continues a straight edge in the next
       row and is narrower than d + 1 texels is redrawn as max(d, round(width)) whole columns
       from the column boundary nearest its left edge (stem hinting); a diagonal or curved run
       narrower than d texels is widened to d towards the right without moving;
    3. horizontal bars (vertical runs), the same with rows, growing downwards, or upwards when
       the run sits on the baseline or would leave the band, so cap line and baseline stay put.

    Strokes of d + 1 texels or more are left as drawn. Returns the new coverage image.
    """
    a = np.asarray(cov) >= 128
    if not a.any():
        return cov
    xs = np.flatnonzero(a.any(axis=0))
    a = np.roll(a, pad - int(xs[0]), axis=1)
    a = _floor_axis(a, col, pad, d, lambda lo, hi, c0, c1, snapped: (lo, hi))

    def place_rows(lo, hi, r0, r1, snapped):
        if abs(r1 - base_y) <= SS // 2 or hi > bottom_y:   # on the baseline or leaving the band
            end = top_y + round((r1 - top_y) / SS) * SS if snapped else r1
            end = min(end, bottom_y)
            lo, hi = end - (hi - lo), end
            if lo < top_y:
                lo, hi = top_y, top_y + (hi - lo)
        return lo, hi

    t = _floor_axis(a.T, SS, top_y, d, place_rows).T
    return Image.fromarray(np.where(t, 255, 0).astype(np.uint8))


def _grey_floor(cov, wx, wy, top_y, bottom_y):
    """floor_mode 'grey': grey-scale dilation by the width the thinnest stem (wx, to the right)
    and bar (wy, split up and down, kept in the band) lack; no binarisation, so edges keep
    their antialiasing. wx, wy in canvas px; <= 0 leaves the coverage unchanged."""
    a = np.asarray(cov).astype(np.float64)
    nx, ny = max(0, int(math.ceil(wx))), max(0, int(math.ceil(wy)))
    out = a.copy()
    for k in range(1, nx + 1):
        w = 1.0 if k < nx or wx >= nx else wx - (nx - 1)
        sh = np.zeros_like(a)
        sh[:, k:] = a[:, :-k] * w
        out = np.maximum(out, sh)
    a = out.copy()
    up, down = (ny + 1) // 2, ny // 2
    for k in range(1, up + 1):
        sh = np.zeros_like(a)
        sh[:-k] = a[k:]
        out = np.maximum(out, sh)
    for k in range(1, down + 1):
        sh = np.zeros_like(a)
        sh[k:] = a[:-k]
        out = np.maximum(out, sh)
    out[:top_y] = 0
    out[bottom_y:] = 0
    return Image.fromarray(np.rint(out).astype(np.uint8))


def render(inst, ch, band, base, d, shadow):
    """Render `ch` into a B x band tile. `base` = baseline row (boundary) inside the band.

    The ink's left edge is snapped to the nearest output column (shift <= 0.5 px), so left
    stems land on whole texels, and every stroke is widened to at least d texels
    (_stroke_floor); the advance is unchanged.
    """
    g = Glyph()
    g.squeezed = False
    adv = math.floor(inst.advance(ch) + 0.5) if ch else 0
    ink = _ink(inst.font, ch) if ch and not ch.isspace() else None
    if ink is None:
        g.A, g.B, g.C = 0, 1, adv
        g.alpha = np.zeros((band, 1), np.uint8)
        g.rgb = None
        return g
    SSr = inst.ss
    fx = 1.0 if OPTS['hinted'] else inst.fx   # hinted: image at 1:1, advances still fitted
    l, t, r, b = ink
    A = round(l * fx / SSr)                   # ink left edge snapped to a whole column
    R = A + math.ceil((r - l) * fx / SSr)
    reach = max(dx for dx, _ in SHADOW_D1) * d + d if shadow else 0
    Bw = R + d + 1 + reach - A                # + room for the stroke floor
    pad = int(inst.font.size) + 4 + 2 * d * SSr
    ox = pad - l                              # ink left edge exactly at canvas x = pad
    oy = pad + base * SSr                     # canvas y of the baseline
    cov = Image.new('L', (math.ceil(Bw * SSr / fx) + 2 * pad, band * SSr + 2 * pad))
    ImageDraw.Draw(cov).text((ox, oy), ch, font=inst.font, fill=255, anchor='ls')
    top_y, bottom_y = oy - base * SSr, oy + (band - base) * SSr
    g.squeezed = _fit_band(cov, oy, top_y, bottom_y, oy - inst.cap_ss)
    if inst.floor_mode == 'binary':
        cov = _stroke_floor(cov, pad, SSr / fx, d, oy, top_y, bottom_y, SSr)
    elif inst.floor_mode == 'grey':
        stem, bar = inst.thinnest()
        cov = _grey_floor(cov, d * SSr / fx - stem, d * SSr - bar, top_y, bottom_y)
    box = (pad, pad, pad + Bw * SSr / fx, pad + band * SSr)
    ga = np.asarray(cov.resize((Bw, band), Image.BOX, box=box), np.float64) / 255.0
    if OPTS['gamma'] != 1.0:
        ga = ga ** (1.0 / OPTS['gamma'])
    if shadow:
        sh = np.zeros_like(ga)
        for dx, dy, w in _shadow_taps(d):
            if dx < Bw and dy < band:
                sh[dy:, dx:] += w * ga[:band - dy, :Bw - dx]
        sh = np.minimum(sh, 1.0)
        a = ga + sh * (1.0 - ga)
        alpha = np.rint(a * 255).astype(np.uint8)
        rgb = np.where(alpha > 0, np.rint(255 * ga / np.maximum(a, 1e-9)), 0).astype(np.uint8)
    else:
        alpha = np.rint(ga * 255).astype(np.uint8)
        rgb = None
    used = np.flatnonzero(alpha.any(axis=0))
    first, last = (int(used[0]), int(used[-1])) if used.size else (0, 0)
    g.alpha = np.ascontiguousarray(alpha[:, first:last + 1])
    g.rgb = None if rgb is None else np.ascontiguousarray(rgb[:, first:last + 1])
    g.A, g.B, g.C = A + first, last + 1 - first, adv - (A + first)
    return g


def char_for(name, code):
    """The character drawn for a stock code. Harrier maps C1 codes 0x82..0x9f: cp1252 bytes."""
    if code == 0xad:  # soft hyphen: drawn as a hyphen (Exo 2 has no U+00AD glyph)
        return '-'
    if 0x80 <= code < 0xa0:
        try:
            return bytes([code]).decode('cp1252')
        except UnicodeDecodeError:
            return None
    return chr(code)


def codes_of(meta):
    out = []
    for s, e in meta['codes']:
        out.extend(range(s, e + 1))
    return out


# --- packing and writing ----------------------------------------------------------------------

def pack(widths, cell):
    """Shelf-pack glyph cells in code order into the smallest power-of-two atlas.

    A 1-texel gap between cells is kept when it costs no area; the engine blits exactly B
    columns from trunc(W*u0) on the CPU (no filtering), so the gap is cosmetic only.
    """
    best = None
    for gap in (1, 0):
        for W in (256, 512, 1024, 2048):
            if max(widths) > W:
                continue
            x, shelves, pos = 0, 1, []
            for w in widths:
                if x + w > W:
                    x, shelves = 0, shelves + 1
                pos.append((x, (shelves - 1) * cell))
                x += w + gap
            H = 1 << max(0, (shelves * cell - 1).bit_length())
            if H > 4096:
                continue
            key = (W * H, abs(W.bit_length() - H.bit_length()), -gap)
            if best is None or key < best[0]:
                best = (key, W, H, pos)
    if best is None:
        raise ValueError('glyphs do not fit a 2048x4096 atlas')
    return best[1:]


def write_tga(path, W, H, bgra):
    hdr = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, W, H, 32, 0x28)
    path.write_bytes(hdr + bgra)


def build(name, d, meta, out_dir):
    """Render one font at density d; returns (abc_path, tga_path, info dict)."""
    S, yoff = meta['S'], meta['yoff']
    band = S * d - 1
    cell = yoff * d + band
    Hrows = meta['H_ink_rows_in_cell']
    cap = d * (Hrows[1] - Hrows[0] + 1)
    base = d * (Hrows[1] + 1 - yoff)
    adv_target = d * meta['advance_a_to_z_mean']
    shadow = bool(meta['baked_shadow'])
    spec = SOURCES[name]
    if OPTS['weight']:
        spec = dict(spec, axes=dict(spec['axes'], Weight=OPTS['weight']))
    prim = Instance(spec, cap, adv_target)
    fb = None
    glyphs, missing, fallback_codes = [], [], []
    sp = render(prim, ' ', band, base, d, shadow)
    sp.code = 0x20
    glyphs.append(sp)
    for code in codes_of(meta):
        ch = char_for(name, code)
        inst = prim
        if ch is not None and ord(ch) not in prim.codes and not ch.isspace():
            alias = unicodedata.normalize('NFKC', ch)  # e.g. U+037E Greek question mark -> ';'
            if len(alias) == 1 and ord(alias) in prim.codes:
                ch = alias
        if ch is not None and ord(ch) not in prim.codes and not ch.isspace():
            fb = fb or Instance(dict(FALLBACK, floor=spec['floor']), cap, adv_target)
            if ord(ch) in fb.codes:
                inst = fb
                fallback_codes.append(code)
            else:
                ch = None
        if ch is None:
            missing.append(code)
            g = render(prim, '', band, base, d, shadow)
            g.C = sp.C
        else:
            g = render(inst, ch, band, base, d, shadow)
        g.code = code
        for k, lo, hi in (('A', -128, 127), ('B', 1, 255), ('C', -128, 127)):
            v = getattr(g, k)
            if not lo <= v <= hi:
                raise ValueError('%s d=%d U+%04X %s=%d out of range' % (name, d, code, k, v))
        glyphs.append(g)
    W, H, pos = pack([g.B for g in glyphs], cell)
    atlas = np.zeros((H, W, 4), np.uint8)
    for g, (x, y) in zip(glyphs, pos):
        y0 = y + yoff * d
        tile = atlas[y0:y0 + band, x:x + g.B]
        tile[..., 3] = g.alpha
        tile[..., :3] = (g.rgb if g.rgb is not None else np.where(g.alpha > 0, 255, 0))[..., None]
    n = meta['highest_code']
    cmap = [0] * (n + 1)
    for i, g in enumerate(glyphs[1:], 1):
        cmap[g.code] = i
    abc = bytearray(struct.pack('<I4fH', 5, float(cell), 0.0, 2.0 * d if shadow else 0.0,
                                float(S * d), n))
    abc += struct.pack('<%dH' % (n + 1), *cmap)
    abc += struct.pack('<I', len(glyphs))
    for g, (x, y) in zip(glyphs, pos):
        abc += struct.pack('<4f3hH', x / W, y / H, (x + g.B) / W, (y + cell) / H, g.A, g.B, g.C, 0)
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = '%s%d' % (name, S * d)
    abc_path, tga_path = out_dir / (stem + '.abc'), out_dir / (stem + '.tga')
    abc_path.write_bytes(bytes(abc))
    write_tga(tga_path, W, H, atlas[..., [2, 1, 0, 3]].tobytes())
    info = {'name': stem, 'glyphs': len(glyphs), 'atlas': [W, H], 'cell_rows': cell,
            'band_rows': [yoff * d, yoff * d + band - 1], 'cap_px': cap, 'baseline_row': yoff * d + base,
            'source': SOURCES[name]['file'], 'axes': prim.axes, 'em_px': round(prim.px, 3),
            'x_scale': round(prim.fx, 4), 'fallback_codes': [hex(c) for c in fallback_codes],
            'blank_codes': [hex(c) for c in missing],
            'squeezed': ''.join(chr(g.code) for g in glyphs if g.squeezed)}
    return abc_path, tga_path, info


# --- report and preview -----------------------------------------------------------------------

def metric_ratios(abc_path, meta, d):
    b = abc_path.read_bytes()
    n = struct.unpack_from('<H', b, 0x14)[0]
    cmap = struct.unpack_from('<%dH' % (n + 1), b, 0x16)
    count = struct.unpack_from('<I', b, 0x18 + 2 * n)[0]
    recs = [struct.unpack_from('<4f3hH', b, 0x1c + 2 * n + 24 * i) for i in range(count)]

    def abc(ch):
        r = recs[cmap[ord(ch)]]
        return {'A': r[4], 'B': r[5], 'C': r[6]}
    adv = sum(recs[cmap[o]][4] + recs[cmap[o]][6] for o in range(97, 123)) / 26
    out = {'advance_a_to_z_mean': round(adv, 3),
           'ratio_advance_mean': round(adv / (d * meta['advance_a_to_z_mean']), 4)}
    for ch, key in (('H', 'H'), ('g', 'g'), (' ', 'space')):
        m = abc(ch)
        st = meta[key]
        out[key] = m
        out['ratio_%s_advance' % key] = round((m['A'] + m['C']) / (d * (st['A'] + st['C'])), 4)
        if key != 'space':
            out['ratio_%s_B' % key] = round(m['B'] / (d * st['B']), 4)
    return out


def preview(abc_path, tga_path, dst_dir, sample):
    b = abc_path.read_bytes()
    t = tga_path.read_bytes()
    W, H = struct.unpack_from('<HH', t, 12)
    img = Image.frombytes('RGBA', (W, H), t[18:18 + W * H * 4], 'raw', 'BGRA')
    bg = Image.new('RGBA', (W, H), (40, 60, 90, 255))
    Image.alpha_composite(bg, img).convert('RGB').save(dst_dir / (abc_path.stem + '_atlas.png'))
    n = struct.unpack_from('<H', b, 0x14)[0]
    cmap = struct.unpack_from('<%dH' % (n + 1), b, 0x16)
    count = struct.unpack_from('<I', b, 0x18 + 2 * n)[0]
    recs = [struct.unpack_from('<4f3hH', b, 0x1c + 2 * n + 24 * i) for i in range(count)]
    cell = round(H * (recs[0][3] - recs[0][1]))
    line = Image.new('RGBA', (sum(recs[cmap[ord(c)] if ord(c) <= n else 0][4] +
                                   recs[cmap[ord(c)] if ord(c) <= n else 0][6] for c in sample) + 40,
                              cell + 8), (40, 60, 90, 255))
    pen = 20
    for c in sample:
        if ord(c) > n:
            continue
        u0, v0, u1, v1, A, B, C, _ = recs[cmap[ord(c)]]
        x0, y0 = int(W * u0), int(H * v0)
        pen += A
        tile = img.crop((x0, y0, x0 + B, y0 + cell))
        line.alpha_composite(tile, (pen, 4))
        pen += C
    line.convert('RGB').save(dst_dir / (abc_path.stem + '_sample.png'))


SAMPLES = {'Tahoma': 'Hull 98% | Shields 120 MJ | Argon Prime — Ägypten, Mañana, Привет мир',
           'Zekton': 'TRADE COMMAND Argon Prime 1.234 Cr — Überraschung, Ångström',
           'ZektonES': '¿Dónde está la estación? ¡Comercio! 1.234 Cr — Señor',
           'Harrier': 'Торговый центр Аргон Прайм 1.234 Cr — Trade Station'}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', default=str(ROOT / 'build' / 'fonts'))
    ap.add_argument('--density', type=int, nargs='+', default=[2, 3])
    ap.add_argument('--font', nargs='+', choices=sorted(SOURCES), default=list(SOURCES))
    ap.add_argument('--preview', help='write atlas and sample-line PNGs here')
    ap.add_argument('--report', help='write metric ratios versus stock as JSON here')
    ap.add_argument('--weight', type=float, help='experiment: Weight axis override')
    ap.add_argument('--hinted', action='store_true', help='experiment: hinted 1:1 rendering')
    ap.add_argument('--floor-mode', choices=('binary', 'grey', 'none'),
                    help='stroke-width floor (default: per family, SOURCES)')
    ap.add_argument('--gamma', type=float, default=1.0, help='experiment: alpha = cov^(1/g)')
    a = ap.parse_args(argv)
    OPTS.update(weight=a.weight, hinted=a.hinted, floor_mode=a.floor_mode, gamma=a.gamma)
    meta = json.loads(STOCK.read_text())['fonts']
    out = Path(a.out) / 'F'
    report = {'sources': {}, 'fonts': {}}
    for f in sorted({s['file'] for s in SOURCES.values()} | {FALLBACK['file']}):
        p = ASSETS / f
        report['sources'][f] = {'version': font_version(p),
                                'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
    for name in a.font:
        for d in a.density:
            abc_path, tga_path, info = build(name, d, meta[name], out)
            info['ratios'] = metric_ratios(abc_path, meta[name], d)
            report['fonts'][info['name']] = info
            for p in (abc_path, tga_path):
                print('%-24s glyphs %4d  image %4dx%-4d  %9d bytes' % (
                    'F/' + p.name, info['glyphs'], info['atlas'][0], info['atlas'][1], p.stat().st_size))
            if a.preview:
                Path(a.preview).mkdir(parents=True, exist_ok=True)
                preview(abc_path, tga_path, Path(a.preview), SAMPLES[name])
    if a.report:
        Path(a.report).write_text(json.dumps(report, indent=1) + '\n')
    return report


if __name__ == '__main__':
    main()
