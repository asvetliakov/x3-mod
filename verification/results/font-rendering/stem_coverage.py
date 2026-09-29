#!/usr/bin/env python3
"""Stroke width of narrow glyphs: stock fonts (x d) against the generated F\\<Name><S*d> pairs
(docs/reverse-engineering/font-rendering.md section 5, docs/architecture/font-assets.md).

White coverage = (R/255)*(A/255) per texel, i.e. what the alpha blit 0x004b2730 deposits as
text colour (a baked black shadow adds nothing). Per glyph:
  row metric    = median over inked rows of the row's white sum (vertical strokes, i l r ! |)
  column metric = median over inked columns of the column's white sum (horizontal bars, - T)
Rule: every metric >= d for every checked character the font maps (the stock target: a 1-px
stock stroke is d texels). Reads the stock fonts from the installed catalogues in memory when
available and the generated pairs from build/fonts/F; writes stem_coverage.json, numbers only.

  python3 verification/results/font-rendering/stem_coverage.py [--gen DIR] [--before DIR]
"""
import argparse, importlib.util, json, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
# output name -> (stock stem, S)
FONTS = {'Tahoma': ('Tahoma13', 13), 'Zekton': ('Zekton26', 26), 'ZektonES': ('ZektonES26', 26),
         'Harrier': ('HarRier24', 24)}
DENSITIES = (2, 3)
ROW_CHARS = 'ilr!|Itj' + 'іїг'   # + Cyrillic i, yi, ge where mapped
COL_CHARS = '-T'


def _font_files():
    saved = sys.argv
    sys.argv = [saved[0]]
    try:
        spec = importlib.util.spec_from_file_location('font_files', HERE / 'font_files.py')
        ff = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(ff)
    finally:
        sys.argv = saved
    return ff


def measure(abc_bytes, tga_bytes, ff=None):
    """{char: {'A','B','C','row','col'}} for the checked characters the font maps."""
    ff = ff or _font_files()
    h, cmap, recs = ff.parse_abc(abc_bytes)
    info = ff.parse_tga(tga_bytes)
    W, H = info['width'], info['height']
    top = info['origin'] == 'top-left'
    px = tga_bytes[18:18 + W * H * 4]

    def white(x, y):
        o = ((y if top else H - 1 - y) * W + x) * 4
        return px[o] / 255 * px[o + 3] / 255

    def median(v):
        v = [x for x in v if x > 0.25]
        return round(sorted(v)[len(v) // 2], 2) if v else 0.0

    out = {}
    for ch in ROW_CHARS + COL_CHARS:
        o = ord(ch)
        if o >= len(cmap) or not cmap[o]:
            continue
        r = recs[cmap[o]]
        x0, y0 = int(W * r[0]), int(H * r[1])
        rows = round(H * (r[3] - r[1]))
        grid = [[white(x, y0 + y) for x in range(x0, x0 + r[5])] for y in range(rows)]
        m = {'A': r[4], 'B': r[5], 'C': r[6]}
        if ch in ROW_CHARS:
            m['row'] = median([sum(row) for row in grid])
        else:
            m['col'] = median([sum(col) for col in zip(*grid)])
        out[ch] = m
    return out


def failures(result, d):
    """Characters whose metric is below d."""
    return sorted(ch for ch, m in result.items() if m.get('row', m.get('col')) < d)


def generated(gen_dir, ff):
    out = {}
    for name, (_, S) in FONTS.items():
        for d in DENSITIES:
            stem = gen_dir / ('%s%d' % (name, S * d))
            if stem.with_suffix('.abc').exists():
                out['%s%d' % (name, S * d)] = measure(stem.with_suffix('.abc').read_bytes(),
                                                      stem.with_suffix('.tga').read_bytes(), ff)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gen', default=str(ROOT / 'build/fonts/F'))
    ap.add_argument('--before', help='an earlier generated F directory, for a before/after table')
    a = ap.parse_args()
    ff = _font_files()
    out = {'rule': 'row/col median >= d', 'stock_x_d': {}, 'after': {}, 'before': {}}
    try:
        m = ff.members()
        for name, (stem, S) in FONTS.items():
            st = measure(ff.read(*m['f/%s.abc' % stem.lower()]), ff.read(*m['f/%s.tga' % stem.lower()]), ff)
            out['stock_x_d'][name] = {ch: v.get('row', v.get('col')) for ch, v in st.items()}
    except (OSError, KeyError, ValueError) as e:   # no game install: generated fonts only
        out['stock_error'] = str(e)
    out['after'] = generated(Path(a.gen), ff)
    if a.before:
        out['before'] = generated(Path(a.before), ff)
    ok = True
    print('%-12s %-3s %6s %6s %6s  %s' % ('font', 'ch', 'stock1', 'before', 'after', 'A,B,C after'))
    for font, res in out['after'].items():
        d = int(font[-2:]) // FONTS[font[:-2]][1]
        bad = failures(res, d)
        ok &= not bad
        out['after'][font] = {'d': d, 'fail': bad, 'glyphs': res}
        for ch, v in res.items():
            val = v.get('row', v.get('col'))
            b = out['before'].get(font, {}).get(ch, {})
            bv = b.get('row', b.get('col', '')) if b else ''
            print('%-12s %-3s %6s %6s %6.2f  %d,%d,%d' % (
                font, ch, out['stock_x_d'].get(font[:-2], {}).get(ch, ''), bv, val, v['A'], v['B'], v['C']))
    for font in list(out['before']):
        out['before'][font] = {ch: v.get('row', v.get('col')) for ch, v in out['before'][font].items()}
    out['pass'] = ok
    (HERE / 'stem_coverage.json').write_text(json.dumps(out, indent=1, ensure_ascii=False) + '\n')
    print('PASS' if ok else 'FAIL', {f: v['fail'] for f, v in out['after'].items() if v['fail']})
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
