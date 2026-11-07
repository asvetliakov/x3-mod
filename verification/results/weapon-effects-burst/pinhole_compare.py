#!/usr/bin/env python3
"""pinhole_compare.py <before_dir> <after_dir>: the hull burst before and after the fireball program's pinhole fix
(2026-11-07: the outward drift's direction blends to a seed-hashed one within 0.08 of the disc's centre). Both dirs hold
the fixture's dump of `energy_single` at 120 and 296 ms (X3M_WEAPON_EFFECTS_DUMP_CASES=energy_single
X3M_WEAPON_EFFECTS_DUMP_MS=120,296); measure.py's figures per -burst / -glow part and their relative change (bound 2 %).
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def rows(d):
    out = subprocess.run([sys.executable, str(HERE / 'measure.py'), d], capture_output=True, text=True, check=True).stdout
    table = {}
    for line in out.splitlines()[1:]:
        f = line.split()
        table[(f[0], int(f[1]), f[2])] = (float(f[3]), int(f[4]), float(f[5]))
    return table


before, after = rows(sys.argv[1]), rows(sys.argv[2])
worst = 0.0
print('case ms part peak_before peak_after lit_before lit_after eq_r_before eq_r_after max_rel_change')
for k in sorted(before):
    b, a = before[k], after.get(k)
    if not a:
        continue
    rel = max(abs(a[i] - b[i]) / b[i] if b[i] else 0.0 for i in range(3))
    worst = max(worst, rel)
    print(f'{k[0]} {k[1]} {k[2]} {b[0]:.4f} {a[0]:.4f} {b[1]} {a[1]} {b[2]:.1f} {a[2]:.1f} {rel:.4%}')
print(f'worst_relative_change {worst:.4%} bound 2% {"PASS" if worst < 0.02 else "FAIL"}')
