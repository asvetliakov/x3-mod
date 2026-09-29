#!/usr/bin/env python3
"""Show generated fonts the way the game shows them under --ui-scale (font-assets.md).

For each variant (a directory holding F/Tahoma26.* and optionally F/Zekton52.*) a sample block
is laid out with the engine's pen rule (pen += A, blit B x (S*d - 1), pen += C; line pitch S*d),
blitted into a transparent text texture with the 0x004b2730 formula
(dst = src_rgb * a * colour + dst * (1 - a), dst_a = dst_a * (1 - a) + a), then drawn onto the
panel colour: the texture is sampled with an explicit 2x2 bilinear tap at every screen-pixel
centre (texel coordinate (X + 0.5) * d / s - 0.5) and blended with SRCALPHA / INVSRCALPHA.
Each result is saved at 4x nearest zoom, plus one labelled sheet per font and scale.

  python3 tools/fonts/preview_minified.py --out DIR --variant A=build/fonts [--variant B=DIR ...]
      [--scale 1.25 1.5 2] [--stock] [--make-variants DIR | --variants-dir DIR]

--make-variants DIR generates the experiment set into DIR/<label> first (Tahoma: A wght 400 +
binary floor, B wght 600 + binary, C400/C600 hinted, D wght 600 + grey floor = shipped, E = A +
gamma 1.45; 'shipped' Tahoma + Zekton; E_large Zekton + gamma) and adds them.
--stock adds the stock Tahoma13/Zekton26 at d = 1 read from the installed catalogues in memory
(nothing is written but the PNGs).
"""
import argparse
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
PANEL = (37, 37, 37)          # text4.png, Titles block background (measured)
COLOUR = (191, 198, 229)      # text4.png, full-coverage text colour (measured)
HUD_LINES = [('Trade Rank', 'Salesman', '0%'), ('Combat Rank', 'Harmless', '0.0000%'),
             ('Split', 'Distinguished Associate', '50%'), ('Teladi', 'Profit Initiate', '50%'),
             ('Experimental Transporter Device', '', '1'),
             ('Bounty earned with a Police License', '', '0 Cr'),
             ('Time saved using SETA', '', '00:04:35'), ('Jumpgates', 'illicit lil | i! rn', '1235')]
LARGE_LINES = ['Pilot Information', 'Trade Command: Argon Prime', 'Sai t\'Nst (Independent)']
FONTS = {'Tahoma': (13, 'HUD'), 'Zekton': (26, 'LARGE')}


def load_font(abc, tga):
    n = struct.unpack_from('<H', abc, 0x14)[0]
    cmap = struct.unpack_from('<%dH' % (n + 1), abc, 0x16)
    count = struct.unpack_from('<I', abc, 0x18 + 2 * n)[0]
    recs = [struct.unpack_from('<4f3hH', abc, 0x1c + 2 * n + 24 * i) for i in range(count)]
    W, H, bpp, desc = struct.unpack_from('<HHBB', tga, 12)
    img = np.frombuffer(tga[18:18 + W * H * 4], np.uint8).reshape(H, W, 4)[..., [2, 1, 0, 3]]
    if not desc & 0x20:
        img = img[::-1]
    return cmap, recs, img.astype(np.float64) / 255.0


