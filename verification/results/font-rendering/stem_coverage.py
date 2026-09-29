#!/usr/bin/env python3
"""Stem width of narrow glyphs, stock Tahoma13 (x d) against a generated Tahoma{13d}
(docs/reverse-engineering/font-rendering.md section 5).

White coverage of one glyph row = sum over the B columns of (R/255)*(A/255): the texels that
the alpha blit 0x004b2730 deposits as text colour (the baked shadow is black and adds nothing).
Reads the stock font from the installed catalogues in memory and the generated pair from
build/fonts; writes stem_coverage.json with numbers only.

  python3 verification/results/font-rendering/stem_coverage.py [generated_dir] [d]
"""
import importlib.util, json, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('ff', HERE / 'font_files.py')
ff = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ff)

GEN = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE.parents[2] / 'build/fonts/F'
D = int(sys.argv[2]) if len(sys.argv) > 2 else 2
CHARS = 'iljI!|.:tfrneH'


def load(abc_bytes, tga_bytes):
    h, cmap, recs = ff.parse_abc(abc_bytes)
    info = ff.parse_tga(tga_bytes)
    W, H = info['width'], info['height']
    top = info['origin'] == 'top-left'
    px = tga_bytes[18:18 + W * H * 4]

    def white(x, y):
        o = ((y if top else H - 1 - y) * W + x) * 4
        return px[o] / 255 * px[o + 3] / 255

    def stems(ch):
        r = recs[cmap[ord(ch)]]
        x0, y0 = int(W * r[0]), int(H * r[1])
        rows = round(H * (r[3] - r[1]))
        per_row = [sum(white(x, y0 + y) for x in range(x0, x0 + r[5])) for y in range(rows)]
        inked = [v for v in per_row if v > 0.25]
        # narrowest inked row = the thinnest vertical stroke crossing the glyph
        return {'A': r[4], 'B': r[5], 'C': r[6],
                'min_row_white': round(min(inked), 2) if inked else 0.0,
                'median_row_white': round(sorted(inked)[len(inked) // 2], 2) if inked else 0.0}
    return {ch: stems(ch) for ch in CHARS}


m = ff.members()
stock = load(ff.read(*m['f/tahoma13.abc']), ff.read(*m['f/tahoma13.tga']))
for v in stock.values():   # scale the stock stroke widths to the generated density
    v['min_row_white_x_d'] = round(v['min_row_white'] * D, 2)
    v['median_row_white_x_d'] = round(v['median_row_white'] * D, 2)
gen = load((GEN / f'Tahoma{13 * D}.abc').read_bytes(), (GEN / f'Tahoma{13 * D}.tga').read_bytes())
out = {'density': D, 'generated_dir': str(GEN.relative_to(HERE.parents[2])) if GEN.is_relative_to(HERE.parents[2]) else str(GEN),
       'stock_tahoma13': stock, f'generated_tahoma{13 * D}': gen}
(HERE / 'stem_coverage.json').write_text(json.dumps(out, indent=1) + '\n')
for ch in CHARS:
    print(f"{ch!r}: stock x{D} median {stock[ch]['median_row_white_x_d']:5.2f}  generated median "
          f"{gen[ch]['median_row_white']:5.2f}  A,B,C stock {stock[ch]['A']},{stock[ch]['B']},{stock[ch]['C']} "
          f"gen {gen[ch]['A']},{gen[ch]['B']},{gen[ch]['C']}")
