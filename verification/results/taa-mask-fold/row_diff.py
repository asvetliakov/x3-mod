#!/usr/bin/env python3
"""Row-type diff of the temporal fixture reports against the committed ones (docs/verification/temporal-resolve.md, "Mask
fold"): for temporal-pass.txt and temporal-lattice.txt under verification/results/bottle-X3/, the committed version (git
HEAD, or the revision given as the first argument) against the working tree. Per row type (the first token): the line
counts before / after and how many lines differ in order; wall-clock rows (*TIMING*) are counted but not compared.
Run from the repository root."""
import collections
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
REVISION = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
for name in ('temporal-pass.txt', 'temporal-lattice.txt'):
    path = f'verification/results/bottle-X3/{name}'
    old = subprocess.run(['git', 'show', f'{REVISION}:{path}'], capture_output=True, text=True, cwd=ROOT, check=True).stdout.splitlines()
    new = (ROOT / path).read_text().splitlines()
    group = lambda lines: collections.OrderedDict((k, [l for l in lines if l.split(' ', 1)[0] == k]) for k in dict.fromkeys(l.split(' ', 1)[0] for l in lines))
    before, after = group(old), group(new)
    print(f'== {name}: {len(old)} -> {len(new)} lines')
    for key in list(dict.fromkeys(list(before) + list(after))):
        a, b = before.get(key, []), after.get(key, [])
        if 'TIMING' in key:
            print(f'{key} {len(a)} -> {len(b)} (timing, not compared)')
            continue
        differ = sum(1 for x, y in zip(a, b) if x != y) + abs(len(a) - len(b))
        if differ:
            print(f'{key} {len(a)} -> {len(b)} differing={differ}')
    same = [k for k in before if k in after and 'TIMING' not in k and before[k] == after[k]]
    print(f'identical row types: {len(same)}')
