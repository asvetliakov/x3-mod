#!/usr/bin/env python3
"""The lattice report (verification/results/bottle-X3/temporal-lattice.txt) against the committed one (HEAD): prints the
first word of every differing line and the final pass/numerical/state line of both, so a wall-clock-only difference
(LINE_TIMING rows) is visible without reading the report."""
import difflib
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PATH = 'verification/results/bottle-X3/temporal-lattice.txt'
old = subprocess.run(['git', 'show', 'HEAD:' + PATH], cwd=ROOT, capture_output=True, text=True, check=True).stdout.splitlines()
new = (ROOT / PATH).read_text().splitlines()
removed = [l for l in difflib.ndiff(old, new) if l.startswith('- ')]
added = [l for l in difflib.ndiff(old, new) if l.startswith('+ ')]
kinds = sorted({l[2:].split()[0] if l[2:].split() else '' for l in removed + added})
print(f'lines committed={len(old)} now={len(new)} removed={len(removed)} added={len(added)} kinds={kinds}')
for tag, lines in (('committed', old), ('now', new)):
    tail = [l for l in lines if 'numerical=' in l or 'PASS' in l]
    print(tag, tail[-1] if tail else '')
