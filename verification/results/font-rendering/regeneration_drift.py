#!/usr/bin/env python3
"""Compare a regeneration of one committed density font with the committed file (rasteriser drift check).

Regenerates the named font at one density into a temp dir with tools/fonts/generate_fonts.py and prints, for the
.abc: header and glyph-record differences; for the .tga: image size and alpha/colour pixel differences. Prints the
Pillow and FreeType versions it ran with. Usage: regeneration_drift.py [Harrier] [2]
"""
import contextlib
import importlib.util
import io
import sys
import tempfile
from pathlib import Path

import numpy as np
import PIL
from PIL import ImageFont

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    font = sys.argv[1] if len(sys.argv) > 1 else 'Harrier'
    density = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    ff = load('font_files', HERE / 'font_files.py')
    gen = load('generate_fonts', ROOT / 'tools/fonts/generate_fonts.py')
    print('pillow', PIL.__version__, 'freetype', ImageFont.core.freetype2_version, 'numpy', np.__version__)
    with tempfile.TemporaryDirectory() as tmp:
        with contextlib.redirect_stdout(io.StringIO()):
            gen.main(['--out', tmp, '--density', str(density), '--font', font])
        (new_path,) = [p for p in (Path(tmp) / 'F').glob('*.abc')]
        new_abc, new_tga = new_path.read_bytes(), new_path.with_suffix('.tga').read_bytes()
    old_path = ROOT / 'assets/fonts/generated/F' / new_path.name
    old_abc, old_tga = old_path.read_bytes(), old_path.with_suffix('.tga').read_bytes()
    (h0, c0, r0), (h1, c1, r1) = ff.parse_abc(old_abc), ff.parse_abc(new_abc)
    print(new_path.stem, 'abc bytes', len(old_abc), len(new_abc), 'identical', old_abc == new_abc)
    print('abc header differs', {k: (h0[k], h1.get(k)) for k in h0 if h0[k] != h1.get(k)})
    print('abc cmap identical', c0 == c1, 'records', len(r0), len(r1),
          'records differing', sum(a != b for a, b in zip(r0, r1)))
    t0, t1 = ff.parse_tga(old_tga), ff.parse_tga(new_tga)
    print('tga size', (t0['width'], t0['height']), (t1['width'], t1['height']), 'identical', old_tga == new_tga)
    if (t0['width'], t0['height']) == (t1['width'], t1['height']):
        n = t0['width'] * t0['height'] * 4
        a = np.frombuffer(old_tga[18:18 + n], np.uint8).reshape(-1, 4).astype(int)
        b = np.frombuffer(new_tga[18:18 + n], np.uint8).reshape(-1, 4).astype(int)
        diff = np.abs(a - b)
        print('tga pixels', len(a), 'alpha differing', int((diff[:, 3] > 0).sum()),
              'alpha max abs diff', int(diff[:, 3].max()), 'alpha mean abs diff over differing',
              round(float(diff[:, 3][diff[:, 3] > 0].mean()), 3) if (diff[:, 3] > 0).any() else 0,
              'colour differing', int((diff[:, :3] > 0).any(axis=1).sum()))


if __name__ == '__main__':
    main()
