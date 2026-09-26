#!/usr/bin/env python3
"""Rotation-aware motion weight (docs/architecture/taa-motion-history-weight.md section 10): the re-recorded temporal
fixture reports against a committed baseline, and the new rows.

Usage: python3 compare_records.py [BASE_COMMIT]   (default 1abdd4e8, the commit the option was built on)
The lattice report at 1abdd4e8 is stale (committed before 22776b6f's re-record was kept): compare it against a fresh
run of the base commit, not this default, for its identity.
Prints, per report, the lines of the baseline that are missing from the current file (a multiset difference, so moved
lines do not count), and every MOTION_WEIGHT_ROTATION* row of the current temporal-pass.txt, first generation.
"""
from collections import Counter
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
BASE = sys.argv[1] if len(sys.argv) > 1 else '1abdd4e8'
REPORTS = ('verification/results/bottle-X3/temporal-pass.txt', 'verification/results/bottle-X3/temporal-lattice.txt')

for name in REPORTS:
    old = subprocess.run(['git', '-C', str(ROOT), 'show', f'{BASE}:{name}'], check=True, capture_output=True,
                         text=True).stdout.splitlines()
    new = (ROOT / name).read_text().splitlines()
    missing = Counter(old) - Counter(new)
    added = Counter(new) - Counter(old)
    print(f'{name}: baseline {len(old)} lines, current {len(new)}; missing from current {sum(missing.values())}, '
          f'added {sum(added.values())}')
    for line, count in sorted(missing.items()):
        print(f'  missing x{count}: {line[:200]}')
    kinds = Counter(line.split(' ', 1)[0] for line in added.elements())
    print('  added by kind:', dict(sorted(kinds.items())))

text = (ROOT / REPORTS[0]).read_text().splitlines()
rows = [line for line in text if line.startswith('MOTION_WEIGHT_ROTATION')]
print(f'MOTION_WEIGHT_ROTATION rows: {len(rows)} (two generations)')
keep = ('program', 'row', 'pan_px', 'weight', 'on', 'e_ratio', 'ripple_rms', 'output_diff', 'age_diff')
for line in rows[:len(rows) // 2]:
    fields = dict(re.findall(r'(\w+)=(\S+)', line))
    if line.startswith('MOTION_WEIGHT_ROTATION_CLASSES') or line.startswith('MOTION_WEIGHT_ROTATION_CONTOUR'):
        print('  ' + line.split(' ', 1)[0].rsplit('_', 1)[1].lower(), ' '.join(f'{k}={v}' for k, v in fields.items()))
    else:
        print('  ', ' '.join(f'{k}={fields[k]}' for k in keep))
same = [line for line in rows[:len(rows) // 2]] == [line for line in rows[len(rows) // 2:]]
print('generation 2 identical to generation 1:', same)
