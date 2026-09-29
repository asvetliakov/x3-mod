#!/usr/bin/env python3
"""Stroke coverage and metric fit of the font experiment variants (font-assets.md, "Readability
under minification"). Reads each variant's F/Tahoma26 and F/Zekton52 made by
`tools/fonts/preview_minified.py --make-variants DIR`, measures the median white coverage per
checked glyph with stem_coverage.measure and the mean a-z advance ratio against the stock value,
and writes variant_coverage.json next to this script (numbers only).

  python3 verification/results/font-rendering/variant_coverage.py DIR
"""
import importlib.util, json, struct, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
spec = importlib.util.spec_from_file_location('sc', HERE / 'stem_coverage.py')
sc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sc)
STOCK = json.loads((ROOT / 'tools/fonts/stock_fonts.json').read_text())['fonts']


def mean_adv(abc):
    n = struct.unpack_from('<H', abc, 0x14)[0]
    cmap = struct.unpack_from('<%dH' % (n + 1), abc, 0x16)
    recs = [struct.unpack_from('<4f3h', abc, 0x1c + 2 * n + 24 * cmap[o]) for o in range(97, 123)]
    return sum(r[4] + r[6] for r in recs) / 26


def main():
    base = Path(sys.argv[1])
    ff = sc._font_files()
    out = {}
    for var in sorted(p for p in base.iterdir() if (p / 'F').is_dir()):
        for name, S in (('Tahoma', 13), ('Zekton', 26)):
            stem = var / 'F' / ('%s%d' % (name, 2 * S))
            if not stem.with_suffix('.abc').exists():
                continue
            abc = stem.with_suffix('.abc').read_bytes()
            m = sc.measure(abc, stem.with_suffix('.tga').read_bytes(), ff)
            out['%s/%s' % (var.name, name)] = {
                'coverage': {ch: v.get('row', v.get('col')) for ch, v in m.items()},
                'fail_below_d': sc.failures(m, 2),
                'advance_ratio': round(mean_adv(abc) / (2 * STOCK[name]['advance_a_to_z_mean']), 4)}
    (HERE / 'variant_coverage.json').write_text(json.dumps(out, indent=1, ensure_ascii=False) + '\n')
    for k, v in out.items():
        c = v['coverage']
        print('%-14s adv %.3f  ' % (k, v['advance_ratio']) +
              ' '.join('%s=%.2f' % (ch, c[ch]) for ch in 'ilr!|ItjI-T' if ch in c),
              'FAIL' if v['fail_below_d'] else '')


if __name__ == '__main__':
    main()
