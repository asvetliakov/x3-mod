#!/usr/bin/env python3
"""Classify MoltenVK shader-library compile failures in a smoke run's full log (kept local beside the
fixture executable, e.g. build-d3d9smoke/d3d9-backend-smoke-crossover-dxvk-1.10.3.full.txt):
python3 compile_errors.py <full log>"""
import collections
import re
import sys

text = open(sys.argv[1], errors='replace').read()
blocks = [b.split('[mvk-error]')[0] for b in text.split('Shader library compile failed')[1:]]
kinds = collections.Counter()
for block in blocks:
    found = frozenset(re.findall(r"cannot reserve '(\w+)' resource location", block))
    kinds[', '.join(sorted(found)) or 'other'] += 1
print(f'{len(blocks)} compile failures; Metal resource-index collisions by kind: {dict(kinds)}')
