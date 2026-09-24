#!/usr/bin/env python3
"""The temporal pass and its lattice mode against the committed summary (copied from verification/results/thin-vote; the
fade owner changes no TemporalPass program or input: the main report must be identical, the lattice report differs by
the new THIN_REGION_HOLD_FADE_OWNER row and timing rows, see lattice_diff.py): pass flags, check counts and the two
report hashes, plus the budget rows of the embedded programs. Prints one line per field and a verdict.
usage: compare_temporal.py [baseline-rev]   (default HEAD; verification/results/bottle-X3/temporal-pass-summary.json)"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PATH = 'verification/results/bottle-X3/temporal-pass-summary.json'
rev = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
old = json.loads(subprocess.run(['git', 'show', f'{rev}:{PATH}'], cwd=ROOT, capture_output=True, text=True, check=True).stdout)
new = json.loads((ROOT / PATH).read_text())


def pick(record):
    lattice = record.get('lattice', {})
    return {'passed': record.get('passed'), 'report_sha256': record.get('report_sha256'),
            'checks': record.get('checks'), 'numerical_checks': record.get('numerical_checks'), 'state_checks': record.get('state_checks'),
            'lattice_exit': lattice.get('exit_code'), 'lattice_report_sha256': lattice.get('report_sha256'),
            'lattice_checks': lattice.get('checks'), 'lattice_numerical': lattice.get('numerical_checks'), 'lattice_state': lattice.get('state_checks'),
            'budget': {k: v.get('instruction_slots') for k, v in lattice.get('budget', {}).items()}}


a, b = pick(old), pick(new)
same = True
for key in a:
    equal = a[key] == b[key]
    same &= equal
    print(f'{key}: committed={a[key]!r} now={b[key]!r} {"same" if equal else "DIFFERS"}')
print(f'verdict={"identical" if same else "differs"}')
