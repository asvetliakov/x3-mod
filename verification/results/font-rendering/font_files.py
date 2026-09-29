#!/usr/bin/env python3
"""Measure the installed X3AP bitmap fonts (docs/reverse-engineering/font-rendering.md, section 1).

Reads the `f/` members and every `types/Fonts*` table from the installed catalogues in memory
(read-only; `.dat` slices are XOR 0x33, `.pck` members are the scrambled gzip form) and writes only
derived facts -- sizes, SHA-256 of the decoded bytes, header fields, glyph counts, cell and band
statistics -- to font_files.json next to this script. No glyph image or metric table is copied.

  python3 verification/results/font-rendering/font_files.py [game_root]
"""
import collections, gzip, hashlib, json, struct, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
from inspect_x3 import read_catalogue  # noqa: E402

GAME = Path(sys.argv[1]) if len(sys.argv) > 1 else \
    Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3'
CATS = sorted(GAME.glob('*.cat')) + sorted((GAME / 'addon').glob('*.cat'))

# (font file stem, cell height = the size argument S, y offset = 4th Fonts column / native push)
OPENED = {'Tahoma13': (13, 1), 'Zekton26': (26, 8), 'ZektonES26': (26, 0), 'HarRier24': (24, 5)}


def members():
    """Winning member per lower-cased path (last catalogue in engine order wins)."""
    out = {}
    for cat in CATS:
        for e in read_catalogue(cat):
            out[e['path'].lower()] = (cat, e)
    return out


def read(cat, e):
    with open(cat.with_suffix('.dat'), 'rb') as fh:
        fh.seek(e['offset'])
        return bytes(b ^ 0x33 for b in fh.read(e['size']))


def unpck(b):
    k = b[0] ^ 0x1f
    return gzip.decompress(bytes(x ^ k for x in b)).decode('latin1')


def parse_abc(b):
    ver = struct.unpack_from('<I', b, 0)[0]
    h = {'version': ver, 'size': len(b)}
    if ver == 4:
        h['header_int32_0x04_0x10'] = list(struct.unpack_from('<4i', b, 4))
        first, last = struct.unpack_from('<HH', b, 0x14)
        h['count_field_0x18'] = struct.unpack_from('<I', b, 0x18)[0]
        cmap, count, data = None, last - first + 1, 0x1c
    elif ver == 5:
        h['header_float_0x04_0x10'] = list(struct.unpack_from('<4f', b, 4))
        n = struct.unpack_from('<H', b, 0x14)[0]
        cmap = struct.unpack_from('<%dH' % (n + 1), b, 0x16)
        count = struct.unpack_from('<I', b, 0x18 + 2 * n)[0]
        data, first, last = 0x1c + 2 * n, 0, n
        h['map_entries'] = n + 1
        h['mapped_codes_nonzero'] = sum(1 for g in cmap[1:] if g)
    else:
        raise ValueError(ver)
    recs = [struct.unpack_from('<4f3hH', b, data + 24 * i) for i in range(count)]
    h.update(first=first, last=last, glyphs=count, records_at=data,
             size_matches_layout=(data + 24 * count == len(b)),
             pad_0x16_nonzero=sum(1 for r in recs if r[7]))
    return h, cmap, recs


def parse_tga(b):
    it = b[2]
    w, hgt, bpp, desc = struct.unpack_from('<HHBB', b, 12)
    return {'image_type': it, 'width': w, 'height': hgt, 'bpp': bpp, 'descriptor': desc,
            'origin': 'top-left' if desc & 0x20 else 'bottom-left', 'alpha_bits': desc & 15,
            'footer_TRUEVISION': b.endswith(b'TRUEVISION-XFILE.\0'), 'size': len(b)}


