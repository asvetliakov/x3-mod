#!/usr/bin/env python3
"""Stored bytes per body (body member / atlas textures) of overlay catalogues; reads only.

  python3 verification/results/lod-overlay-pilot/dat_members.py <addon/NN.cat> ...
"""
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools' / 'analysis'))
from inspect_x3 import read_catalogue  # noqa: E402

for cat in map(Path, sys.argv[1:]):
    sizes = Counter()
    for e in read_catalogue(cat):
        stem = Path(e['path']).stem
        body = stem[len('x3m_lod_'):].rsplit('_', 1)[0] if stem.startswith('x3m_lod_') else stem
        sizes[body, 'body' if e['path'].lower().endswith('.pbb') else 'textures'] += e['size']
    print(f'{cat}: dat {cat.with_suffix(".dat").stat().st_size} bytes, members total {sum(sizes.values())}')
    for body in sorted({b for b, _ in sizes}):
        print(f'  {body}: body {sizes[body, "body"]} textures {sizes[body, "textures"]}'
              f' total {sizes[body, "body"] + sizes[body, "textures"]}')
