#!/usr/bin/env python3
"""Fog route step C (docs/architecture/fog-gpu-cost.md): the exporter with --march-scale reproduces the pinned references at
spacing 2 bit for bit. Pairs of reference directories (a pinned one, a fresh export of the same packets):
every reference.npz array and cases.txt compared exactly.
Usage: /usr/bin/python3 verification/results/fog-gpu-cost/step_c_exporter_identity.py PINNED FRESH [PINNED FRESH ...]"""
import sys
from pathlib import Path
import numpy as np

ok = True
for pinned, fresh in zip(sys.argv[1::2], sys.argv[2::2]):
    a, b = np.load(Path(pinned) / 'reference.npz'), np.load(Path(fresh) / 'reference.npz')
    same = sum(np.array_equal(a[k], b[k]) for k in a.files if k in b.files)
    cases = (Path(pinned) / 'cases.txt').read_bytes() == (Path(fresh) / 'cases.txt').read_bytes()
    extra = sorted(set(a.files) ^ set(b.files))
    ok = ok and same == len(a.files) and cases and not extra
    print(f'{Path(pinned).name}: arrays identical {same}/{len(a.files)} cases identical {cases} only_one_side {extra}')
print('result', 'PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