def glyph_stats(name, abc, tga_b, tga):
    h, cmap, recs = abc
    W, H = tga['width'], tga['height']
    top = tga['origin'] == 'top-left'
    px = tga_b[18:18 + W * H * 4]

    def alpha(x, y):
        row = y if top else H - 1 - y
        return px[(row * W + x) * 4 + 3]

    S, yoff = OPENED[name]
    cells = collections.Counter()
    exact = 0
    first_ink = collections.Counter()
    last_ink = collections.Counter()
    for r in recs:
        u0, v0, u1, v1, a, bw, c, _ = r
        x0, y0 = int(W * u0), int(H * v0)
        cells[round(H * (v1 - v0))] += 1
        exact += (round(W * (u1 - u0)) == bw) and (W * u0 == x0) and (H * v0 == y0)
        rows = [y - y0 for y in range(y0, round(H * v1)) if any(alpha(x, y) for x in range(x0, x0 + bw))]
        if rows:
            first_ink[min(rows)] += 1
            last_ink[max(rows)] += 1

    def ink(ch):
        u0, v0, u1, v1, a, bw, c, _ = recs[cmap[ord(ch)] if cmap else ord(ch) - h['first']]
        x0, y0 = int(W * u0), int(H * v0)
        rows = [y - y0 for y in range(y0, round(H * v1)) if any(alpha(x, y) > 127 for x in range(x0, x0 + bw))]
        return {'A': a, 'B': bw, 'C': c, 'ink_rows': [min(rows), max(rows)] if rows else None}

    adv = [recs[cmap[o]][4] + recs[cmap[o]][6] for o in range(ord('a'), ord('z') + 1)]
    alphas = collections.Counter(px[3::4])
    rgb_white_where_ink = sum(1 for i in range(0, len(px), 4) if px[i + 3] and px[i:i + 3] == b'\xff\xff\xff')
    ink_px = sum(1 for i in range(3, len(px), 4) if px[i])
    return {'cell_rows': dict(cells), 'glyphs_exact_uv_and_B': exact,
            'A_negative': sum(1 for r in recs if r[4] < 0), 'B_max': max(r[5] for r in recs),
            'advance_A_plus_C_a_to_z_mean': round(sum(adv) / len(adv), 3),
            'engine_band_rows_in_cell': [yoff, yoff + S - 2],
            'first_ink_row_hist': dict(sorted(first_ink.items())),
            'last_ink_row_hist': dict(sorted(last_ink.items())),
            'H': ink('H'), 'g': ink('g'), 'space': ink(' '),
            'alpha_levels': len(alphas), 'inked_px': ink_px,
            'inked_px_rgb_white': rgb_white_where_ink}


def main():
    m = members()
    out = {'game_root': ('~/' + str(GAME.relative_to(Path.home()))) if GAME.is_relative_to(Path.home()) else str(GAME), 'fonts_tables': {}, 'font_files': {}, 'stats': {}}
    for p, (cat, e) in sorted(m.items()):
        if '/types/fonts' in '/' + p:
            out['fonts_tables'][p] = {
                'catalogue': str(cat.relative_to(GAME)),
                'rows': [l.strip() for l in unpck(read(cat, e)).splitlines() if l.strip()]}
    decoded = {}
    for p, (cat, e) in sorted(m.items()):
        if p.startswith('f/'):
            b = read(cat, e)
            decoded[p] = b
            rec = {'catalogue': str(cat.relative_to(GAME)), 'size': len(b),
                   'sha256': hashlib.sha256(b).hexdigest()}
            if p.endswith('.abc'):
                rec['abc'] = parse_abc(b)[0]
            elif p.endswith('.tga'):
                rec['tga'] = parse_tga(b)
            elif p.endswith('.bmp'):
                rec['bmp'] = {'magic': b[:2].decode(), 'width': struct.unpack_from('<i', b, 18)[0],
                              'height': struct.unpack_from('<i', b, 22)[0],
                              'bpp': struct.unpack_from('<H', b, 28)[0]}
            out['font_files'][p] = rec
    for name in OPENED:
        a = decoded['f/%s.abc' % name.lower()]
        t = decoded['f/%s.tga' % name.lower()]
        out['stats'][name] = glyph_stats(name, parse_abc(a), t, parse_tga(t))
    dst = Path(__file__).with_name('font_files.json')
    dst.write_text(json.dumps(out, indent=1) + '\n')
    print('wrote', dst)
    for name, s in out['stats'].items():
        print(name, 'band', s['engine_band_rows_in_cell'], 'H', s['H'], 'g', s['g'],
              'exact', s['glyphs_exact_uv_and_B'], 'cells', s['cell_rows'])


if __name__ == '__main__':
    main()
