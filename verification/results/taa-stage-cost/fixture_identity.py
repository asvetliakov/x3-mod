#!/usr/bin/env python3
"""Identity of the temporal pass fixture after the TAA stage cuts (docs/architecture/engine-frame-time.md,
"TAA stage cost"). For the lattice-mode reports of run_temporal_pass.py (committed, a baseline run of the
HEAD programs, the run with the cuts), lists the line kinds that differ from the committed report (expected:
RESOLVE_BUDGET of the three changed programs and the LINE_TIMING rows, which are CPU wall times) and the timing
rows side by side. The main report's identity was checked with cmp (fixture_identity_out.txt).
Usage: fixture_identity.py COMMITTED_LATTICE BASELINE_LATTICE NEW_LATTICE"""
import re
import sys
from pathlib import Path


lattices = sys.argv[1:]
names = ('committed', 'baseline', 'new')
reports = [Path(p).read_text().splitlines() for p in lattices]
base, *others = reports
for name, lines in zip(names[1:], others):
    kinds = sorted({l.split(' ', 1)[0] + (' ' + l.split()[1] if l.startswith('RESOLVE_BUDGET') else '')
                    for a, l in zip(base, lines) if a != l})
    print('lattice committed vs %s: lines=%d/%d differing kinds=%s' % (name, len(base), len(lines), kinds))
for name, lines in zip(names, reports):
    for l in lines:
        if l.startswith(('LINE_TIMING ', 'LINE_TIMING_CAMERA ', 'LINE_TIMING_SENTINEL ')):
            fields = dict(re.findall(r'(\w+_ms)=(\S+)', l))
            print(name, l.split()[0], ' '.join(f'{k}={v}' for k, v in fields.items()))