def text_texture(font, S, d, yoff, lines):
    """RGBA float texture (premultiplied by the blit, as in the engine) of the laid-out lines."""
    cmap, recs, img = font
    Ht, Wt = img.shape[:2]
    col = np.array(COLOUR) / 255.0
    pitch = S * d
    rows = S * d - 1
    tex = np.zeros((pitch * (len(lines) + 1), 900 * d // 2 + 64, 4))
    for li, parts in enumerate(lines):
        # layout-pixel column starts as in the Pilot menu (10, 93, 225 at 1x)
        for x_layout, text in zip((10, 93, 225), parts if isinstance(parts, tuple) else (parts,)):
            pen = x_layout * d
            y = 4 + li * pitch
            for ch in text:
                o = ord(ch)
                if o >= len(cmap):
                    continue
                u0, v0, u1, v1, A, B, C, _ = recs[cmap[o]]
                x0, y0 = int(Wt * u0), int(Ht * v0) + yoff * d   # blit starts yoff*d below v0
                pen += A
                src = img[y0:y0 + rows, x0:x0 + B]
                dst = tex[y:y + src.shape[0], pen:pen + src.shape[1]]
                a = src[..., 3:4]
                dst[..., :3] = src[..., :3] * a * col + dst[..., :3] * (1 - a)
                dst[..., 3:4] = dst[..., 3:4] * (1 - a) + a
                pen += C
    used = np.flatnonzero(tex[..., 3].any(axis=0))
    return tex[:, :int(used[-1]) + 8] if used.size else tex


def bilinear(tex, k):
    """Sample tex at every screen-pixel centre, texel coordinate (X + 0.5) * k - 0.5."""
    Ht, Wt = tex.shape[:2]
    Ws, Hs = int(Wt / k), int(Ht / k)
    xs = (np.arange(Ws) + 0.5) * k - 0.5
    ys = (np.arange(Hs) + 0.5) * k - 0.5
    x0 = np.clip(np.floor(xs).astype(int), 0, Wt - 1)
    y0 = np.clip(np.floor(ys).astype(int), 0, Ht - 1)
    x1, y1 = np.clip(x0 + 1, 0, Wt - 1), np.clip(y0 + 1, 0, Ht - 1)
    fx = (xs - np.floor(xs))[None, :, None]
    fy = (ys - np.floor(ys))[:, None, None]
    t00, t01 = tex[y0][:, x0], tex[y0][:, x1]
    t10, t11 = tex[y1][:, x0], tex[y1][:, x1]
    return (t00 * (1 - fx) + t01 * fx) * (1 - fy) + (t10 * (1 - fx) + t11 * fx) * fy


def screen(tex, k):
    s = bilinear(tex, k)
    panel = np.array(PANEL) / 255.0
    out = s[..., :3] * s[..., 3:4] + panel * (1 - s[..., 3:4])
    return Image.fromarray(np.rint(np.clip(out, 0, 1) * 255).astype(np.uint8))


def zoom(im, z=4):
    return im.resize((im.width * z, im.height * z), Image.NEAREST)


def make_variants(base):
    sys.path.insert(0, str(Path(__file__).parent))
    import generate_fonts as g
    # Tahoma experiment set of 2026-09-30 (A = the pre-D default, D = shipped since then);
    # the LARGE family only gets its shipped output and the gamma variant, since weight
    # switches apply to every family generated in one call.
    a400 = ['--weight', '400', '--floor-mode', 'binary']
    sets = {'A': (['Tahoma'], a400), 'B': (['Tahoma'], ['--weight', '600', '--floor-mode', 'binary']),
            'C400': (['Tahoma'], ['--hinted', '--floor-mode', 'none', '--weight', '400']),
            'C600': (['Tahoma'], ['--hinted', '--floor-mode', 'none', '--weight', '600']),
            'D': (['Tahoma'], ['--weight', '600', '--floor-mode', 'grey']),
            'E': (['Tahoma'], a400 + ['--gamma', '1.45']),
            'shipped': (['Tahoma', 'Zekton'], []), 'E_large': (['Zekton'], ['--gamma', '1.45'])}
    out = {}
    for label, (fonts, extra) in sets.items():
        g.OPTS.update(weight=None, hinted=False, floor_mode=None, gamma=1.0)
        dst = Path(base) / label
        g.main(['--out', str(dst), '--density', '2', '--font'] + fonts + extra)
        out[label] = dst
    return out


def stock_fonts():
    import importlib.util
    saved, sys.argv = sys.argv, [sys.argv[0]]
    try:
        spec = importlib.util.spec_from_file_location(
            'ff', ROOT / 'verification/results/font-rendering/font_files.py')
        ff = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(ff)
    finally:
        sys.argv = saved
    m = ff.members()
    return {name: (ff.read(*m['f/%s%d.abc' % (name.lower(), S)]), ff.read(*m['f/%s%d.tga' % (name.lower(), S)]))
            for name, (S, _) in FONTS.items()}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', required=True)
    ap.add_argument('--variant', action='append', default=[], help='LABEL=DIR (DIR/F/Tahoma26.*)')
    ap.add_argument('--scale', type=float, nargs='+', default=[1.25, 1.5, 2.0])
    ap.add_argument('--stock', action='store_true')
    ap.add_argument('--make-variants')
    ap.add_argument('--variants-dir', help='add every subdirectory as a variant (label = name)')
    a = ap.parse_args(argv)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    variants = {}
    if a.make_variants:
        variants.update(make_variants(a.make_variants))
    if a.variants_dir:
        for sub in sorted(Path(a.variants_dir).iterdir()):
            if (sub / 'F').is_dir():
                variants[sub.name] = sub
    for v in a.variant:
        label, _, d = v.partition('=')
        variants[label] = Path(d)
    sources = []   # (label, font name, d, abc bytes, tga bytes)
    if a.stock:
        for name, (abc, tga) in stock_fonts().items():
            sources.append(('stock', name, 1, abc, tga))
    for label, d in variants.items():
        for name, (S, _) in FONTS.items():
            stem = d / 'F' / ('%s%d' % (name, S * 2))
            if stem.with_suffix('.abc').exists():
                sources.append((label, name, 2, stem.with_suffix('.abc').read_bytes(),
                                stem.with_suffix('.tga').read_bytes()))
    yoffs = {'Tahoma': 1, 'Zekton': 8}
    sheets = {}
    written = []
    for label, name, d, abc, tga in sources:
        S = FONTS[name][0]
        lines = HUD_LINES if name == 'Tahoma' else LARGE_LINES
        tex = text_texture(load_font(abc, tga), S, d, yoffs[name], lines)
        for s in a.scale:
            im = zoom(screen(tex, d / s))
            p = out / ('%s_%s_s%.2f.png' % (name, label, s))
            im.save(p)
            written.append(p)
            sheets.setdefault((name, s), []).append((label, im))
    for (name, s), ims in sheets.items():
        W = max(im.width for _, im in ims)
        H = sum(im.height + 24 for _, im in ims)
        sheet = Image.new('RGB', (W, H), (0, 0, 0))
        dr = ImageDraw.Draw(sheet)
        y = 0
        for label, im in ims:
            dr.text((4, y + 4), '%s  s=%.2f' % (label, s), fill=(255, 255, 0))
            sheet.paste(im, (0, y + 24))
            y += im.height + 24
        p = out / ('sheet_%s_s%.2f.png' % (name, s))
        sheet.save(p)
        written.append(p)
    for p in written:
        print(p)


if __name__ == '__main__':
    main()
