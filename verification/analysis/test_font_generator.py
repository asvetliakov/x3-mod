"""Round-trip check of tools/fonts/generate_fonts.py (docs/architecture/font-assets.md).

Generates Tahoma and Zekton at d = 2 into a temp dir, parses them back with the reader that
measured the stock fonts (verification/results/font-rendering/font_files.py) and checks the
engine constraints of docs/reverse-engineering/font-rendering.md sections 1 and 4 plus the
metric ratios against the stock values in font_files.json.
"""
import contextlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
D = 2

try:
    from PIL import ImageFont
    HAVE_FT = bool(ImageFont.core.freetype2_version)
except Exception:  # pragma: no cover - environment without Pillow/FreeType
    HAVE_FT = False


def _load(name, path, argv=None):
    saved = sys.argv
    sys.argv = [str(path)] + (argv or [])
    try:
        spec = importlib.util.spec_from_file_location(name, path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
    finally:
        sys.argv = saved
    return mod


@unittest.skipUnless(HAVE_FT, 'Pillow with FreeType required')
class FontGeneratorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.ff = _load('font_files', ROOT / 'verification/results/font-rendering/font_files.py')
        cls.gen = _load('generate_fonts', ROOT / 'tools/fonts/generate_fonts.py')
        cls.stock = json.loads((ROOT / 'tools/fonts/stock_fonts.json').read_text())['fonts']
        cls.stats = json.loads(
            (ROOT / 'verification/results/font-rendering/font_files.json').read_text())
        cls.tmp = tempfile.TemporaryDirectory()
        with contextlib.redirect_stdout(io.StringIO()):
            cls.gen.main(['--out', cls.tmp.name, '--density', str(D), '--font', 'Tahoma', 'Zekton'])
        cls.fonts = {}
        for name in ('Tahoma', 'Zekton'):
            S = cls.stock[name]['S']
            stem = Path(cls.tmp.name) / 'F' / ('%s%d' % (name, S * D))
            abc = stem.with_suffix('.abc').read_bytes()
            tga = stem.with_suffix('.tga').read_bytes()
            cls.fonts[name] = (cls.ff.parse_abc(abc), cls.ff.parse_tga(tga), tga)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def glyphs(self, name):
        """Yield (code or None, record, x0, y0, cell rows) per glyph; also the alpha accessor."""
        (h, cmap, recs), t, raw = self.fonts[name]
        W, H = t['width'], t['height']
        return h, cmap, recs, W, H, raw[18:18 + W * H * 4]

    def test_files_and_image_format(self):
        for name in self.fonts:
            (h, cmap, recs), t, raw = self.fonts[name]
            self.assertEqual(h['version'], 5)
            self.assertTrue(h['size_matches_layout'])
            self.assertEqual((t['image_type'], t['bpp'], t['alpha_bits']), (2, 32, 8))
            for side in (t['width'], t['height']):
                self.assertEqual(side & (side - 1), 0, '%s not a power of two' % side)
            self.assertEqual(len(raw), 18 + t['width'] * t['height'] * 4)

    def test_code_coverage_equals_stock(self):
        for name in self.fonts:
            (h, cmap, recs), _, _ = self.fonts[name]
            meta = self.stock[name]
            want = set()
            for s, e in meta['codes']:
                want.update(range(s, e + 1))
            got = {c for c, g in enumerate(cmap) if g}
            self.assertEqual(got, want, name)
            self.assertEqual(h['last'], meta['highest_code'])
            stock_abc = self.stats['font_files']['f/%s.abc' % meta['stock_stem'].lower()]['abc']
            self.assertEqual(len(got), stock_abc['mapped_codes_nonzero'])
            self.assertEqual(h['glyphs'], stock_abc['glyphs'])
            self.assertEqual(cmap[0x20], 0, 'space is glyph 0 as in the stock files')

    def test_records_band_and_ranges(self):
        for name in self.fonts:
            h, cmap, recs, W, H, px = self.glyphs(name)
            S, yoff = self.stock[name]['S'], self.stock[name]['yoff']
            band = (yoff * D, yoff * D + S * D - 2)
            ink_total = sum(px[3::4])
            ink_in_rects = 0
            for u0, v0, u1, v1, a, b, c, pad in recs:
                self.assertTrue(1 <= b <= 255 and -128 <= a <= 127 and -128 <= c <= 127)
                self.assertEqual(pad, 0)
                x0, y0 = W * u0, H * v0
                self.assertEqual((x0, y0), (int(x0), int(y0)), 'uv not texel exact')
                x0, y0 = int(x0), int(y0)
                self.assertEqual(round(W * (u1 - u0)), b)
                self.assertLessEqual(x0 + b, W)
                cell = round(H * (v1 - v0))
                self.assertEqual(cell, band[1] + 1)
                for row in range(cell):
                    line = px[((y0 + row) * W + x0) * 4 + 3:((y0 + row) * W + x0 + b) * 4:4]
                    s = sum(line)
                    ink_in_rects += s
                    if s:
                        self.assertTrue(band[0] <= row <= band[1],
                                        '%s ink in cell row %d outside band %s' % (name, row, band))
            self.assertEqual(ink_in_rects, ink_total, '%s ink outside glyph rectangles' % name)

    def test_shadow_and_colour(self):
        for name in self.fonts:
            _, _, _, W, H, px = self.glyphs(name)
            inked = [i for i in range(0, len(px), 4) if px[i + 3]]
            dark = sum(1 for i in inked if px[i] < 128)
            if self.stock[name]['baked_shadow']:
                # stock Tahoma13: 72 % of inked texels are black shadow
                self.assertGreater(dark / len(inked), 0.3, name)
            else:
                self.assertEqual(dark, 0, name)
                self.assertTrue(all(px[i:i + 3] == b'\xff\xff\xff' for i in inked))

    def test_stroke_width_rule(self):
        # font-assets.md stroke-width rule / font-rendering.md section 5: every stroke at least d
        # texels of white coverage (median over the glyph's inked rows for vertical strokes,
        # columns for bars), or one-texel strokes vanish under nearest-sampled minification.
        sc = _load('stem_coverage', ROOT / 'verification/results/font-rendering/stem_coverage.py')
        for name in self.fonts:
            S = self.stock[name]['S']
            stem = Path(self.tmp.name) / 'F' / ('%s%d' % (name, S * D))
            res = sc.measure(stem.with_suffix('.abc').read_bytes(),
                             stem.with_suffix('.tga').read_bytes(), self.ff)
            self.assertTrue(set('ilr!|Itj-T') <= set(res), name)
            self.assertEqual(sc.failures(res, D), [], '%s %s' % (name, res))

    def test_metric_ratios_against_stock(self):
        # The a-z mean advance is fitted (on the rounded advances) to d x stock, so it must hold
        # to 1 %. Cap height and baseline are placed on whole rows at d x stock, +-1 row for the
        # antialiased edge. Single letters keep the substitute's own proportions: +-15 % for H
        # and g (measured 0.92..1.08), +-25 % for the space (measured 0.83..0.90).
        for name in self.fonts:
            h, cmap, recs, W, H, px = self.glyphs(name)
            st = self.stock[name]
            adv = sum(recs[cmap[o]][4] + recs[cmap[o]][6] for o in range(97, 123)) / 26
            self.assertAlmostEqual(adv / (D * st['advance_a_to_z_mean']), 1.0, delta=0.01)
            for ch, key, tol in (('H', 'H', 0.15), ('g', 'g', 0.15), (' ', 'space', 0.25)):
                r = recs[cmap[ord(ch)]]
                ratio = (r[4] + r[6]) / (D * (st[key]['A'] + st[key]['C']))
                self.assertAlmostEqual(ratio, 1.0, delta=tol, msg='%s %r' % (name, ch))
            u0, v0, u1, v1, a, b, c, _ = recs[cmap[ord('H')]]
            x0, y0 = int(W * u0), int(H * v0)
            rows = [r for r in range(round(H * (v1 - v0)))
                    if any(px[((y0 + r) * W + x) * 4 + 3] > 127 for x in range(x0, x0 + b))]
            s0, s1 = st['H_ink_rows_in_cell']
            yoff = st['yoff']
            self.assertLessEqual(abs((rows[-1] - rows[0] + 1) - D * (s1 - s0 + 1)), 1, name)
            self.assertLessEqual(abs(rows[-1] + 1 - (yoff * D + D * (s1 + 1 - yoff))), 1, name)


if __name__ == '__main__':
    unittest.main()
