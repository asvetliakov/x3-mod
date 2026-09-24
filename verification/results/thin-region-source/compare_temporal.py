#!/usr/bin/env python3
"""X3M_TAA_THIN_REGION_SOURCE, temporal pass evidence (docs/verification/temporal-resolve.md, "thin-region source A/B"):
temporal-pass.txt against HEAD (sha256, must be identical with the default source), the lattice report against HEAD by
first word of every differing line (the new THIN_SOURCE* rows, the two twin budget rows and BOX_HALF_BASE are expected;
wall-clock rows differ run to run), every other line identical, and the THIN_SOURCE_TIMING rows. Run from the checkout
after run_temporal_pass.py; prints only these rows."""
import difflib
import hashlib
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RESULTS = 'verification/results/bottle-X3/'
committed = lambda path: subprocess.run(['git', 'show', 'HEAD:' + path], cwd=ROOT, capture_output=True, check=True).stdout
sha = lambda data: hashlib.sha256(data).hexdigest()

old, new = committed(RESULTS + 'temporal-pass.txt'), (ROOT / RESULTS / 'temporal-pass.txt').read_bytes()
print(f'temporal-pass.txt committed={sha(old)[:16]} now={sha(new)[:16]} identical={int(old == new)}')

WALL_CLOCK = ('LINE_TIMING', 'LINE_TIMING_CAMERA', 'LINE_TIMING_CAMERA_LANE', 'LINE_TIMING_SENTINEL', 'BOX_HALF_TIMING', 'THIN_REGION_CAMERA_TIMING')
old_lines = committed(RESULTS + 'temporal-lattice.txt').decode().splitlines()
new_lines = (ROOT / RESULTS / 'temporal-lattice.txt').read_text().splitlines()
diff = [l for l in difflib.ndiff(old_lines, new_lines) if l[:2] in ('- ', '+ ')]
first = lambda line: line[2:].split()[0] if line[2:].split() else ''
kinds = {}
for line in diff:
    kinds.setdefault((line[0], first(line)), []).append(line[2:])
print(f'temporal-lattice.txt lines committed={len(old_lines)} now={len(new_lines)} differing={len(diff)}')
for (sign, kind), lines in sorted(kinds.items()):
    note = 'wall_clock' if kind in WALL_CLOCK else ''
    if kind == 'RESOLVE_BUDGET':
        note = ' '.join(l.split()[1] for l in lines)
    elif kind in ('RESULT', 'BOX_HALF_BASE'):
        note = ' | '.join(lines)
    print(f'  {sign} {kind} x{len(lines)} {note}'.rstrip())
unexpected = sorted({k for (_, k) in kinds} - set(WALL_CLOCK) - {'RESOLVE_BUDGET', 'RESULT', 'BOX_HALF_BASE', 'THIN_SOURCE', 'THIN_SOURCE_IDENTITY',
                                                                 'THIN_SOURCE_INVALID', 'THIN_SOURCE_RESET', 'THIN_SOURCE_TIMING', 'THIN_SOURCE_CASES', 'CHECK'})
print('unexpected kinds:', unexpected or 'none')
changed_budget = [l for l in kinds.get(('-', 'RESOLVE_BUDGET'), [])]
print('removed RESOLVE_BUDGET rows (an existing program changed):', len(changed_budget))
for line in new_lines:
    if line.startswith(('THIN_SOURCE_TIMING', 'RESOLVE_BUDGET variant=embedded_line_mask')):
        print(line)
